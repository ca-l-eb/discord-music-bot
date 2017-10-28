#ifndef GATEWAY_STORE_H
#define GATEWAY_STORE_H

#include <discord/guild.h>
#include <json.hpp>
#include <map>
#include <set>
#include <string>

namespace cmd
{
namespace discord
{
class gateway_store
{
public:
    void parse_guild(const nlohmann::json &json);
    
    // Returns the guild_id that the channel is in
    const std::string lookup_channel(const std::string &channel_id);

private:
    std::set<guild> guilds;
    std::map<std::string, std::string> channels_to_guild;
    std::multimap<std::string, std::string> user_to_guilds;
};
}
}

#endif
