#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "checkout.hpp"
#include "hash.hpp"
#include "object.hpp"
#include "paths.hpp"
#include "repository.hpp"
#include "write.hpp"
#include "status.hpp"

namespace
{
    namespace fs = std::filesystem;

    struct FileEntry
    {
        std::string hash;
    };

    using FileMap = std::map<std::string, FileEntry>;

    bool valid_branch_name(const std::string &name)
    {
        if (name.empty() || name.back() == '/' || name.find("..") != std::string::npos)
            return false;

        std::size_t start = 0;
        while (start < name.size())
        {
            const auto slash = name.find('/', start);
            const std::string component = name.substr(start, slash == std::string::npos ? slash : slash - start);
            if (component.empty() || component.front() == '.' || component.back() == '.')
                return false;
            for (unsigned char character : component)
                if (!std::isalnum(character) && character != '-' && character != '_' && character != '.')
                    return false;
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        return true;
    }

    Status read_single_line(const fs::path &path, std::string &line)
    {
        std::ifstream file(path, std::ios::binary);
        std::string extra;
        if (!file || !std::getline(file, line) || std::getline(file, extra) || file.bad())
            return Status::Failure;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return Status::Success;
    }

    Status flatten_tree(const std::string &tree_hash, const std::string &prefix, FileMap &files)
    {
        std::vector<object_store::TreeEntry> entries;
        if (object_store::read_tree(tree_hash, entries) != Status::Success)
            return Status::Failure;
        for (const auto &entry : entries)
        {
            if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
                entry.name.find('/') != std::string::npos)
            {
                std::cerr << "Malformed path in commit tree" << std::endl;
                return Status::Failure;
            }
            const std::string path = prefix.empty() ? entry.name : prefix + "/" + entry.name;
            if (entry.is_directory)
            {
                if (flatten_tree(entry.hash, path, files) != Status::Success)
                    return Status::Failure;
            }
            else if (!files.emplace(path, FileEntry{entry.hash}).second)
            {
                std::cerr << "Malformed file entry in commit tree" << std::endl;
                return Status::Failure;
            }
        }
        return Status::Success;
    }

