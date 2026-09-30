#include <filesystem>
#include <iostream>

#include "add.hpp"
#include "hash.hpp"

namespace fs = std::filesystem;

void add(std::string object)
{
    fs::path git_folder = "./.mgit";

    if (!fs::is_directory(git_folder))
        std::cerr << "No repository found for the current project!\nTry running \"mgit init\" first" << std::endl;
    else if (!fs::is_directory(object))
        std::cerr << "Please input a valid file or directory" << std::endl;

    else
    {
    }
}
