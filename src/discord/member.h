#ifndef CMD_DISCORD_MEMBER_H
#define CMD_DISCORD_MEMBER_H

#include <user.h>
#include <json.hpp>
#include <string>

namespace cmd
{
namespace discord
{
struct member {
    cmd::discord::user user;
    std::string nick;
    std::string joined_at;
};

bool operator<(const member &lhs, const member &rhs)
{
    return lhs.user < rhs.user;
}

void to_json(nlohmann::json &json, const cmd::discord::member &m)
{
    json = {{"user", m.user}, {"nick", m.nick}, {"joined_at", m.joined_at}};
}

void from_json(const nlohmann::json &json, cmd::discord::member &m)
{
    m.user = json.at("user").get<cmd::discord::user>();
    m.joined_at = json.at("joined_at").get<std::string>();
    auto nick = json.find("nick");
    if (nick != json.end() && !nick.value().is_null())
        m.nick = nick.value();
}
}
}

#endif
