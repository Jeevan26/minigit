#include <string>
#include <vector>

#include "hash.hpp"
#include "sha.hpp"

std::string hash(const std::vector<u_int8_t> &data)
{
    const std::string bytes(
        reinterpret_cast<const char *>(data.data()),
        data.size());

    return sha256(bytes);
}