#include "arguments.hpp"

#include <algorithm>

ArgumentParseResult parse_arguments(
    const std::vector<std::string> &args,
    const std::vector<std::string> &value_options,
    const std::vector<std::string> &flag_options)
{
    ArgumentParseResult result;
    bool positional_only = false;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string &arg = args[i];
        if (!positional_only && arg == "--")
        {
            positional_only = true;
            continue;
        }

        if (positional_only || arg.empty() || arg[0] != '-')
        {
            result.arguments.positional.push_back(arg);
            continue;
        }

        const std::size_t equals = arg.find('=');
        const std::string option = arg.substr(0, equals);
        const auto value_option = std::find(value_options.begin(), value_options.end(), option);
        const auto flag_option = std::find(flag_options.begin(), flag_options.end(), option);

        if (value_option != value_options.end())
        {
            std::string value;
            if (equals != std::string::npos)
            {
                value = arg.substr(equals + 1);
            }
            else
            {
                if (i + 1 >= args.size())
                {
                    result.error = "Option requires a value: " + option;
                    return result;
                }
                value = args[++i];
            }

            if (!result.arguments.options.emplace(option, value).second)
            {
                result.error = "Option provided more than once: " + option;
                return result;
            }
        }
        else if (flag_option != flag_options.end())
        {
            if (equals != std::string::npos)
            {
                result.error = "Flag does not take a value: " + option;
                return result;
            }
            if (!result.arguments.options.emplace(option, "true").second)
            {
                result.error = "Option provided more than once: " + option;
                return result;
            }
        }
        else
        {
            result.error = "Unknown option: " + option;
            return result;
        }
    }

    return result;
}
