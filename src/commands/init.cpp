#include <filesystem>
#include <iostream>
#include <fstream>

#include "init.hpp"
#include "paths.hpp"
#include "status.hpp"
#include "repository.hpp"

namespace fs = std::filesystem;

Status init()
{

    if (is_repository_initialized())
    {
        std::cout << "A repository already exists!" << std::endl;
        return Status::Existing;
    }

    // Try creating the .mgit directory and its subdirectories
    try
    {
        fs::create_directory(mgit::paths::repository_dir);
        fs::create_directories(mgit::paths::objects_dir);
        fs::create_directories(mgit::paths::refs_heads_dir);

        // Configure the repository for Git's SHA-256 object format.
        const fs::path &config_path = mgit::paths::config_file;
        std::ofstream config_file(config_path);
        config_file << "[core]\n"
                    << "\trepositoryformatversion = 1\n"
                    << "\tbare = false\n"
                    << "[extensions]\n"
                    << "\tobjectFormat = sha256\n";
        config_file.close();
        if (!config_file)
        {
            std::cerr << "Failed to write repository config" << std::endl;
            return Status::Failure;
        }

        // Point Git and minigit at the initial branch.
        std::ofstream head_file(mgit::paths::head_file);
        head_file << "ref: "
                  << mgit::paths::refs_heads_reference.generic_string() << "/main\n";
        head_file.close();
        if (!head_file)
        {
            std::cerr << "Failed to write HEAD reference" << std::endl;
            return Status::Failure;
        }

        // Keep minigit's staging data separate from Git's native index.
        std::ofstream index_file(mgit::paths::index_file);
        index_file.close();
        if (!index_file)
        {
            std::cerr << "Failed to create index file" << std::endl;
            return Status::Failure;
        }

        std::cout << "Initialized an empty repository" << std::endl;
        return Status::Success;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing repository: " << e.what() << std::endl;
        return Status::Failure;
    }
}