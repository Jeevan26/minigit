#pragma once

#include "status.hpp"

#include <string>
#include <vector>

namespace object_store
{
    struct TreeEntry
    {
        std::string mode;
        std::string name;
        std::string hash;
    };

    bool valid_hash(const std::string &hash);
    Status write_object(const std::string &type, const std::string &contents, std::string &object_hash);
    Status read_object(const std::string &object_hash, const std::string &expected_type, std::string &contents);
    Status write_tree(const std::vector<TreeEntry> &entries, std::string &tree_hash);
    Status read_tree(const std::string &tree_hash, std::vector<TreeEntry> &entries);
}
