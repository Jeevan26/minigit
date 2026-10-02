#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "hash.hpp"

namespace fs = std::filesystem;

namespace
{
    enum Status
    {
        Success,
        Failure,
        Existing
    };

    /// Checks if a child is within a given parent
    bool is_within(const fs::path &child, const fs::path &parent)
    {
        // Path to reach child from parent
        const fs::path relative = child.lexically_relative(parent);
        return (!relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..");
    }

    /// Loads entries from the index file
    bool load_entries(const fs::path &index_path, std::unordered_map<std::string, std::string> &entries)
    {
        // Throw an error cause this shouldn't occur
        if (!fs::exists(index_path))
            throw std::runtime_error("Invalid index path given!");

        std::ifstream index(index_path);
        if (!index)
        {
            std::cerr << "Failed to open index" << std::endl;
            return false;
        }

        try
        {
            std::string line;
            while (std::getline(index, line))
            {
                const auto separator = line.find('\t');

                // Check for malformed lines
                if (separator == std::string::npos || separator == 0 || separator + 1 == line.size())
                {
                    std::cerr << "Invalid index entry" << std::endl;
                    return false;
                }

                entries[line.substr(separator + 1)] = line.substr(0, separator);
            }

            if (index.bad())
            {
                std::cerr << "Failed to read index" << std::endl;
                return false;
            }
        }
        catch (const std::exception &error)
        {
            std::cerr << "Failed to read index: " << error.what() << std::endl;
            return false;
        }

        return true;
    }

    /// Saves entries to the index file
    bool save_entries(const fs::path &index_path, const std::unordered_map<std::string, std::string> &entries)
    {
        const fs::path temporary_path = index_path.string() + ".tmp";
        std::ofstream index(temporary_path, std::ios::trunc);
        if (!index)
        {
            std::cerr << "Failed to open temporary index for writing" << std::endl;
            return false;
        }

        for (const auto &[file_path, object_hash] : entries)
            index << object_hash << '\t' << file_path << '\n';

        if (!index)
        {
            std::cerr << "Failed to write temporary index" << std::endl;
            fs::remove(temporary_path);
            return false;
        }

        index.close();
        
        if (!index)
        {
            fs::remove(temporary_path);
            std::cerr << "Failed to close temporary index" << std::endl;
            return false;
        }

        std::error_code error;
        fs::rename(temporary_path, index_path, error);
        if (error)
        {
            std::error_code remove_error;
            fs::remove(index_path, remove_error);
            if (remove_error)
            {
                fs::remove(temporary_path);
                std::cerr << "Failed to replace index: " << error.message() << std::endl;
                return false;
            }

            error.clear();
            fs::rename(temporary_path, index_path, error);
            if (error)
            {
                fs::remove(temporary_path);
                std::cerr << "Failed to replace index: " << error.message() << std::endl;
                return false;
            }
        }

        return true;
    }

    Status add_file(const fs::path &file_path)
    {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Error opening file: " << file_path << std::endl;
            return Failure;
        }

        std::string object_hash;
        try
        {
            object_hash = hash(file);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Failed to hash file: " << error.what() << std::endl;
            return Failure;
        }

        const fs::path object_path = fs::path(".mgit") / "objects" / object_hash;
        const fs::path index_path = ".mgit/index";
        const std::string index_key = file_path.lexically_normal().generic_string();
        std::unordered_map<std::string, std::string> entries;

        if (!load_entries(index_path, entries))
        {
            std::cerr << "An error occured whilst trying to fetch index entries" << std::endl;
            return Failure;
        }

        const auto existing_entry = entries.find(index_key);
        if (existing_entry != entries.end() && existing_entry->second == object_hash)
        {
            std::cerr << "File is already staged: " << file_path << std::endl;
            return Existing;
        }

        bool object_created = false;
        if (!fs::exists(object_path))
        {
            std::error_code error;
            fs::copy_file(file_path, object_path, fs::copy_options::none, error);
            if (error)
            {
                std::cerr << "Failed to store object: " << error.message() << std::endl;
                return Failure;
            }
            object_created = true;
        }

        entries[index_key] = object_hash;
        if (!save_entries(index_path, entries))
        {
            if (object_created)
                fs::remove(object_path);
            return Failure;
        }

        return Success;
    }

    Status add_folder(const fs::path &folder_path)
    {
        bool added_file = false;

        try
        {
            fs::path git_repo = ".mgit";
            for (fs::recursive_directory_iterator it(folder_path), end; it != end; ++it)
            {
                if (is_within(it->path(), git_repo))
                {
                    it.disable_recursion_pending();
                    continue;
                }

                if (it->is_regular_file())
                {
                    const Status result = add_file(it->path());
                    if (result == Failure)
                        return Failure;
                    if (result == Success)
                        added_file = true;
                }
            }
        }
        catch (const fs::filesystem_error &error)
        {
            std::cerr << "Error traversing directory: " << error.what() << std::endl;
            return Failure;
        }

        return added_file ? Success : Existing;
    }
}

bool add(const std::string &object_name)
{
    const fs::path object(object_name);

    if (!fs::exists(object))
    {
        std::cerr << "No such file or directory: " << object << std::endl;
        return false;
    }

    fs::path git_repo = ".mgit";
    if (is_within(object, git_repo))
    {
        std::cerr << "Cannot add files within mgit repository" << std::endl;
        return false;
    }

    if (!fs::exists(git_repo))
    {
        std::cerr << "No repository found. Try running \"mgit init\" first" << std::endl;
        return false;
    }

    fs::path index_path = ".mgit/index";
    if (!fs::exists(index_path))
    {
        std::cerr << "Malformed repository found!" << std::endl;
        return false;
    }

    // If object is folder
    if (fs::is_directory(object))
    {
        const Status result = add_folder(object);
        if (result == Failure)
            return false;
        if (result == Existing)
        {
            std::cerr << "No new files to stage" << std::endl;
            return false;
        }

        std::cout << "Successfully added files to staging" << std::endl;
        return true;
    }

    // If object is a file
    else if (fs::is_regular_file(object))
    {
        try
        {
            if (add_file(object) == Success)
            {
                std::cout << "Successfully added the file to staging" << std::endl;
                return true;
            }

            else
                return false;
        }
        catch (const std::exception &error)
        {
            std::cout << "An error occured while trying to add file: " << error.what() << std::endl;
            return false;
        }
    }

    // If object is neither a file nor a directory
    else
    {
        std::cerr << "Only files or directories can be a valid input" << std::endl;
        return false;
    }
}