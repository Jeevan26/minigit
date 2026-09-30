#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

#include "sha.hpp"

std::string sha256(const std::string &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), hash);

    std::stringstream result;

    for (unsigned char byte : hash)
        result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);

    return result.str();
}