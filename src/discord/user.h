#ifndef CMD_DISCORD_USER_H
#define CMD_DISCORD_USER_H

#include <json.hpp>

namespace discord
{
struct user {
    std::string id;
    std::string name;
    std::string discriminator;
};

bool operator<(const user &lhs, const user &rhs);
void to_json(nlohmann::json &json, const discord::user &u);
void from_json(const nlohmann::json &json, discord::user &u);
}

#endif
