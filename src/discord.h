#ifndef CMD_DISCORD_H
#define CMD_DISCORD_H

#include <set>
#include <string>

#include <json.hpp>

namespace discord
{
enum class gateway_op {
    dispatch = 0,
    heartbeat = 1,
    identify = 2,
    status_update = 3,
    voice_state_update = 4,
    voice_server_ping = 5,
    resume = 6,
    reconnect = 7,
    request_guild_members = 8,
    invalid_session = 9,
    hello = 10,
    heartbeat_ack = 11
};

enum class voice_op {
    identify = 0,
    select_proto = 1,
    ready = 2,
    heartbeat = 3,
    session_description = 4,
    speaking = 5,
    heartbeat_ack = 6,
    resume = 7,
    hello = 8,
    resumed = 9,
    client_disconnect = 13
};

struct channel {
    uint64_t id;
    uint64_t guild_id;
    int user_limit;
    int bitrate;
    enum class channel_type {
        guild_text = 0,
        dm = 1,
        guild_voice = 2,
        guild_dm = 3,
        guild_category = 4
    } type;
    std::string name;
    channel() = default;
};

struct user {
    uint64_t id;
    std::string name;
    std::string discriminator;
    user() = default;
};

struct member {
    discord::user user;
    std::string nick;
    member() = default;
};

struct guild {
    uint64_t id;
    uint64_t owner;
    std::set<channel> channels;
    std::set<member> members;
    std::string name;
    std::string region;
    bool unavailable;
    guild() = default;
};

struct message {
    uint64_t id;
    uint64_t channel_id;
    discord::user author;
    std::string content;
    enum class message_type {
        default_ = 0,
        recipient_add = 1,
        recipient_remove = 2,
        call = 3,
        channel_name_change = 4,
        channel_icon_change = 5,
        channel_pinned_message = 6,
        guild_member_join = 7
    } type;
    message() = default;
};

struct voice_state {
    uint64_t guild_id;  // server_id in docs
    uint64_t channel_id;
    uint64_t user_id;
    std::string session_id;
    bool deaf;
    bool mute;
    bool self_deaf;
    bool self_mute;
    bool suppress;
    voice_state() = default;
};

struct payload {
    discord::gateway_op op;
    int sequence_num;
    std::string event_name;
    nlohmann::json data;
};

struct voice_payload {
    discord::voice_op op;
    nlohmann::json data;
};

struct voice_ready {
    uint32_t ssrc;
    uint16_t port;
};

struct voice_session {
    std::string mode;
    std::vector<uint8_t> secret_key;
};

bool operator<(const discord::channel &lhs, const discord::channel &rhs);
bool operator<(const discord::user &lhs, const discord::user &rhs);
bool operator<(const discord::member &lhs, const discord::member &rhs);
bool operator<(const discord::guild &lhs, const discord::guild &rhs);
bool operator<(const discord::message &lhs, const discord::message &rhs);

void from_json(const nlohmann::json &json, discord::channel &c);
void from_json(const nlohmann::json &json, discord::user &u);
void from_json(const nlohmann::json &json, discord::member &m);
void from_json(const nlohmann::json &json, discord::guild &g);
void from_json(const nlohmann::json &json, discord::message &m);
void from_json(const nlohmann::json &json, discord::voice_state &v);
void from_json(const nlohmann::json &json, discord::payload &p);
void from_json(const nlohmann::json &json, discord::voice_payload &vp);
void from_json(const nlohmann::json &json, discord::voice_ready &vr);
void from_json(const nlohmann::json &json, discord::voice_session &vs);

namespace event
{
struct hello {
    int heartbeat_interval;
};

struct ready {
    int version;
    discord::user user;
    std::string session_id;
};

struct voice_server_update {
    uint64_t guild_id;
    std::string token;
    std::string endpoint;
};

void from_json(const nlohmann::json &json, discord::event::hello &h);
void from_json(const nlohmann::json &json, discord::event::ready &r);
void from_json(const nlohmann::json &json, discord::event::voice_server_update &v);
}
}

#endif