    Status load_commit_files(const std::string &commit_hash, FileMap &files)
    {
        std::string contents;
        if (object_store::read_object(commit_hash, "commit", contents) != Status::Success)
            return Status::Failure;
        const auto newline = contents.find('\n');
        if (newline == std::string::npos || contents.compare(0, 5, "tree ") != 0)
        {
            std::cerr << "Malformed commit object: " << commit_hash << std::endl;
            return Status::Failure;
        }
        const std::string tree_hash = contents.substr(5, newline - 5);
        if (!object_store::valid_hash(tree_hash) || flatten_tree(tree_hash, "", files) != Status::Success)
        {
            std::cerr << "Malformed commit tree: " << commit_hash << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    Status load_index(FileMap &files)
    {
        std::ifstream index(mgit::paths::index_file, std::ios::binary);
        if (!index)
        {
            std::cerr << "Failed to read repository index" << std::endl;
            return Status::Failure;
        }
        std::string line;
        while (std::getline(index, line))
        {
            const auto first = line.find('\t');
            const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
            if (first == std::string::npos || first == 0 || first + 1 == line.size())
            {
                std::cerr << "Malformed repository index" << std::endl;
                return Status::Failure;
            }
            FileEntry entry;
            std::string path;
            if (second == std::string::npos)
            {
                entry = {line.substr(0, first)};
                path = line.substr(first + 1);
            }
            else
            {
                if (second == first + 1 || second + 1 == line.size())
                {
                    std::cerr << "Malformed repository index" << std::endl;
                    return Status::Failure;
                }
                entry.hash = line.substr(first + 1, second - first - 1);
                path = line.substr(second + 1);
            }
            if (!object_store::valid_hash(entry.hash) ||
                path.empty() || fs::path(path).is_absolute() ||
                path.find('\n') != std::string::npos || path.find('\0') != std::string::npos ||
                !files.emplace(path, std::move(entry)).second)
            {
                std::cerr << "Malformed repository index" << std::endl;
                return Status::Failure;
            }
        }
        if (index.bad())
        {
            std::cerr << "Failed to read repository index" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    std::string serialize_index(const FileMap &files)
    {
        std::string contents;
        for (const auto &[path, entry] : files)
            contents += entry.hash + '\t' + path + '\n';
        return contents;
    }

    std::string blob_hash(const std::string &contents)
    {
        return hash("blob " + std::to_string(contents.size()) + '\0' + contents);
    }

    bool contains_symlink_component(const fs::path &path)
    {
        fs::path current;
        for (const auto &component : path)
        {
            current /= component;
            std::error_code error;
            const auto status = fs::symlink_status(current, error);
            if (!error && fs::is_symlink(status))
                return true;
        }
        return false;
    }

    bool worktree_is_clean(const FileMap &head_files, const FileMap &index_files)
    {
        for (const auto &[path, staged_entry] : index_files)
        {
            const auto committed = head_files.find(path);
            if (committed == head_files.end() || staged_entry.hash != committed->second.hash)
            {
                std::cerr << "Cannot checkout with staged changes; commit or unstage them first" << std::endl;
                return false;
            }
        }

        for (const auto &[path, head_entry] : head_files)
        {
            const fs::path file_path(path);
            if (contains_symlink_component(file_path) || !fs::is_regular_file(file_path))
            {
                std::cerr << "Cannot checkout with modified or missing file: " << path << std::endl;
                return false;
            }
            std::ifstream file(file_path, std::ios::binary);
            const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (file.bad() || blob_hash(contents) != head_entry.hash)
            {
                std::cerr << "Cannot checkout with modified file: " << path << std::endl;
                return false;
            }
        }
        return true;
    }

    Status read_head(std::string &commit_hash)
    {
        std::string head_contents;
        if (read_single_line(mgit::paths::head_file, head_contents) != Status::Success)
        {
            std::cerr << "Failed to read repository HEAD" << std::endl;
            return Status::Failure;
        }
        if (head_contents.rfind("ref: ", 0) == 0)
        {
            const std::string reference = head_contents.substr(5);
            const std::string heads_prefix = mgit::paths::refs_heads_reference.generic_string() + "/";
            if (reference.rfind(heads_prefix, 0) != 0 ||
                !valid_branch_name(reference.substr(heads_prefix.size())))
            {
                std::cerr << "Invalid repository HEAD reference" << std::endl;
                return Status::Failure;
            }
            const fs::path branch_path = mgit::paths::repository_dir / reference;
            if (fs::exists(branch_path))
            {
                if (read_single_line(branch_path, commit_hash) != Status::Success)
                {
                    std::cerr << "Malformed current branch reference" << std::endl;
                    return Status::Failure;
                }
                if (!commit_hash.empty() && !object_store::valid_hash(commit_hash))
                {
                    std::cerr << "Malformed current branch reference" << std::endl;
                    return Status::Failure;
                }
            }
            else
                commit_hash.clear();
            return Status::Success;
        }
        commit_hash = head_contents;
        if (!object_store::valid_hash(commit_hash))
        {
            std::cerr << "Invalid detached HEAD commit hash" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    Status resolve_branch(const std::string &branch_name, std::string &commit_hash)
    {
        if (valid_branch_name(branch_name))
        {
            const fs::path possible_branch = mgit::paths::refs_heads_dir / branch_name;
            if (fs::is_regular_file(possible_branch))
            {
                if (read_single_line(possible_branch, commit_hash) != Status::Success ||
                    (!commit_hash.empty() && !object_store::valid_hash(commit_hash)))
                {
                    std::cerr << "Malformed branch reference: " << branch_name << std::endl;
                    return Status::Failure;
                }
                return Status::Success;
            }
        }
        std::cerr << "Local branch not found: " << branch_name << std::endl;
        return Status::Failure;
    }

    Status checkout_to(const std::string &target_commit, const std::string &branch_name, bool create_branch)
    {
        std::string old_commit;
        if (read_head(old_commit) != Status::Success)
            return Status::Failure;

        FileMap old_files;
        if (!old_commit.empty() && load_commit_files(old_commit, old_files) != Status::Success)
            return Status::Failure;
        FileMap index_files;
        if (load_index(index_files) != Status::Success || !worktree_is_clean(old_files, index_files))
            return Status::Failure;

        FileMap target_files;
        if (!target_commit.empty() && load_commit_files(target_commit, target_files) != Status::Success)
            return Status::Failure;

        for (const auto &[path, entry] : target_files)
        {
            (void)entry;
            const fs::path output_path(path);
            std::error_code status_error;
            const auto output_status = fs::symlink_status(output_path, status_error);
            const bool output_exists = !status_error && fs::exists(output_status);
            if (output_exists && old_files.find(path) == old_files.end())
            {
                std::cerr << "Checkout would overwrite untracked file: " << path << std::endl;
                return Status::Failure;
            }
            fs::path parent = output_path.parent_path();
            while (!parent.empty() && parent != ".")
            {
                std::error_code parent_error;
                const auto parent_status = fs::symlink_status(parent, parent_error);
                if (!parent_error && fs::is_symlink(parent_status))
                {
                    std::cerr << "Checkout path contains a symbolic link: " << parent << std::endl;
                    return Status::Failure;
                }
                if (!parent_error && fs::exists(parent_status) && !fs::is_directory(parent_status))
                {
                    std::cerr << "Checkout path is blocked by an existing file: " << parent << std::endl;
                    return Status::Failure;
                }
                parent = parent.parent_path();
            }
        }

        std::map<std::string, std::string> target_contents;
        for (const auto &[path, entry] : target_files)
        {
            std::string contents;
            if (object_store::read_object(entry.hash, "blob", contents) != Status::Success)
            {
                std::cerr << "Missing or malformed file object for: " << path << std::endl;
                return Status::Failure;
            }
            target_contents.emplace(path, std::move(contents));
        }

        for (const auto &[path, entry] : old_files)
        {
            (void)entry;
            if (target_files.find(path) == target_files.end())
            {
                std::error_code error;
                if (!fs::remove(path, error) || error)
                {
                    std::cerr << "Failed to remove tracked file during checkout: " << path << std::endl;
                    return Status::Failure;
                }
            }
        }

        for (const auto &[path, entry] : target_files)
        {
            const std::string &contents = target_contents.at(path);
            const fs::path output_path(path);
            std::error_code error;
            if (!output_path.parent_path().empty())
                fs::create_directories(output_path.parent_path(), error);
            if (error)
            {
                std::cerr << "Failed to create checkout directory: " << error.message() << std::endl;
                return Status::Failure;
            }
            if (write_file_atomically(output_path, contents) != Status::Success)
                return Status::Failure;
        }

        if (write_file_atomically(mgit::paths::index_file, serialize_index(target_files)) != Status::Success)
            return Status::Failure;

        const std::string branch_reference =
            mgit::paths::refs_heads_reference.generic_string() + "/" + branch_name;
        if (write_file_atomically(mgit::paths::head_file, "ref: " + branch_reference + "\n") != Status::Success)
            return Status::Failure;

        std::cout << (create_branch ? "Switched to a new branch '" : "Switched to branch '")
                  << branch_name << "'" << std::endl;
        return Status::Success;
    }
}

Status checkout(const std::string &branch_name)
{
    if (!is_repository_initialized("No repository found in this project"))
        return Status::Failure;
    std::string commit_hash;
    if (resolve_branch(branch_name, commit_hash) != Status::Success)
        return Status::Failure;
    return checkout_to(commit_hash, branch_name, false);
}

Status checkout_new_branch(const std::string &branch_name)
{
    if (!is_repository_initialized("No repository found in this project"))
        return Status::Failure;

    if (!valid_branch_name(branch_name))
    {
        std::cerr << "Invalid branch name: " << branch_name << std::endl;
        return Status::Failure;
    }
    
    const fs::path new_branch_path = mgit::paths::refs_heads_dir / branch_name;
    if (fs::exists(new_branch_path))
    {
        std::cerr << "Branch already exists: " << branch_name << std::endl;
        return Status::Existing;
    }

    std::string head_commit;
    if (read_head(head_commit) != Status::Success)
        return Status::Failure;

    std::error_code error;
    fs::create_directories(new_branch_path.parent_path(), error);
    if (error)
    {
        std::cerr << "Failed to create branch directory: " << error.message() << std::endl;
        return Status::Failure;
    }

    if (write_file_atomically(new_branch_path, head_commit + "\n") != Status::Success)
        return Status::Failure;
        
    if (checkout_to(head_commit, branch_name, true) != Status::Success)
    {
        fs::remove(new_branch_path, error);
        return Status::Failure;
    }
    return Status::Success;
}
