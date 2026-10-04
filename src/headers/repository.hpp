#pragma once

#include "status.hpp"

#include <filesystem>
#include <string>

bool is_repository_initialized(const std::string &error_message = "");
Status current_branch_path(std::filesystem::path &path);
