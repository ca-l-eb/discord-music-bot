#ifndef RESOURCE_PARSER_H
#define RESOURCE_PARSER_H

#include <string>

namespace resource_parser
{
struct parsed_url {
    std::string protocol;
    std::string host;
    std::string resource;
    int port;
};
parsed_url parse(const std::string &url);
}  // namespace resource_parser

#endif
