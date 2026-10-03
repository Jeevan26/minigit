#include <iostream>
#include <filesystem>
#include <fstream>

#include "config.hpp"

namespace fs = std::filesystem;

namespace
{
    std::string trim(const std::string &str)
    {
        const auto first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos)
            return "";

        const auto last = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(first, last - first + 1);
    }

    bool read_user_config(std::ifstream &config_file, std::string &name, std::string &email)
    {
        std::string line;
        bool user_flag = false;

        while (getline(config_file, line))
        {
            if (user_flag)
            {
                const auto separator = line.find('=');
                if (separator == std::string::npos)
                    continue;

                std::string config_type = trim(line.substr(0, separator));
                std::string config_detail = trim(line.substr(separator + 1));

                if (config_type == "name")
                    name = config_detail;
                else if (config_type == "email")
                {
                    email = config_detail;
                    user_flag = false;
                }
            }
            if (line == "[user]")
                user_flag = true;
        }

        return !config_file.bad() && (config_file.eof() || !config_file.fail());
    }
}

bool config(std::string &cfg, std::string &arg)
{
    if (cfg == "" || arg == "")
    {
        std::cerr << "Please provide sufficient arguments" << std::endl;
        return false;
    }

    if (!fs::exists(".mgit"))
    {
        std::cerr << "No repo initialized in current directory!\nTry running \"mgit init\" first" << std::endl;
        return false;
    }

    fs::path config_path = ".mgit/config";
    if (!fs::exists(config_path))
    {
        std::cerr << "Malformed repository found" << std::endl;
        return false;
    }

    std::ifstream config_file(config_path);
    if (!config_file)
    {
        std::cerr << "Failed to open config file" << std::endl;
        return false;
    }

    if (cfg != "name" && cfg != "email")
    {
        std::cerr << "Config setting not found" << std::endl;
        return false;
    }

    // Fetch previous name and email if they exist
    std::string name = "", email = "";
    if (!read_user_config(config_file, name, email))
    {
        std::cerr << "Failed to read config file" << std::endl;
        return false;
    }

    if (cfg == "name")
        name = arg;
    else if (cfg == "email")
        email = arg;

    try
    {
        // Overwrite the file with new data
        std::ofstream modified_config(config_path, std::ios::trunc);
        if (!modified_config.is_open())
        {
            std::cerr << "Failed to open config file for writing" << std::endl;
            return false;
        }

        modified_config << "[core]" << std::endl;
        modified_config << "\trepositoryformatversion = 1" << std::endl;
        modified_config << "\tfilemode = true" << std::endl;
        modified_config << "\tbare = false" << std::endl;
        modified_config << "[extensions]" << std::endl;
        modified_config << "\tobjectFormat = sha256" << std::endl;
        modified_config << "[user]" << std::endl;
        modified_config << "\tname = " << name << std::endl;
        modified_config << "\temail = " << email << std::endl;
        modified_config.close();

        if (!modified_config)
        {
            std::cerr << "Failed to write config file" << std::endl;
            return false;
        }

        return true;
    }
    catch (fs::filesystem_error err)
    {
        std::cerr << "An error occured whilst trying to write to file: " << err.what() << std::endl;
        return false;
    }
}