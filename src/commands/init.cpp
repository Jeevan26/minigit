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
        fs::create_directories(".mgit/logs");

        // Create a config file
        fs::path config_path = ".mgit/config";
        std::ofstream config_file(config_path);
        config_file.close();

        //  Create an index file
        fs::path index_path = ".mgit/index";
        std::ofstream index_file(index_path);
        index_file.close();

        std::cout << "Initialized an empty repository" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing repository: " << e.what() << std::endl;
        return 1;
    }
}