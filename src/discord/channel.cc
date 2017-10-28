#include "channel.h"

bool cmd::discord::operator<(const channel &lhs, const channel &rhs)
{
    return lhs.id < rhs.id;
}

void cmd::discord::to_json(nlohmann::json &json, const channel &c)
{
    json = {{"name", c.name},
            {"id", c.id},
            {"type", static_cast<int>(c.type)},
            {"user_limit", c.user_limit},
            {"bitrate", c.bitrate}};
}

void cmd::discord::from_json(const nlohmann::json &json, channel &c)
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
