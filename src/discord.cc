#include <discord.h>

bool discord::operator<(const channel &lhs, const channel &rhs)
{
    return lhs.id < rhs.id;
}

void discord::to_json(nlohmann::json &json, const channel &c)
{
    json = {{"name", c.name},
            {"id", c.id},
            {"type", static_cast<int>(c.type)},
            {"user_limit", c.user_limit},
            {"bitrate", c.bitrate}};
}

void discord::from_json(const nlohmann::json &json, channel &c)
{
    c.name = json.at("name").get<std::string>();
    c.id = json.at("id").get<std::string>();
    c.type = json.at("type").get<discord::channel::channel_type>();

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

bool discord::operator<(const discord::guild &lhs, const discord::guild &rhs)
{
    return lhs.id < rhs.id;
}

void discord::to_json(nlohmann::json &json, const discord::guild &g)
{
    json = {{"owner", g.owner},
            {"name", g.name},
            {"id", g.id},
            {"members", g.members},
            {"channels", g.channels},
            {"region", g.region},
            {"unavailable", g.unavailable}};
}

void discord::from_json(const nlohmann::json &json, discord::guild &g)
{
    g.members = json.at("members").get<std::set<discord::member>>();
    g.owner = json.at("owner_id").get<std::string>();
    g.name = json.at("name").get<std::string>();
    g.id = json.at("id").get<std::string>();
    g.region = json.at("region").get<std::string>();
    g.unavailable = json.at("unavailable").get<bool>();
    g.channels = json.at("channels").get<std::set<discord::channel>>();
}

bool discord::operator<(const member &lhs, const member &rhs)
{
    return lhs.user < rhs.user;
}

void discord::to_json(nlohmann::json &json, const discord::member &m)
{
    json = {{"user", m.user}, {"nick", m.nick}, {"joined_at", m.joined_at}};
}

void discord::from_json(const nlohmann::json &json, discord::member &m)
{
    m.user = json.at("user").get<discord::user>();
    m.joined_at = json.at("joined_at").get<std::string>();
    auto nick = json.find("nick");
    if (nick != json.end() && !nick.value().is_null())
        m.nick = nick.value();
}

bool discord::operator<(const user &lhs, const user &rhs)
{
    return lhs.id < rhs.id;
}

void discord::to_json(nlohmann::json &json, const discord::user &u)
{
    json = {{"name", u.name}, {"id", u.id}, {"discriminator", u.discriminator}};
}

void discord::from_json(const nlohmann::json &json, discord::user &u)
{
    auto discriminator = json.find("discriminator");
    auto name = json.find("username");
    auto id = json.find("id");
    if (discriminator != json.end())
        u.discriminator = discriminator.value();
    if (name != json.end())
        u.name = name.value();
    if (id != json.end())
        u.id = id.value();
}
