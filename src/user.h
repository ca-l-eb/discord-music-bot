#ifndef CMD_DISCORD_USER_H
#define CMD_DISCORD_USER_H

#include <json.hpp>

namespace cmd
{
namespace discord
{
struct user {
    std::string id;
    std::string name;
    std::string discriminator;
};

bool operator<(const user &lhs, const user &rhs)
{
    return lhs.id < rhs.id;
}

void to_json(nlohmann::json &json, const cmd::discord::user &u)
{
    json = {{"name", u.name}, {"id", u.id}, {"discriminator", u.discriminator}};
}

void from_json(const nlohmann::json &json, cmd::discord::user &u)
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
}
}

#endif
