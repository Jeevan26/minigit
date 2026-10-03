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

        // Configure the repository for Git's SHA-256 object format.
        fs::path config_path = ".mgit/config";
        std::ofstream config_file(config_path);
        config_file << "[core]\n"
                    << "\trepositoryformatversion = 1\n"
                    << "\tfilemode = true\n"
                    << "\tbare = false\n"
                    << "[extensions]\n"
                    << "\tobjectFormat = sha256\n";
        config_file.close();
        if (!config_file)
        {
            std::cerr << "Failed to write repository config" << std::endl;
            return 1;
        }

        // Point Git and minigit at the initial branch.
        std::ofstream head_file(".mgit/HEAD");
        head_file << "ref: refs/heads/main\n";
        head_file.close();
        if (!head_file)
        {
            std::cerr << "Failed to write HEAD reference" << std::endl;
            return 1;
        }

        // Keep minigit's staging data separate from Git's native index.
        fs::path index_path = ".mgit/minigit-index";
        std::ofstream index_file(index_path);
        index_file.close();
        if (!index_file)
        {
            std::cerr << "Failed to create index file" << std::endl;
            return 1;
        }

        std::cout << "Initialized an empty repository" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing repository: " << e.what() << std::endl;
        return 1;
    }
}