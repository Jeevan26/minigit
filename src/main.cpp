#include <string>
#include <iostream>
#include <filesystem>
#include <vector>

// header files
#include "headers/init.hpp"
#include "headers/add.hpp"
#include "headers/commit.hpp"
#include "headers/config.hpp"
#include "headers/destory.hpp"
#include "headers/log.hpp"
#include "headers/checkout.hpp"
#include "headers/arguments.hpp"
#include "headers/status_command.hpp"

inline int status(Status result, bool existing_is_success = false)
{
    return result == Status::Success || (existing_is_success && result == Status::Existing) ? 0 : 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Please provide sufficient arguments" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "init")
    {
        return status(init(), true);
    }
    else if (command == "add")
    {
        std::string object = argc > 2 ? argv[2] : "";
        return status(add(object));
    }
    else if (command == "commit")
    {
        std::string message = argc > 2 ? argv[2] : "";
        return status(commit(message));
    }
    else if (command == "config")
    {
        std::string cfg = argc > 2 ? argv[2] : "";
        std::string arg = argc > 3 ? argv[3] : "";
        return status(config(cfg, arg));
    }
    else if (command == "destroy")
    {
        return status(destroy());
    }
    else if (command == "log")
    {
        return status(log());
    }
    else if (command == "checkout")
    {
        std::vector<std::string> arguments;
        for (int i = 2; i < argc; ++i)
            arguments.emplace_back(argv[i]);

        const auto parsed = parse_arguments(arguments, {"-b"});
        if (!parsed)
        {
            std::cerr << parsed.error << std::endl;
            return 1;
        }
        const auto branch = parsed.arguments.options.find("-b");
        if (branch != parsed.arguments.options.end())
        {
            if (!parsed.arguments.positional.empty())
            {
                std::cerr << "Usage: mgit checkout -b <branch>" << std::endl;
                return 1;
            }
            return status(checkout_new_branch(branch->second));
        }
        if (parsed.arguments.positional.size() != 1)
        {
            std::cerr << "Usage: mgit checkout <branch> | checkout -b <branch>" << std::endl;
            return 1;
        }
        return status(checkout(parsed.arguments.positional.front()));
    }
    else if (command == "status")
    {
        return status(status_command());
    }
    else
    {
        std::cerr << "Invalid command" << std::endl;
        return 1;
    }
}
