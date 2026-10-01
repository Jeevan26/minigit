#include <string>
#include <istream>
#include <vector>

#pragma once

std::string hash(const std::vector<u_int8_t> &data);
std::string hash(std::istream &input);