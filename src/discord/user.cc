#include <discord/user.h>

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
