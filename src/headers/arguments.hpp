#pragma once

#include <map>
#include <string>
#include <vector>

struct ParsedArguments
{
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
};

struct ArgumentParseResult
{
    ParsedArguments arguments;
    std::string error;

    explicit operator bool() const
    {
        return error.empty();
    }
};

ArgumentParseResult parse_arguments(
    const std::vector<std::string> &args,
    const std::vector<std::string> &value_options,
    const std::vector<std::string> &flag_options = {});
