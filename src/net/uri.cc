#include <regex>

#include "net/uri.h"

uri::parsed_uri uri::parse(const std::string &uri)
{
    static const auto re = std::regex{
        R"(^(?:(\S+)://)?([A-Za-z0-9.-]{2,})(?::(\d+))?(/[/A-Za-z0-9-._~:/?#\[\]%@!$&'()*+,;=`]*)?$)"};
    auto matcher = std::smatch{};
    auto path = std::string{};
    auto port = -1;

    std::regex_match(uri, matcher, re);
    if (matcher.empty())
        return {"", "", "", -1};

    auto scheme = matcher.str(1);
    auto authority = matcher.str(2);
    if (!matcher.str(3).empty()) {
        port = std::stoi(matcher.str(3));
    } else {
        if (scheme == "http" || scheme == "ws")
            port = 80;
        else if (scheme == "https" || scheme == "wss")
            port = 443;
    }
    if (!matcher.str(4).empty())
        path = matcher.str(4);
    else
        path = "/";
    return {scheme, authority, path, port};
}
