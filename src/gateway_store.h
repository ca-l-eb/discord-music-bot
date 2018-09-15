#ifndef GATEWAY_STORE_H
#define GATEWAY_STORE_H

#include <json.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "discord.h"

namespace discord
{
class gateway_store
{
public:
    void guild_create(const nlohmann::json &json);
    void channel_create(const nlohmann::json &json);
    void channel_update(const nlohmann::json &json);
    void channel_delete(const nlohmann::json &json);
    void voice_state_update(const nlohmann::json &json);

    // Returns the guild_id that the channel is in
    discord::snowflake lookup_channel(discord::snowflake channel_id) const;
    const discord::guild *get_guild(discord::snowflake guild_id) const;

private:
    std::map<discord::snowflake, std::unique_ptr<discord::guild>>
        guilds;                                                          // guild id to guild struct
    std::map<discord::snowflake, discord::snowflake> channels_to_guild;  // channel id to guild id
    std::multimap<discord::snowflake, discord::snowflake>
        user_to_guilds;  // user id to multiple guild ids
};
}  // namespace discord

#endif
