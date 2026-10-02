#include <iterator>
#include <istream>
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

std::string hash(std::istream &input)
{
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return sha256(bytes);
}