#include <iostream>

#include "gateway_store.h"

void discord::gateway_store::guild_create(const nlohmann::json &json)
{
    try {
        discord::guild g = json;

        for (auto &channel : g.channels)
            channels_to_guild[channel.id] = g.id;

        for (auto &member : g.members)
            user_to_guilds.insert({member.user.id, g.id});

        guilds[g.id] = std::make_unique<discord::guild>(std::move(g));
    } catch (std::exception &e) {
        std::cerr << "[gateway store] " << e.what() << "\n";
    }
}

void discord::gateway_store::channel_create(const nlohmann::json &json)
{
    try {
        discord::channel c = json;
        channels_to_guild[c.id] = c.guild_id;
        auto g = guilds[c.guild_id].get();
        if (g) {
            g->channels.insert(c);
        }
    } catch (std::exception &e) {
        std::cerr << "[gateway store] " << e.what() << "\n";
    }
}

void discord::gateway_store::channel_update(const nlohmann::json &json)
{
    try {
        discord::channel c = json;
        auto g = guilds[c.guild_id].get();
        if (g) {
            // erase old entry, replace with new channel
            g->channels.erase(c);
            g->channels.insert(c);
        }
    } catch (std::exception &e) {
        std::cerr << "[gateway store] " << e.what() << "\n";
    }
}

void discord::gateway_store::channel_delete(const nlohmann::json &json)
{
    try {
        discord::channel c = json;
        auto g = guilds[c.guild_id].get();
        if (g) {
            g->channels.erase(c);
        }
        channels_to_guild.erase(c.id);
    } catch (std::exception &e) {
        std::cerr << "[gateway store] " << e.what() << "\n";
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
