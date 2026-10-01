#include <filesystem>
#include <iostream>
#include <fstream>

#include "init.hpp"

namespace fs = std::filesystem;

int init()
{

    if (fs::is_directory("./.mgit"))
    {
        std::cout << "A repository already exists!" << std::endl;
        return 0;
    }

    // Try creating the .mgit directory and its subdirectories
    try
    {
        fs::create_directory(".mgit");
        fs::create_directories(".mgit/objects");
        fs::create_directories(".mgit/refs/heads");
        fs::create_directories(".mgit/refs/tags");
        fs::create_directories(".mgit/refs/remotes");
        fs::create_directories(".mgit/hooks");
        fs::create_directories(".mgit/logs");
        fs::create_directories(".mgit/info");

        //Create an index file
        fs::path file_path = ".mgit/index";
        std::ofstream file(file_path);
        file.close();
        
        std::cout << "Initialized an empty repository" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing repository: " << e.what() << std::endl;
        return 1;
    }
}