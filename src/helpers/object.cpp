#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "hash.hpp"
#include "object.hpp"
#include "paths.hpp"
#include "status.hpp"

namespace
{
    namespace fs = std::filesystem;
    constexpr std::size_t hash_bytes = 32;

    fs::path object_path(const std::string &object_hash)
    {
        return mgit::paths::objects_dir / object_hash.substr(0, 2) / object_hash.substr(2);
    }

    Status decode_hash(const std::string &hex, std::string &bytes)
    {
        if (!object_store::valid_hash(hex))
            return Status::Failure;

        bytes.clear();
        bytes.reserve(hash_bytes);
        for (std::size_t i = 0; i < hex.size(); i += 2)
        {
            const auto digit = [](char c) -> unsigned char
            {
                if (c >= '0' && c <= '9')
                    return static_cast<unsigned char>(c - '0');
                return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
            };
            bytes.push_back(static_cast<char>((digit(hex[i]) << 4) | digit(hex[i + 1])));
        }
        return Status::Success;
    }

    std::string encode_hash(const std::string &bytes)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(bytes.size() * 2);
        for (unsigned char byte : bytes)
        {
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 0x0f]);
        }
        return hex;
    }

    Status write_object_file(const fs::path &path, const std::string &data)
    {
        std::error_code directory_error;
        fs::create_directories(path.parent_path(), directory_error);
        if (directory_error)
        {
            std::cerr << "Failed to create Git object directory: " << directory_error.message() << std::endl;
            return Status::Failure;
        }

        const fs::path temporary_path = path.string() + ".tmp";
        std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            std::cerr << "Failed to open temporary Git object" << std::endl;
            return Status::Failure;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file)
        {
            fs::remove(temporary_path);
            std::cerr << "Failed to write Git object" << std::endl;
            return Status::Failure;
        }

        std::error_code error;
        fs::rename(temporary_path, path, error);
        if (error && fs::exists(path))
        {
            fs::remove(temporary_path);
            return Status::Success;
        }
        if (error)
        {
            fs::remove(temporary_path);
            std::cerr << "Failed to store Git object: " << error.message() << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }
}

namespace object_store
{
    bool valid_hash(const std::string &hash)
    {
        return hash.size() == hash_bytes * 2 &&
               std::all_of(hash.begin(), hash.end(), [](unsigned char c)
                           { return std::isxdigit(c) != 0; });
    }

    Status write_object(const std::string &type, const std::string &contents, std::string &object_hash)
    {
        if (type != "blob" && type != "tree" && type != "commit")
        {
            std::cerr << "Unsupported Git object type" << std::endl;
            return Status::Failure;
        }

        const std::string header = type + " " + std::to_string(contents.size()) + '\0';
        const std::string serialized = header + contents;
        object_hash = hash(serialized);

        std::error_code error;
        fs::create_directories(mgit::paths::objects_dir, error);
        if (error)
        {
            std::cerr << "Failed to create objects directory: " << error.message() << std::endl;
            return Status::Failure;
        }
        return write_object_file(object_path(object_hash), serialized);
    }

    Status read_object(const std::string &object_hash, const std::string &expected_type, std::string &contents)
    {
        if (!valid_hash(object_hash))
            return Status::Failure;

        std::ifstream file(object_path(object_hash), std::ios::binary);
        if (!file)
            return Status::Failure;
        const std::string serialized((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (file.bad())
        {
            std::cerr << "Failed to read Git object" << std::endl;
            return Status::Failure;
        }

        const auto separator = serialized.find('\0');
        if (separator == std::string::npos ||
            hash(serialized) != object_hash)
        {
            std::cerr << "Git object hash verification failed" << std::endl;
            return Status::Failure;
        }

        const std::string header = serialized.substr(0, separator);
        const auto space = header.find(' ');
        if (space == std::string::npos || header.substr(0, space) != expected_type)
            return Status::Failure;

        std::size_t declared_size = 0;
        try
        {
            const std::string size_text = header.substr(space + 1);
            std::size_t parsed = 0;
            declared_size = std::stoull(size_text, &parsed);
            if (parsed != size_text.size())
                return Status::Failure;
        }
        catch (const std::exception &)
        {
            return Status::Failure;
        }

        contents = serialized.substr(separator + 1);
        if (declared_size != contents.size())
        {
            std::cerr << "Git object size does not match its header" << std::endl;
            return Status::Failure;
        }
        return Status::Success;
    }

    Status write_tree(const std::vector<TreeEntry> &entries, std::string &tree_hash)
    {
        std::string contents = "MGTR";
        contents.push_back('\1');
        for (const auto &entry : entries)
        {
            std::string raw_hash;
            if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
                entry.name.find('/') != std::string::npos ||
                entry.name.find('\0') != std::string::npos || decode_hash(entry.hash, raw_hash) != Status::Success)
            {
                std::cerr << "Invalid tree entry" << std::endl;
                return Status::Failure;
            }
            contents.push_back(entry.is_directory ? 'D' : 'F');
            contents += entry.name + '\0' + raw_hash;
        }
        return write_object("tree", contents, tree_hash);
    }

    Status read_tree(const std::string &tree_hash, std::vector<TreeEntry> &entries)
    {
        std::string contents;
        if (read_object(tree_hash, "tree", contents) != Status::Success)
            return Status::Failure;

        entries.clear();
        if (contents.size() < 5 || contents.compare(0, 4, "MGTR") != 0 || contents[4] != '\1')
        {
            std::cerr << "Unsupported tree object format" << std::endl;
            return Status::Failure;
        }

        std::size_t offset = 5;
        while (offset < contents.size())
        {
            const char kind = contents[offset];
            const auto nul = contents.find('\0', offset + 1);
            if ((kind != 'F' && kind != 'D') || nul == std::string::npos ||
                contents.size() - nul - 1 < hash_bytes)
            {
                std::cerr << "Malformed tree object" << std::endl;
                return Status::Failure;
            }

            TreeEntry entry;
            entry.is_directory = kind == 'D';
            entry.name = contents.substr(offset + 1, nul - offset - 1);
            entry.hash = encode_hash(contents.substr(nul + 1, hash_bytes));
            if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
                entry.name.find('/') != std::string::npos)
            {
                std::cerr << "Malformed tree entry" << std::endl;
                return Status::Failure;
            }
            entries.push_back(std::move(entry));
            offset = nul + 1 + hash_bytes;
        }
        return Status::Success;
    }
}
