#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "hash.hpp"
#include "object.hpp"
#include "paths.hpp"
#include "repository.hpp"
#include "status_command.hpp"

namespace
{
    namespace fs = std::filesystem;

    struct FileEntry
    {
        std::string hash;
    };

    Status load_index(std::unordered_map<std::string, FileEntry> &entries)
    {
        std::ifstream index(mgit::paths::index_file);
        if (!index)
        {
            std::cerr << "Failed to read index file" << std::endl;
            return Status::Failure;
        }

        std::string line;
        while (std::getline(index, line))
        {
            const auto first = line.find('\t');
            if (first == std::string::npos || first == 0 || first + 1 == line.size())
            {
                std::cerr << "Malformed index file" << std::endl;
                return Status::Failure;
            }
            const auto second = line.find('\t', first + 1);
            FileEntry entry;
            std::string path;
            if (second == std::string::npos)
            {
                entry = {line.substr(0, first)};
                path = line.substr(first + 1);
            }
            else if (second == first + 1 || second + 1 == line.size())
            {
                std::cerr << "Malformed index file" << std::endl;
                return Status::Failure;
            }
            else
            {
                entry = {line.substr(first + 1, second - first - 1)};
                path = line.substr(second + 1);
            }
            if (!object_store::valid_hash(entry.hash) || path.empty())
            {
                std::cerr << "Malformed index file" << std::endl;
                return Status::Failure;
            }
            entries[path] = entry;
        }
        if (index.bad())
        {
            std::cerr << "Failed to read index file" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
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

    Status load_head(std::string &branch_name, std::string &commit_hash)
    {
        std::ifstream head(mgit::paths::head_file);
        std::string reference;
        std::string extra_line;
        if (!head || !std::getline(head, reference) || std::getline(head, extra_line) || head.bad())
        {
            std::cerr << "Failed to read repository HEAD" << std::endl;
            return Status::Failure;
        }
        if (!reference.empty() && reference.back() == '\r')
            reference.pop_back();

        if (reference.rfind("ref: ", 0) == 0)
        {
            fs::path branch_path;
            if (current_branch_path(branch_path) != Status::Success)
                return Status::Failure;

            branch_name = branch_path.lexically_relative(mgit::paths::refs_heads_dir).generic_string();
            if (!fs::exists(branch_path))
                return Status::Success;

            std::ifstream branch(branch_path);
            std::string extra_branch_line;
            if (!branch || !std::getline(branch, commit_hash) ||
                std::getline(branch, extra_branch_line) || branch.bad())
            {
                std::cerr << "Invalid current branch reference" << std::endl;
                return Status::Failure;
            }
            if (!commit_hash.empty() && commit_hash.back() == '\r')
                commit_hash.pop_back();
            if (!object_store::valid_hash(commit_hash))
            {
                std::cerr << "Invalid current branch commit hash" << std::endl;
                return Status::Failure;
            }
            return Status::Success;
        }

        if (!object_store::valid_hash(reference))
        {
            std::cerr << "Invalid detached HEAD commit hash" << std::endl;
            return Status::Failure;
        }
        commit_hash = reference;
        branch_name = "HEAD detached at " + reference.substr(0, 7);
        return Status::Success;
    }

    Status load_committed_entries(const std::string &commit_hash,
                                  std::unordered_map<std::string, FileEntry> &entries)
    {
        if (commit_hash.empty())
            return Status::Success;

        std::string contents;
        if (object_store::read_object(commit_hash, "commit", contents) != Status::Success)
        {
            std::cerr << "Failed to read current commit" << std::endl;
            return Status::Failure;
        }
        std::istringstream commit(contents);
        std::string line;
        if (!std::getline(commit, line) || line.rfind("tree ", 0) != 0)
        {
            std::cerr << "Malformed current commit" << std::endl;
            return Status::Failure;
        }
        const std::string tree_hash = line.substr(5);
        if (!object_store::valid_hash(tree_hash) ||
            flatten_tree(tree_hash, "", entries) != Status::Success)
        {
            std::cerr << "Malformed current commit tree" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    Status read_working_file(const fs::path &path, FileEntry &entry)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Failed to read file: " << path << std::endl;
            return Status::Failure;
        }
        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        if (file.bad())
        {
            std::cerr << "Failed to read file: " << path << std::endl;
            return Status::Failure;
        }
        try
        {
            entry.hash = hash("blob " + std::to_string(contents.size()) + '\0' + contents);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Failed to hash file " << path << ": " << error.what() << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    bool is_repository_path(const fs::path &path)
    {
        const fs::path repository_path =
            (fs::current_path() / mgit::paths::repository_dir).lexically_normal();
        const fs::path relative = path.lexically_normal().lexically_relative(
            repository_path);
        return !relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..";
    }

    Status load_working_entries(std::unordered_map<std::string, FileEntry> &entries)
    {
        try
        {
            for (fs::recursive_directory_iterator it(fs::current_path()), end; it != end; ++it)
            {
                if (is_repository_path(it->path()) || it->path().filename() == ".git")
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file())
                    continue;

                const std::string path = it->path().lexically_relative(fs::current_path()).generic_string();
                FileEntry entry;
                if (read_working_file(it->path(), entry) != Status::Success)
                    return Status::Failure;
                entries[path] = std::move(entry);
            }
        }
        catch (const fs::filesystem_error &error)
        {
            std::cerr << "Failed to inspect working tree: " << error.what() << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    void print_section(const std::string &title, const std::string &hint,
                       const std::map<std::string, std::string> &changes)
    {
        if (changes.empty())
            return;
        std::cout << title << ":\n";
        if (!hint.empty())
            std::cout << "  " << hint << '\n';
        for (const auto &[path, state] : changes)
            std::cout << "\t" << state << ": " << path << '\n';
        std::cout << '\n';
    }
}

Status status_command()
{
    if (!is_repository_initialized("No repository found in this project"))
        return Status::Failure;
    if (!fs::exists(mgit::paths::index_file))
    {
        std::cerr << "Malformed repository: index file is missing" << std::endl;
        return Status::Failure;
    }

    std::string branch_name;
    std::string commit_hash;
    if (load_head(branch_name, commit_hash) != Status::Success)
        return Status::Failure;

    std::unordered_map<std::string, FileEntry> committed;
    if (load_committed_entries(commit_hash, committed) != Status::Success)
        return Status::Failure;

    std::unordered_map<std::string, FileEntry> index;
    if (load_index(index) != Status::Success)
        return Status::Failure;

    std::unordered_map<std::string, FileEntry> staged = committed;
    for (const auto &[path, entry] : index)
        staged[path] = entry;

    std::unordered_map<std::string, FileEntry> working;
    if (load_working_entries(working) != Status::Success)
        return Status::Failure;

    std::map<std::string, std::string> staged_changes;
    for (const auto &[path, entry] : staged)
    {
        const auto previous = committed.find(path);
        if (previous == committed.end())
            staged_changes[path] = "new file";
        else if (previous->second.hash != entry.hash)
            staged_changes[path] = "modified";
    }

    std::map<std::string, std::string> unstaged_changes;
    for (const auto &[path, entry] : staged)
    {
        const auto current = working.find(path);
        if (current == working.end())
            unstaged_changes[path] = "deleted";
        else if (current->second.hash != entry.hash)
            unstaged_changes[path] = "modified";
    }

    std::map<std::string, std::string> untracked;
    for (const auto &[path, entry] : working)
    {
        (void)entry;
        if (staged.find(path) == staged.end())
            untracked[path] = "untracked";
    }

    if (branch_name.rfind("HEAD detached at ", 0) == 0)
        std::cout << branch_name << '\n';
    else
        std::cout << "On branch " << branch_name << '\n';

    print_section("Changes to be committed",
                  "(use \"mgit add <file>...\" to update what will be committed)",
                  staged_changes);
    print_section("Changes not staged for commit",
                  "",
                  unstaged_changes);
    print_section("Untracked files",
                  "(use \"mgit add <file>...\" to include in what will be committed)",
                  untracked);

    if (staged_changes.empty() && unstaged_changes.empty() && untracked.empty())
        std::cout << "nothing to commit, working tree clean" << std::endl;
    else if (staged_changes.empty())
        std::cout << "no changes added to commit" << std::endl;

    return Status::Success;
}
