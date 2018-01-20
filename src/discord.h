#ifndef CMD_DISCORD_H
#define CMD_DISCORD_H

#include <json.hpp>
#include <set>
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

struct user {
    std::string id;
    std::string name;
    std::string discriminator;
};

struct member {
    discord::user user;
    std::string nick;
    std::string joined_at;
};

struct guild {
    std::set<member> members;
    std::set<channel> channels;
    std::string owner;
    std::string name;
    std::string id;
    std::string region;
    bool unavailable;
};

bool operator<(const channel &lhs, const channel &rhs);
void to_json(nlohmann::json &json, const channel &c);
void from_json(const nlohmann::json &json, channel &c);

bool operator<(const user &lhs, const user &rhs);
void to_json(nlohmann::json &json, const discord::user &u);
void from_json(const nlohmann::json &json, discord::user &u);

bool operator<(const member &lhs, const member &rhs);
void to_json(nlohmann::json &json, const discord::member &m);
void from_json(const nlohmann::json &json, discord::member &m);

bool operator<(const guild &lhs, const guild &rhs);
void to_json(nlohmann::json &json, const discord::guild &g);
void from_json(const nlohmann::json &json, discord::guild &g);

}

#endif
