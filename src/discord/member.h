#ifndef CMD_DISCORD_MEMBER_H
#define CMD_DISCORD_MEMBER_H

#include <discord/user.h>
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

bool operator<(const member &lhs, const member &rhs);
void to_json(nlohmann::json &json, const cmd::discord::member &m);
void from_json(const nlohmann::json &json, cmd::discord::member &m);

}
}

#endif
