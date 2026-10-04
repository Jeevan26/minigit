#include "repository.hpp"
#include "paths.hpp"

#include <fstream>
#include <iostream>
#include <string>

bool current_branch_path(std::filesystem::path &path)
{
    std::ifstream head(mgit::paths::head_file);
    std::string line;
    std::string extra_line;
    if (!head || !std::getline(head, line) || std::getline(head, extra_line) || head.bad())
    {
        std::cerr << "Failed to read repository HEAD" << std::endl;
        return false;
    }

    // Check if the line starts with a prefix
    constexpr const char prefix[] = "ref: ";
    if (line.rfind(prefix, 0) != 0)
    {
        std::cerr << "Invalid repository HEAD reference" << std::endl;
        return false;
    }

    // Validate refs path
    const std::filesystem::path reference = line.substr(sizeof(prefix) - 1);
    auto component = reference.begin();
    if (reference.is_absolute() || component == reference.end() || *component != "refs")
    {
        std::cerr << "Invalid repository HEAD reference" << std::endl;
        return false;
    }

    // Validate head path
    component++;
    if (component == reference.end() || *component != "heads")
    {
        std::cerr << "Invalid repository HEAD reference" << std::endl;
        return false;
    }

    // Check if a head file reference exists
    component++;
    if (component == reference.end())
    {
        std::cerr << "Invalid repository HEAD reference" << std::endl;
        return false;
    }

    // Make sure that the reference was lexically normal
    for (; component != reference.end(); ++component)
    {
        if (*component == "." || *component == "..")
        {
            std::cerr << "Invalid repository HEAD reference" << std::endl;
            return false;
        }
    }

    path = mgit::paths::repository_dir / reference;
    return true;
}
