#ifndef NET_URI_H
#define NET_URI_H

#include <string>

namespace uri
{
struct parsed_uri {
    std::string scheme;
    std::string authority;
    std::string path;
    int port;
};

parsed_uri parse(const std::string &uri);

}  // namespace uri

#endif
