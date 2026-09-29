#include "init.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void init(){
    fs::create_directories(".mgit/directories");
    fs::create_directories(".mgit/refs/heads");
    fs::create_directories(".mgit/refs/tags");

    std::cout << "Initialized an empty repository" << std::endl;
}