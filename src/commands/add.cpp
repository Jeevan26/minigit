#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "add.hpp"
#include "hash.hpp"

namespace fs = std::filesystem;

namespace
{
    enum class FileResult
    {
        Added,
        Existing,
        Failed
    };

    // Check whether candidate is within the parent directory
    bool is_within(const fs::path &candidate, const fs::path &parent)
    {
        const fs::path relative = candidate.lexically_relative(parent);
        return !relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..";
    }

    // Encode a path to a string that can be used as a key in the index
    std::string encode_path(const std::string &path)
    {
        static const char digits[] = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(path.size() * 2);
        for (unsigned char byte : path)
        {
            encoded.push_back(digits[byte >> 4]);
            encoded.push_back(digits[byte & 0x0f]);
        }
        return encoded;
    }

    // Load the index from a file
    bool load_index(const fs::path &index_path, std::map<std::string, std::string> &entries)
    {
        std::ifstream index(index_path);
        if (!index)
        {
            if (!fs::exists(index_path))
                return true;
            std::cerr << "Error opening repository index: " << index_path << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(index, line))
        {
            const auto separator = line.find('\t');
            if (separator == std::string::npos)
            {
                std::cerr << "Invalid repository index entry" << std::endl;
                return false;
            }
            entries[line.substr(separator + 1)] = line.substr(0, separator);
        }
        if (index.bad())
        {
            std::cerr << "Error reading repository index: " << index_path << std::endl;
            return false;
        }
        return true;
    }

    // Save the index to a file
    bool save_index(const fs::path &index_path, const std::map<std::string, std::string> &entries)
    {
        const fs::path temporary_path = index_path.string() + ".tmp";
        {
            std::ofstream index(temporary_path, std::ios::trunc);
            if (!index)
            {
                std::cerr << "Error writing repository index: " << temporary_path << std::endl;
                return false;
            }
            for (const auto &[path, object_hash] : entries)
                index << object_hash << '\t' << path << '\n';
            if (!index)
            {
                std::cerr << "Error writing repository index: " << temporary_path << std::endl;
                return false;
            }
        }

        std::error_code error;
        fs::rename(temporary_path, index_path, error);
        if (error)
        {
            fs::remove(temporary_path);
            std::cerr << "Error replacing repository index: " << error.message() << std::endl;
            return false;
        }
        return true;
    }

    FileResult add_file(const fs::path &file_path, const fs::path &relative_path, const fs::path &git_folder, std::map<std::string, std::string> &entries)
    {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Error opening file: " << file_path << std::endl;
            return FileResult::Failed;
        }

        std::string object_hash;
        try
        {
            object_hash = hash(file);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Error hashing file " << file_path << ": " << error.what() << std::endl;
            return FileResult::Failed;
        }

        const fs::path object_path = git_folder / "objects" / object_hash;
        const bool object_exists = fs::exists(object_path);
        if (!object_exists)
        {
            std::error_code error;
            fs::copy_file(file_path, object_path, fs::copy_options::none, error);
            if (error)
            {
                std::cerr << "Error storing object for " << file_path << ": " << error.message() << std::endl;
                return FileResult::Failed;
            }
        }

        entries[encode_path(relative_path.generic_string())] = object_hash;
        std::cout << (object_exists ? "Staged file (object already exists): " : "Staged file: ") << relative_path << std::endl;
        return object_exists ? FileResult::Existing : FileResult::Added;
    }
}

bool add(const std::string &object)
{
    const fs::path working_directory = fs::current_path();
    const fs::path git_folder = (working_directory / ".mgit").lexically_normal();
    const fs::path index_path = git_folder / "index";
    const fs::path object_path = fs::absolute(object).lexically_normal();

    if (!fs::is_directory(git_folder))
    {
        std::cerr << "No repository found for the current project!\nTry running \"mgit init\" first" << std::endl;
        return false;
    }
    if (!fs::exists(object_path))
    {
        std::cerr << "Please input a valid file or directory" << std::endl;
        return false;
    }
    if (is_within(object_path, git_folder))
    {
        std::cerr << "Cannot add repository metadata" << std::endl;
        return false;
    }

    std::map<std::string, std::string> entries;
    if (!load_index(index_path, entries))
        return false;

    bool success = true;
    if (fs::is_regular_file(object_path))
    {
        const fs::path relative_path = object_path.lexically_relative(working_directory);
        success = add_file(object_path, relative_path, git_folder, entries) != FileResult::Failed;
    }
    else if (fs::is_directory(object_path))
    {
        try
        {
            for (fs::recursive_directory_iterator it(object_path), end; it != end; ++it)
            {
                const fs::path entry_path = fs::absolute(it->path()).lexically_normal();
                if (it->is_directory() && entry_path == git_folder)
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file())
                {
                    const fs::path relative_path = entry_path.lexically_relative(working_directory);
                    if (add_file(entry_path, relative_path, git_folder, entries) == FileResult::Failed)
                        success = false;
                }
            }
        }
        catch (const fs::filesystem_error &error)
        {
            std::cerr << "Error traversing directory " << object_path << ": " << error.what() << std::endl;
            success = false;
        }
    }
    else
    {
        std::cerr << "Input must be a regular file or directory" << std::endl;
        return false;
    }

    return save_index(index_path, entries) && success;
}
