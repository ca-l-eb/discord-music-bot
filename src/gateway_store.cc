#include "gateway_store.h"

void cmd::discord::gateway_store::parse_guild(const nlohmann::json &json) {
    try {
        cmd::discord::guild g = json;
        
        // Add all channels to channel_to_guild map
        for (auto &channel : g.channels)
            channels_to_guild[channel.id] = g.id;
        
        for (auto &member: g.members)
            user_to_guilds.insert(std::pair<std::string, std::string>(member.user.id, g.id));
        
        guilds.insert(std::move(g));
    } catch (std::exception &e) {
        // Do nothing if we get an exception
    }
}

const std::string cmd::discord::gateway_store::lookup_channel(const std::string &channel_id)
{
    auto it = channels_to_guild.find(channel_id);
    if (it == channels_to_guild.end())
        return "";
    return it->second;
}
