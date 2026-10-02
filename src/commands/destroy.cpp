#include <filesystem>
#include <iostream>

#include "destory.hpp"

namespace fs = std::filesystem;

bool destroy()
{
    fs::path mgit_repo = ".mgit";

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