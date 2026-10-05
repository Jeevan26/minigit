#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "commit.hpp"
#include "object.hpp"
#include "paths.hpp"
#include "repository.hpp"
#include "write.hpp"
#include "status.hpp"

namespace
{
    namespace fs = std::filesystem;
    fs::path branch_path;

    struct FileEntry
    {
        std::string hash;
    };

    struct TreeNode
    {
        std::map<std::string, TreeNode> children;
        std::string hash;
        bool is_file = false;
    };

    enum class LoadStatus
    {
        Success,
        Failure,
        Malformed
    };

    std::string trim(const std::string &value)
    {
        const auto first = value.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos)
            return "";
        const auto last = value.find_last_not_of(" \t\n\r\f\v");
        return value.substr(first, last - first + 1);
    }

    Status read_user_config(std::string &name, std::string &email)
    {
        std::ifstream file(mgit::paths::config_file);
        if (!file)
        {
            std::cerr << "Failed to open config file" << std::endl;
            return Status::Failure;
        }

        bool in_user = false;
        std::string line;
        while (std::getline(file, line))
        {
            if (trim(line) == "[user]")
            {
                in_user = true;
                continue;
            }
            if (!in_user)
                continue;

            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const std::string key = trim(line.substr(0, separator));
            const std::string value = trim(line.substr(separator + 1));
            if (key == "name")
                name = value;
            else if (key == "email")
                email = value;
        }
        if (file.bad())
        {
            std::cerr << "Failed to read config file" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    LoadStatus load_staged_entries(std::unordered_map<std::string, FileEntry> &entries)
    {
        std::ifstream index(mgit::paths::index_file);
        if (!index)
        {
            std::cerr << "Failed to read index file" << std::endl;
            return LoadStatus::Failure;
        }

        std::string line;
        while (std::getline(index, line))
        {
            const auto first = line.find('\t');
            if (first == std::string::npos || first == 0 || first + 1 == line.size())
                return LoadStatus::Malformed;
            const auto second = line.find('\t', first + 1);
            if (second == std::string::npos)
                entries[line.substr(first + 1)] = {line.substr(0, first)};
            else if (second == first + 1 || second + 1 == line.size())
                return LoadStatus::Malformed;
            else
                entries[line.substr(second + 1)] = {
                    line.substr(first + 1, second - first - 1)};
        }
        if (index.bad())
        {
            std::cerr << "Failed to read index file" << std::endl;
            return LoadStatus::Failure;
        }
        return LoadStatus::Success;
    }

    Status flatten_tree(const std::string &tree_hash, const std::string &prefix,
                      std::unordered_map<std::string, FileEntry> &entries)
    {
        std::vector<object_store::TreeEntry> tree;
        if (object_store::read_tree(tree_hash, tree) != Status::Success)
            return Status::Failure;
        for (const auto &entry : tree)
        {
            const std::string path = prefix.empty() ? entry.name : prefix + "/" + entry.name;
            if (entry.is_directory)
            {
                if (flatten_tree(entry.hash, path, entries) != Status::Success)
                    return Status::Failure;
            }
            else
                entries[path] = {entry.hash};
        }
        return Status::Success;
    }

    LoadStatus load_parent_entries(const std::string &commit_hash,
                                   std::unordered_map<std::string, FileEntry> &entries)
    {
        std::string contents;
        if (object_store::read_object(commit_hash, "commit", contents) != Status::Success)
            return LoadStatus::Malformed;

        std::istringstream commit(contents);
        std::string line;
        if (!std::getline(commit, line) || line.rfind("tree ", 0) != 0)
            return LoadStatus::Malformed;
        const std::string tree_hash = line.substr(5);
        if (!object_store::valid_hash(tree_hash) || flatten_tree(tree_hash, "", entries) != Status::Success)
            return LoadStatus::Malformed;
        return LoadStatus::Success;
    }

    Status make_tree(const TreeNode &node, std::string &tree_hash)
    {
        std::vector<object_store::TreeEntry> entries;
        for (const auto &[name, child] : node.children)
        {
            if (child.is_file && !child.children.empty())
                return Status::Failure;

            if (child.is_file)
                entries.push_back({false, name, child.hash});
            else
            {
                std::string child_hash;
                if (make_tree(child, child_hash) != Status::Success)
                    return Status::Failure;
                entries.push_back({true, name, child_hash});
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right)
                  {
                      const std::string left_name = left.name + (left.is_directory ? "/" : "");
                      const std::string right_name = right.name + (right.is_directory ? "/" : "");
                      return left_name < right_name;
                  });
        return object_store::write_tree(entries, tree_hash);
    }

    Status build_root_tree(const std::unordered_map<std::string, FileEntry> &files,
                         std::string &tree_hash)
    {
        TreeNode root;
        for (const auto &[path, entry] : files)
        {
            const fs::path file_path(path);
            if (path.empty() || file_path.is_absolute() ||
                path.find('\n') != std::string::npos || path.find('\0') != std::string::npos ||
                !object_store::valid_hash(entry.hash))
            {
                std::cerr << "Invalid path or object in snapshot: " << path << std::endl;
                return Status::Failure;
            }

            TreeNode *current = &root;
            std::size_t start = 0;
            while (start < path.size())
            {
                const auto separator = path.find('/', start);
                const auto end = separator == std::string::npos ? path.size() : separator;
                const std::string component = path.substr(start, end - start);
                if (component.empty() || component == "." || component == "..")
                {
                    std::cerr << "Invalid path in snapshot: " << path << std::endl;
                    return Status::Failure;
                }
                current = &current->children[component];
                if (separator == std::string::npos)
                {
                    if (current->is_file || !current->children.empty())
                    {
                        std::cerr << "Conflicting paths in snapshot: " << path << std::endl;
                        return Status::Failure;
                    }
                    current->is_file = true;
                    current->hash = entry.hash;
                    break;
                }
                if (current->is_file)
                {
                    std::cerr << "Conflicting paths in snapshot: " << path << std::endl;
                    return Status::Failure;
                }
                start = separator + 1;
            }
        }
        return make_tree(root, tree_hash);
    }

    bool same_file_contents(const std::unordered_map<std::string, FileEntry> &left,
                            const std::unordered_map<std::string, FileEntry> &right)
    {
        if (left.size() != right.size())
            return false;
        for (const auto &[path, entry] : left)
        {
            const auto other = right.find(path);
            if (other == right.end() || entry.hash != other->second.hash)
                return false;
        }
        return true;
    }

}

Status commit(std::string &message)
{
    if (!is_repository_initialized("No repo initialized for the current project"))
        return Status::Failure;
    if (trim(message).empty())
    {
        std::cout << "Enter your commit message: " << std::endl;
        std::getline(std::cin, message);
        if (trim(message).empty())
        {
            std::cerr << "Invalid commit message. Please try again" << std::endl;
            return Status::Failure;
        }
    }
    if (!fs::exists(mgit::paths::index_file) || !fs::exists(mgit::paths::config_file))
    {
        std::cerr << "Malformed mgit repo found!" << std::endl;
        return Status::Failure;
    }
    // Resolve symbolic HEAD to its branch, or update HEAD directly while detached.
    std::ifstream head_file(mgit::paths::head_file, std::ios::binary);
    std::string head_reference;
    std::string extra_head_line;
    if (!head_file || !std::getline(head_file, head_reference) ||
        std::getline(head_file, extra_head_line) || head_file.bad())
    {
        std::cerr << "Failed to read repository HEAD" << std::endl;
        return Status::Failure;
    }
    if (!head_reference.empty() && head_reference.back() == '\r')
        head_reference.pop_back();
    const bool detached_head = head_reference.rfind("ref: ", 0) != 0;
    if (detached_head)
    {
        if (!object_store::valid_hash(head_reference))
        {
            std::cerr << "Invalid detached HEAD commit hash" << std::endl;
            return Status::Failure;
        }
        branch_path = mgit::paths::head_file;
    }
    else if (current_branch_path(branch_path) != Status::Success)
        return Status::Failure;

    std::string name;
    std::string email;
    if (read_user_config(name, email) != Status::Success)
        return Status::Failure;
    if (trim(name).empty() || trim(email).empty())
    {
        std::cerr << "Please configure your user name and email before committing" << std::endl;
        return Status::Failure;
    }
    if (name.find_first_of("\r\n") != std::string::npos ||
        email.find_first_of("<>\r\n") != std::string::npos)
    {
        std::cerr << "Configured author name or email contains invalid characters" << std::endl;
        return Status::Failure;
    }

    std::unordered_map<std::string, FileEntry> staged_entries;
    const LoadStatus index_status = load_staged_entries(staged_entries);
    if (index_status != LoadStatus::Success)
    {
        if (index_status == LoadStatus::Malformed)
            std::cerr << "Malformed index file found" << std::endl;
        return Status::Failure;
    }

    std::unordered_map<std::string, FileEntry> parent_entries;
    std::string parent;
    if (detached_head || fs::exists(branch_path))
    {
        std::ifstream branch(branch_path);
        std::string extra_line;
        if (!branch || !std::getline(branch, parent))
        {
            std::cerr << "Malformed branch file found" << std::endl;
            return Status::Failure;
        }
        parent = trim(parent);
        if (std::getline(branch, extra_line) || !object_store::valid_hash(parent) || branch.bad())
        {
            std::cerr << "Malformed branch file found" << std::endl;
            return Status::Failure;
        }
        if (load_parent_entries(parent, parent_entries) != LoadStatus::Success)
        {
            std::cerr << "Malformed parent commit found" << std::endl;
            return Status::Failure;
        }
    }
    std::unordered_map<std::string, FileEntry> snapshot_entries = parent_entries;
    // Apply staged entries over the parent snapshot to produce the next complete tree.
    for (const auto &[path, entry] : staged_entries)
        snapshot_entries[path] = entry;

    for (const auto &[path, entry] : snapshot_entries)
    {
        std::string blob;
        if (!object_store::valid_hash(entry.hash) ||
            object_store::read_object(entry.hash, "blob", blob) != Status::Success)
        {
            std::cerr << "Invalid or missing blob for staged path: " << path << std::endl;
            return Status::Failure;
        }
    }

    std::string tree_hash;
    if (build_root_tree(snapshot_entries, tree_hash) != Status::Success)
        return Status::Failure;

    if (!parent.empty() && same_file_contents(snapshot_entries, parent_entries))
    {
        std::cerr << "Nothing to commit: staged files are identical to the last commit" << std::endl;
        return Status::Failure;
    }

    const std::time_t timestamp = std::time(nullptr);
    std::ostringstream serialized;
    serialized << "tree " << tree_hash << '\n';
    if (!parent.empty())
        serialized << "parent " << parent << '\n';
    serialized << "author " << name << " <" << email << "> " << timestamp << " +0000\n";
    serialized << "committer " << name << " <" << email << "> " << timestamp << " +0000\n\n";
    serialized << message;
    if (message.empty() || message.back() != '\n')
        serialized << '\n';

    std::string commit_hash;
    if (object_store::write_object("commit", serialized.str(), commit_hash) != Status::Success)
        return Status::Failure;

    if (write_file_atomically(branch_path, commit_hash + "\n") != Status::Success)
    {
        std::cerr << "Commit object was created, but current branch couldn't be updated" << std::endl;
        return Status::Failure;
    }
    if (write_file_atomically(mgit::paths::index_file, "") != Status::Success)
    {
        std::cerr << "Commit was created, but staging index couldn't be cleared" << std::endl;
        return Status::Failure;
    }

    std::cout << "Committed created with hash: " << commit_hash << std::endl;
    return Status::Success;
}
