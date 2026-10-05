#include <string>
#include <iostream>
#include <filesystem>

// header files
#include "headers/init.hpp"
#include "headers/add.hpp"
#include "headers/commit.hpp"
#include "headers/config.hpp"
#include "headers/destory.hpp"
#include "headers/log.hpp"
#include "headers/status_command.hpp"

inline int status(Status result) { return result == Status::Failure; }

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
        return status(init());
    }
    else if (command == "add")
    {
        std::string object = argv[2] ? argv[2] : "";
        return status(add(object));
    }
    else if (command == "commit")
    {
        std::string message = argv[2] ? argv[2] : "";
        return status(commit(message));
    }
    else if (command == "config")
    {
        std::string cfg = argv[2] ? argv[2] : "";
        std::string arg = argv[3] ? argv[3] : "";
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
