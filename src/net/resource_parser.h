#ifndef CMD_RESOURCE_PARSER_H
#define CMD_RESOURCE_PARSER_H

#include <string>

namespace resource_parser{
    struct parsed_url {
        std::string protocol;
        std::string host;
        int port;
        std::string resource;
    };
    parsed_url parse(const std::string &url);
}

#endif
