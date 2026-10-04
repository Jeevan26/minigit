#pragma once

#include <filesystem>
#include <string>

bool is_repository_initialized(const std::string &error_message = "");
bool current_branch_path(std::filesystem::path &path);
