#include <discord/guild.h>

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
