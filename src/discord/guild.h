#ifndef CMD_DISCORD_GUILD_H
#define CMD_DISCORD_GUILD_H

#include <json.hpp>
#include <set>
#include <string>

#include <discord/channel.h>
#include <discord/member.h>

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
void to_json(nlohmann::json &json, const discord::guild &g);
void from_json(const nlohmann::json &json, discord::guild &g);
}

#endif
