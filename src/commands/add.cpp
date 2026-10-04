#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>

#include "object.hpp"
#include "paths.hpp"
#include "repository.hpp"

namespace fs = std::filesystem;

namespace
{
    struct IndexEntry
    {
        std::string hash;
        std::string mode;
    };

    enum Status
    {
        Success,
        Failure,
        Existing
    };

    /// Checks if a child is within a given parent
    bool is_within(const fs::path &child, const fs::path &parent)
    {
        const fs::path relative = child.lexically_normal().lexically_relative(parent.lexically_normal());
        return (!relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..");
    }

    /// Loads entries from the index file
    bool load_entries(const fs::path &index_path, std::unordered_map<std::string, IndexEntry> &entries)
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
                const auto first_separator = line.find('\t');
                if (first_separator == std::string::npos || first_separator == 0 ||
                    first_separator + 1 == line.size())
                {
                    std::cerr << "Invalid index entry" << std::endl;
                    return false;
                }

                const auto second_separator = line.find('\t', first_separator + 1);
                if (second_separator == std::string::npos)
                    entries[line.substr(first_separator + 1)] = {line.substr(0, first_separator), "100644"};
                else if (second_separator == first_separator + 1 || second_separator + 1 == line.size())
                {
                    std::cerr << "Invalid index entry" << std::endl;
                    return false;
                }
                else
                    entries[line.substr(second_separator + 1)] = {
                        line.substr(first_separator + 1, second_separator - first_separator - 1),
                        line.substr(0, first_separator)};
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
    bool save_entries(const fs::path &index_path, const std::unordered_map<std::string, IndexEntry> &entries)
    {
        const fs::path temporary_path = index_path.string() + ".tmp";
        std::ofstream index(temporary_path, std::ios::trunc);
        if (!index)
        {
            std::cerr << "Failed to open temporary index for writing" << std::endl;
            return false;
        }

        for (const auto &[file_path, entry] : entries)
            index << entry.mode << '\t' << entry.hash << '\t' << file_path << '\n';

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

        const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (file.bad())
        {
            std::cerr << "Failed to read file: " << file_path << std::endl;
            return Failure;
        }

        std::string object_hash;
        try
        {
            if (!object_store::write_object("blob", contents, object_hash))
                return Failure;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Failed to hash file: " << error.what() << std::endl;
            return Failure;
        }

        const std::string index_key = file_path.lexically_normal().generic_string();
        std::unordered_map<std::string, IndexEntry> entries;

        const auto permissions = fs::status(file_path).permissions();
        const bool executable = (permissions & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
        const std::string mode = executable ? "100755" : "100644";

        if (!load_entries(mgit::paths::index_file, entries))
        {
            std::cerr << "An error occured whilst trying to fetch index entries" << std::endl;
            return Failure;
        }

        const auto existing_entry = entries.find(index_key);
        if (existing_entry != entries.end() && existing_entry->second.hash == object_hash &&
            existing_entry->second.mode == mode)
            return Existing;

        entries[index_key] = {object_hash, mode};
        if (!save_entries(mgit::paths::index_file, entries))
            return Failure;

        return Success;
    }

    Status add_folder(const fs::path &folder_path)
    {
        bool added_file = false;

        try
        {
            const fs::path &git_repo = mgit::paths::repository_dir;
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

    const fs::path &git_repo = mgit::paths::repository_dir;
    if (is_within(object, git_repo))
    {
        std::cerr << "Cannot add files within mgit repository" << std::endl;
        return false;
    }

    if (!is_repository_initialized("No repository found. Try running \"mgit init\" first"))
        return false;

    const fs::path &index_path = mgit::paths::index_file;
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
        std::cout << (result == Success ? "Successfully added files to staging" : "Already up to date") << std::endl;
        return true;
    }

    // If object is a file
    else if (fs::is_regular_file(object))
    {
        try
        {
            const Status result = add_file(object);
            if (result == Failure)
                return false;
            std::cout << (result == Success ? "Successfully added the file to staging" : "File is already up to date") << std::endl;
            return true;
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