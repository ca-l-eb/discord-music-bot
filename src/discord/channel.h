#ifndef CMD_DISCORD_CHANNELS_H
#define CMD_DISCORD_CHANNELS_H

#include <json.hpp>
#include <string>

namespace discord
{
struct channel {
    enum class channel_type {
        guild_text = 0,
        dm = 1,
        guild_voice = 2,
        guild_dm = 3,
        guild_category = 4
    } type;
    std::string name;
    std::string id;
    int user_limit;
    int bitrate;
};

bool operator<(const channel &lhs, const channel &rhs);
void to_json(nlohmann::json &json, const channel &c);
void from_json(const nlohmann::json &json, channel &c);
}

#endif
