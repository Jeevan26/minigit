#include "write.hpp"
#include "status.hpp"

#include <fstream>
#include <iostream>

Status write_file_atomically(const std::filesystem::path &path, const std::string &contents)
{
    const std::filesystem::path temporary_path = path.string() + ".tmp";
    std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        std::cerr << "Failed to open temporary file for writing: " << path << std::endl;
        return Status::Failure;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if (!file)
    {
        std::filesystem::remove(temporary_path);
        std::cerr << "Failed to write file: " << path << std::endl;
        return Status::Failure;
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, path, error);
    if (error)
    {
        std::filesystem::remove(temporary_path);
        std::cerr << "Failed to replace file: " << error.message() << std::endl;
        return Status::Failure;
    }
    return Status::Success;
}
