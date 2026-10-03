#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#include "commit.hpp"
#include "hash.hpp"

namespace
{
    namespace fs = std::filesystem;

    // Some path constants
    const fs::path mgit_repo = ".mgit";
    const fs::path index_path = mgit_repo / "index";
    const fs::path config_file_path = mgit_repo / "config";
    const fs::path objects_path = mgit_repo / "objects";
    const fs::path branch_path = mgit_repo / "refs" / "heads" / "main";

    struct CommitFormat
    {
        std::string hash;
        std::string parent;
        std::string author_name;
        std::string author_email;
        std::time_t time;
        std::string message;
    };

    enum Status
    {
        Success,
        Failure,
        Malformed
    };

    bool valid_hash(const std::string &hash)
    {
        return hash.size() == 64 &&
               std::all_of(hash.begin(), hash.end(), [](unsigned char c)
                           { return std::isxdigit(c) != 0; });
    }

    // Trim all kinds of whitespaces
    std::string trim(const std::string &str)
    {
        const auto first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos)
            return "";

        const auto last = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(first, last - first + 1);
    }

    // Fetch author details
    Status get_user_config(std::ifstream &config_file, std::string &name, std::string &email)
    {
        if (!config_file.is_open())
        {
            std::cerr << "Unable to read config file" << std::endl;
            return Failure;
        }

        name = email = "";

        std::string line;
        bool user_flag = false;
        while (getline(config_file, line))
        {
            if (user_flag)
            {
                const auto separator = line.find('=');

                // If separator was not found
                if (separator == std::string::npos)
                    return Malformed;

                std::string config_type = trim(line.substr(0, separator));
                std::string config_value = trim(line.substr(separator + 1));

                if (config_type == "name")
                    name = config_value;
                else if (config_type == "email")
                {
                    email = config_value;
                    user_flag = false;
                }
                else
                    return Malformed;
            }

            if (trim(line) == "[user]")
                user_flag = true;
        }

        if (user_flag)
            return Malformed;

        if (config_file.bad())
        {
            std::cerr << "An error occurred while trying to read config file" << std::endl;
            return Failure;
        }
        else
            return Success;
    }

    Status load_staged_entries(std::unordered_map<std::string, std::string> &entries)
    {
        if (!fs::exists(index_path))
            return Malformed;

        std::ifstream index(index_path);
        if (!index)
        {
            std::cerr << "Failed to read index file" << std::endl;
            return Failure;
        }

        try
        {
            std::string line;
            while (getline(index, line))
            {
                const auto separator = line.find('\t');

                // Check for malformed lines
                if (separator == std::string::npos || separator == 0 || separator == line.size() - 1)
                    return Malformed;

                // file_name = file_hash
                entries[line.substr(separator + 1)] = line.substr(0, separator);
            }

            if (index.bad())
            {
                std::cerr << "Failed to read the index file" << std::endl;
                return Failure;
            }

            return Success;
        }
        catch (const std::exception &error)
        {
            std::cerr << "An error occurred while reading index file: " << error.what() << std::endl;
            return Failure;
        }
    }

    Status load_commit_entries(const std::string &commit_hash, std::unordered_map<std::string, std::string> &entries)
    {
        std::ifstream commit_file(objects_path / commit_hash);
        if (!commit_file)
        {
            std::cerr << "Failed to open parent commit object" << std::endl;
            return Failure;
        }

        std::string line;
        if (!getline(commit_file, line) || line != "tree")
            return Malformed;

        bool msg_found = false;
        bool meta_found = false;
        while (getline(commit_file, line))
        {
            if (line == "message")
            {
                msg_found = true;
                break;
            }

            // If we find a blob
            if (line.rfind("blob\t") == 0)
            {
                if (meta_found)
                    return Malformed;

                const auto hash_separator = line.find('\t', 5);

                // An entry is of the form: blob<TAB>hash<TAB>path
                if (hash_separator == std::string::npos || hash_separator == line.length() - 1 || hash_separator == 5)
                    return Malformed;

                std::string object_hash = line.substr(5, hash_separator - 5);
                if (!valid_hash(object_hash))
                    return Malformed;

                entries[line.substr(hash_separator + 1)] = object_hash;
            }

            else if (line.rfind("parent", 0) == 0 || line.rfind("author", 0) == 0 || line.rfind("timestamp", 0) == 0)
                meta_found = true;

            else
                return Malformed;
        }

        if (commit_file.bad())
        {
            std::cerr << "An error occurred while trying to read the commit file" << std::endl;
            return Failure;
        }

        if (!msg_found)
            return Malformed;

        return Success;
    }

    Status write(const fs::path &file_path, const std::string &content)
    {
        const fs::path temp_path = file_path.string() + ".tmp";
        std::ofstream temp(temp_path, std::ios::binary | std::ios::trunc);

        if (!temp)
        {
            std::cerr << "Failed to open a temp file for writing" << std::endl;
            return Failure;
        }

        temp.write(content.data(), static_cast<std::streamsize>(content.size()));
        temp.close();

        if (temp.fail())
        {
            std::cout << "Error writing to temp file" << std::endl;
            return Failure;
        }

        std::error_code error;
        fs::rename(temp_path, file_path, error);
        if (error)
        {
            fs::remove(temp_path);
            std::cerr << "Error renaming temp file" << std::endl;
            return Failure;
        }

        return Success;
    }
}

