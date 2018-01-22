#include <gateway_store.h>

void discord::gateway_store::parse_guild(const nlohmann::json &json)
{
    try {
        discord::guild g = json;

        for (auto &channel : g.channels)
            channels_to_guild[channel.id] = g.id;
        
        for (auto &member : g.members)
            user_to_guilds.insert({member.user.id, g.id});

        if (!guilds[g.id]) {
            guilds[g.id] = std::make_unique<discord::guild>();
            *guilds[g.id] = std::move(g);
        } else {
            // TODO: update fields in existing guild
        }
    } catch (std::exception &e) {
        // Do nothing if we get an exception
    }
}

discord::guild *discord::gateway_store::get_guild(uint64_t guild_id)
{
    auto it = guilds.find(guild_id);
    if (it != guilds.end())
        return it->second.get();
    return nullptr;
}

uint64_t discord::gateway_store::lookup_channel(uint64_t channel_id)
{
    auto it = channels_to_guild.find(channel_id);
    if (it == channels_to_guild.end())
        return 0;
    return it->second;
}
