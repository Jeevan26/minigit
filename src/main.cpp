#include <string>

//header files
#include "headers/init.hpp"

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    std::string command = argv[1];

    if (command == "init")
        init();
}