bool commit(std::string &message)
{
    if (!fs::exists(mgit_repo))
    {
        std::cerr << "No repo initialized for the current project" << std::endl;
        return false;
    }

    if (trim(message).empty())
    {
        std::cout << "Enter your commit message: " << std::endl;
        getline(std::cin, message);

        if (trim(message).empty())
        {
            std::cerr << "Invalid commit message. Please try again" << std::endl;
            return false;
        }
    }

    if (!fs::exists(index_path) || !fs::exists(config_file_path))
    {
        std::cerr << "Malformed mgit repo found!" << std::endl;
        return false;
    }

    std::string name, email;
    std::ifstream config_file(config_file_path);
    Status s1 = get_user_config(config_file, name, email);

    if (s1 == Malformed)
    {
        std::cerr << "Malformed config file found" << std::endl;
        return false;
    }
    if (s1 == Failure)
        return false;

    std::unordered_map<std::string, std::string> staged_entries;
    Status s2 = load_staged_entries(staged_entries);

    if (s2 == Malformed)
    {
        std::cerr << "Malformed index file found" << std::endl;
        return false;
    }
    if (s2 == Failure)
        return false;

    if (trim(name).empty() || trim(email).empty())
    {
        std::cerr << "Please configure your user name and email before committing" << std::endl;
        return false;
    }

    std::unordered_map<std::string, std::string> commit_entries;
    std::string parent;

    if (fs::exists(branch_path))
    {
        std::ifstream branch_file(branch_path);
        std::string extra_line;

        if (!branch_file)
        {
            std::cerr << "Failed to read current branch" << std::endl;
            return false;
        }
        else if (!getline(branch_file, parent))
        {
            std::cerr << "Malformed branch file found" << std::endl;
            return false;
        }

        parent = trim(parent);
        if (getline(branch_file, extra_line) || !valid_hash(parent))
        {
            std::cerr << "Malformed branch file found" << std::endl;
            return false;
        }
        if (branch_file.bad())
        {
            std::cerr << "Failed to read current branch" << std::endl;
            return false;
        }

        Status s3 = load_commit_entries(parent, commit_entries);
        if (s3 == Malformed)
        {
            std::cerr << "Malformed commit file found" << std::endl;
            return false;
        }
        else if (s3 == Failure)
            return false;
    }

    // Copy staged entries into commit entries
    for (const auto &[file_path, file_hash] : staged_entries)
        commit_entries[file_path] = file_hash;

    // Valid all entries in commit_entrires map
    for (const auto &[file_path, file_hash] : commit_entries)
    {
        if (!valid_hash(file_hash) || !fs::is_regular_file(objects_path / file_hash))
        {
            std::cerr << "Invalid or missing object for staged path: " << file_path << std::endl;
            return false;
        }
        if (file_path.find('\n') != std::string::npos)
        {
            std::cerr << "Malformed entry found" << std::endl;
            return false;
        }
    }

    CommitFormat new_commit;
    new_commit.parent = parent;
    new_commit.author_name = name;
    new_commit.author_email = email;
    new_commit.time = std::time(nullptr);
    new_commit.message = message;

    // Sort entries to avoid duplicate commits
    std::vector<std::pair<std::string, std::string>> sorted_entries(commit_entries.begin(), commit_entries.end());
    std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto &left, const auto &right)
              { return left.first < right.first; });

    // Writing the necessary contents
    std::ostringstream content;
    content << "tree\n";
    for (const auto &[file_path, file_hash] : sorted_entries)
        content << "blob\t" << file_hash << '\t' << file_path << '\n';
    if (!new_commit.parent.empty())
        content << "parent " << new_commit.parent << '\n';
    content << "author " << new_commit.author_name << " <" << new_commit.author_email << ">\n";
    content << "timestamp " << new_commit.time << '\n';
    content << "message\n"
            << new_commit.message << '\n';

    std::string commit_content = content.str();
    new_commit.hash = hash(commit_content);

    const fs::path commit_object_path = objects_path / new_commit.hash;

    Status s4 = write(commit_object_path, commit_content);
    if (s4 == Failure)
        return false;

    Status s5 = write(branch_path, new_commit.hash + "\n");
    if (s5 == Failure)
    {
        std::cerr << "Commit object was created, but current branch couldn't be updated" << std::endl;
        return false;
    }

    Status s6 = write(index_path, "");
    if (s6 == Failure)
    {
        std::cerr << "Commit was created, but staging index couldn't be cleared" << std::endl;
        return false;
    }

    std::cout << "Committed created with hash: " << new_commit.hash << std::endl;
    return true;
}
