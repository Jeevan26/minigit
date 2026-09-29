#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void init()
{
    fs::create_directories(".mgit/directories");
    fs::create_directories(".mgit/refs/heads");
    fs::create_directories(".mgit/refs/tags");

    std::cout << "Initialized an empty repository" << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    std::string command = argv[1];

    if (command == "init")
        init();
}
