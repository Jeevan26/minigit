#pragma once

#include <filesystem>
#include <string>

bool write_file_atomically(const std::filesystem::path &path, const std::string &contents);
