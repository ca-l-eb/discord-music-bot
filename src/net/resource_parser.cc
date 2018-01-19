#include <regex>

#include <net/resource_parser.h>

resource_parser::parsed_url resource_parser::parse(const std::string &url)
{
    static std::regex re{
        R"(^(?:(\S+)://)?([A-Za-z0-9.-]{2,})(?::(\d+))?(/[/A-Za-z0-9-._~:/?#\[\]%@!$&'()*+,;=`]*)?$)"};
    std::smatch matcher;
    std::regex_match(url, matcher, re);

    std::string proto;
    std::string host;
    std::string resource;
    int port = -1;
    if (matcher.empty())
        return {"", "", -1, ""};

    proto = matcher.str(1);
    host = matcher.str(2);
    if (!matcher.str(3).empty()) {
        port = std::stoi(matcher.str(3));
    } else {
        if (proto == "http" || proto == "ws")
            port = 80;
        else if (proto == "https" || proto == "wss")
            port = 443;
    }
    if (!matcher.str(4).empty())
        resource = matcher.str(4);
    else
        resource = "/";
    return {proto, host, port, resource};
}
