#include "guild.h"

void cmd::discord::to_json(nlohmann::json &json, const cmd::discord::guild &g)
{
    json = {{"owner", g.owner},
            {"name", g.name},
            {"id", g.id},
            {"members", g.members},
            {"channels", g.channels},
            {"region", g.region},
            {"unavailable", g.unavailable}};
}

void cmd::discord::from_json(const nlohmann::json &json, cmd::discord::guild &g)
{
    g.members = json.at("members").get<std::set<cmd::discord::member>>();
    g.owner = json.at("owner_id").get<std::string>();
    g.name = json.at("name").get<std::string>();
    g.id = json.at("id").get<std::string>();
    g.region = json.at("region").get<std::string>();
    g.unavailable = json.at("unavailable").get<bool>();
    g.channels = json.at("channels").get<std::vector<cmd::discord::channel>>();
}
