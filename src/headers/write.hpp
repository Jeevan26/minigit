#pragma once

#include "status.hpp"

#include <filesystem>
#include <string>

Status write_file_atomically(const std::filesystem::path &path, const std::string &contents);
