#pragma once

#include <filesystem>

namespace mgit::paths
{
    extern const std::filesystem::path repository_dir;
    extern const std::filesystem::path objects_dir;
    extern const std::filesystem::path refs_heads_dir;
    extern const std::filesystem::path refs_heads_reference;
    extern const std::filesystem::path config_file;
    extern const std::filesystem::path head_file;
    extern const std::filesystem::path index_file;
}
