//
// Created by dechant on 10/21/17.
//

#ifndef ASIO_BASE64_H
#define ASIO_BASE64_H

#include <cstddef>
#include <string>
#include <vector>

namespace base64
{
std::string encode(const void *message, size_t size);
std::string encode(const std::string &message);
std::vector<unsigned char> decode(const char *message, size_t size);
std::vector<unsigned char> decode(const std::string &message);
}  // namespace base64

#endif  // ASIO_BASE64_H
