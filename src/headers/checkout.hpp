#pragma once

#include "status.hpp"

#include <string>

Status checkout(const std::string &branch_name);
Status checkout_new_branch(const std::string &branch_name);
