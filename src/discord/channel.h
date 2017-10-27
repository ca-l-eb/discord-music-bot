#ifndef CMD_DISCORD_CHANNELS_H
#define CMD_DISCORD_CHANNELS_H

#include <json.hpp>
#include <string>

namespace cmd
{
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

void to_json(nlohmann::json &json, const channel &c)
{
    json = {{"name", c.name},
            {"id", c.id},
            {"type", static_cast<int>(c.type)},
            {"user_limit", c.user_limit},
            {"bitrate", c.bitrate}};
}

void from_json(const nlohmann::json &json, channel &c)
{
    c.name = json.at("name").get<std::string>();
    c.id = json.at("id").get<std::string>();
    c.type = json.at("type").get<cmd::discord::channel::channel_type>();

    auto user_limit = json.find("user_limit");
    if (user_limit != json.end())
        c.user_limit = user_limit.value();
    else
        c.user_limit = 0;

    auto bitrate = json.find("bitrate");
    if (bitrate != json.end())
        c.bitrate = bitrate.value();
    else
        c.bitrate = 0;
}
}
}
#endif
