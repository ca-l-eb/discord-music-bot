#ifndef GATEWAY_STORE_H
#define GATEWAY_STORE_H

#include <json.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <discord.h>

namespace discord
{
class gateway_store
{
public:
    void guild_create(const nlohmann::json &json);
    void channel_create(const nlohmann::json &json);
    void channel_update(const nlohmann::json &json);
    void channel_delete(const nlohmann::json &json);

    // Returns the guild_id that the channel is in
    uint64_t lookup_channel(uint64_t channel_id);
    discord::guild *get_guild(uint64_t guild_id);

private:
    std::map<uint64_t, std::unique_ptr<discord::guild>> guilds;  // guild id to guild struct
    std::map<uint64_t, uint64_t> channels_to_guild;              // channel id to guild id
    std::multimap<uint64_t, uint64_t> user_to_guilds;            // user id to multiple guild ids
};
}

#endif
