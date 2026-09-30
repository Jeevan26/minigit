#include <filesystem>
#include <iostream>

#include "init.hpp"

namespace fs = std::filesystem;

void init()
{

    if (fs::is_directory("./.mgit"))
    {
        std::cout << "A repository already exists!" << std::endl;
        return;
    }

    // Try creating the .mgit directory and its subdirectories
    try {
        fs::create_directory(".mgit");
        fs::create_directories(".mgit/objects");
        fs::create_directories(".mgit/refs/heads");
        fs::create_directories(".mgit/refs/tags");
        fs::create_directories(".mgit/refs/remotes");
        fs::create_directories(".mgit/hooks");
        fs::create_directories(".mgit/logs");
        fs::create_directories(".mgit/info");
        
        std::cout << "Initialized an empty repository" << std::endl;
        
    } catch (const std::exception &e) {
        std::cerr << "Error initializing repository: " << e.what() << std::endl;
        return;
    }
}