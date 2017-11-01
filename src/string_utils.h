#ifndef CMDSOCK_STRING_UTILS_H
#define CMDSOCK_STRING_UTILS_H

#include <string>

namespace cmd
{
namespace string_utils
{
inline std::string to_lower(const std::string &s)
{
    std::string new_s{s};
    for (int i = 0; i < s.size(); i++)
        new_s[i] = static_cast<char>(std::tolower(s[i]));
    return new_s;
}
}
}

#endif  // CMDSOCK_STRING_UTILS_H
