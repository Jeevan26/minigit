#include <string>
#include <iostream>
#include <filesystem>

// header files
#include "headers/init.hpp"
#include "headers/add.hpp"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Please provide sufficient arguments" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "init")
        init();
    else if (command == "add")
    {
        std::string path = argv[2] ? argv[2] : "";
        add(path);
    }
    else
        std::cerr << "Invalid command" << std::endl;

    return 0;
}
