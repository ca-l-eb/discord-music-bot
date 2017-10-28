#ifndef CMD_DISCORD_GUILD_H
#define CMD_DISCORD_GUILD_H

#include <discord/channel.h>
#include <discord/member.h>
#include <json.hpp>
#include <set>
#include <string>

namespace cmd
{
namespace discord
{
struct guild {
    std::set<member> members;
    std::set<channel> channels;
    std::string owner;
    std::string name;
    std::string id;
    std::string region;
    bool unavailable;
};

bool operator<(const guild &lhs, const guild &rhs);
void to_json(nlohmann::json &json, const cmd::discord::guild &g);
void from_json(const nlohmann::json &json, cmd::discord::guild &g);

}
}

#endif
