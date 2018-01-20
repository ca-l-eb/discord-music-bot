#ifndef GATEWAY_STORE_H
#define GATEWAY_STORE_H

#include <json.hpp>
#include <map>
#include <set>
#include <string>

#include <discord.h>

namespace discord
{
class gateway_store
{
public:
    void parse_guild(const nlohmann::json &json);

    // Returns the guild_id that the channel is in
    const std::string lookup_channel(const std::string &channel_id);
    discord::guild get_guild(const std::string &guild_id);

private:
    std::map<std::string, guild> guilds;
    std::map<std::string, std::string> channels_to_guild;
    std::multimap<std::string, std::string> user_to_guilds;
};
}

#endif
