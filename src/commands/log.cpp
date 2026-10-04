#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "log.hpp"
#include "object.hpp"
#include "paths.hpp"
#include "repository.hpp"
#include "status.hpp"

namespace
{
    namespace fs = std::filesystem;
    struct CommitInfo
    {
        std::string parent;
        std::string author;
        std::string date;
        std::string message;
    };

    Status parse_commit(const std::string &contents, CommitInfo &info)
    {
        std::istringstream input(contents);
        std::string line;
        bool tree_found = false;
        bool author_found = false;
        bool committer_found = false;
        while (std::getline(input, line))
        {
            if (line.empty())
            {
                info.message.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
                return tree_found && author_found && committer_found ? Status::Success : Status::Failure;
            }
            if (line.rfind("tree ", 0) == 0)
                tree_found = object_store::valid_hash(line.substr(5));
            else if (line.rfind("parent ", 0) == 0)
            {
                info.parent = line.substr(7);
                if (!object_store::valid_hash(info.parent))
                    return Status::Failure;
            }
            else if (line.rfind("author ", 0) == 0)
            {
                info.author = line.substr(7);
                const auto email_end = info.author.rfind('>');
                if (email_end == std::string::npos || email_end + 2 >= info.author.size())
                    return Status::Failure;
                info.date = info.author.substr(email_end + 2);
                author_found = true;
            }
            else if (line.rfind("committer ", 0) == 0)
                committer_found = !line.substr(10).empty();
            else
                return Status::Failure;
        }
        return Status::Failure;
    }
}

Status log()
{
    if (!is_repository_initialized("No repository found in this project"))
        return Status::Failure;
    std::ifstream head(mgit::paths::head_file);
    std::string head_reference;
    std::string extra_head_line;
    if (!head || !std::getline(head, head_reference) ||
        std::getline(head, extra_head_line) || head.bad())
    {
        std::cerr << "Failed to read repository HEAD" << std::endl;
        return Status::Failure;
    }
    if (!head_reference.empty() && head_reference.back() == '\r')
        head_reference.pop_back();
    std::string commit_hash;
    if (head_reference.rfind("ref: ", 0) == 0)
    {
        fs::path branch_path;
        if (current_branch_path(branch_path) != Status::Success)
            return Status::Failure;
        std::ifstream branch(branch_path);
        std::string extra_line;
        if (!branch || !std::getline(branch, commit_hash) || std::getline(branch, extra_line))
        {
            std::cerr << "Invalid current branch reference" << std::endl;
            return Status::Failure;
        }
        if (!commit_hash.empty() && commit_hash.back() == '\r')
            commit_hash.pop_back();
    }
    else
    {
        commit_hash = head_reference;
    }
    if (!object_store::valid_hash(commit_hash))
    {
        std::cerr << "HEAD points to an invalid commit hash or no commit exists" << std::endl;
        return Status::Failure;
    }

    std::unordered_set<std::string> visited;
    while (!commit_hash.empty())
    {
        if (!visited.insert(commit_hash).second)
        {
            std::cerr << "Commit history contains a parent cycle" << std::endl;
            return Status::Failure;
        }

        std::string contents;
        CommitInfo commit;
        if (object_store::read_object(commit_hash, "commit", contents) != Status::Success ||
            parse_commit(contents, commit) != Status::Success)
        {
            std::cerr << "Malformed commit object: " << commit_hash << std::endl;
            return Status::Failure;
        }

        std::cout << "commit " << commit_hash << '\n'
                  << "Author: " << commit.author << '\n'
                  << "Date:   " << commit.date << "\n\n";
        std::istringstream message(commit.message);
        std::string line;
        while (std::getline(message, line))
            std::cout << "    " << line << '\n';
        std::cout << '\n';
        commit_hash = commit.parent;
    }
    return Status::Success;
}
