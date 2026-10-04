#include <filesystem>
#include <iostream>

#include "destory.hpp"
#include "paths.hpp"

namespace fs = std::filesystem;

bool destroy()
{
    const fs::path &mgit_repo = mgit::paths::repository_dir;

    if (!fs::exists(mgit_repo))
    {
        std::cerr << "No repo found to delete" << std::endl;
        return true;
    }

    try
    {
        fs::remove_all(mgit_repo);
        std::cout << "Successflly deleted the mgit repo" << std::endl;
        return true;
    }
    catch (fs::filesystem_error error)
    {
        std::cerr << "An error occured while deleting the repo: " << error.what() << std::endl;
        return false;
    }
}