#include "discord.h"

static uint64_t snowflake(const std::string &s)
{
    return std::stoull(s, nullptr, 10);
}

template<typename T>
T get_safe(const nlohmann::json &json, const std::string &field, T default_val)
{
    auto find = json.find(field);
    if (find != json.end())
        return find.value();
    else
        return default_val;
}

template<>
std::string get_safe<std::string>(const nlohmann::json &json, const std::string &field,
                                  std::string default_val)
{
    auto find = json.find(field);
    if (find != json.end() && find.value().is_string())
        return find.value();
    else
        return default_val;
}

static std::string empty_string = "";
static std::string zero_string = "0";

bool discord::operator<(const discord::channel &lhs, const discord::channel &rhs)
{
    return lhs.id < rhs.id;
}

void discord::from_json(const nlohmann::json &json, discord::channel &c)
{
    assert(json.is_object());
    c.id = snowflake(json.at("id").get<std::string>());
    c.guild_id = snowflake(get_safe(json, "guild_id", zero_string));
    c.user_limit = get_safe(json, "user-limit", 0);
    c.bitrate = get_safe(json, "bitrate", 0);
    c.type = json.at("type").get<discord::channel::channel_type>();
    c.name = json.at("name").get<std::string>();
}

bool discord::operator<(const discord::guild &lhs, const discord::guild &rhs)
{
    return lhs.id < rhs.id;
}

void discord::from_json(const nlohmann::json &json, discord::guild &g)
{
    assert(json.is_object());
    g.id = snowflake(json.at("id").get<std::string>());
    g.owner = snowflake(get_safe(json, "owner_id", zero_string));
    g.name = json.at("name").get<std::string>();
    g.region = json.at("region").get<std::string>();
    g.unavailable = json.at("unavailable").get<bool>();
    g.members = json.at("members").get<std::set<discord::member>>();
    g.channels = json.at("channels").get<std::set<discord::channel>>();
}

bool discord::operator<(const discord::member &lhs, const discord::member &rhs)
{
    return lhs.user.id < rhs.user.id;
}

void discord::from_json(const nlohmann::json &json, discord::member &m)
{
    assert(json.is_object());
    m.user = json.at("user").get<discord::user>();
    m.nick = get_safe(json, "nick", empty_string);
}

bool discord::operator<(const discord::user &lhs, const discord::user &rhs)
{
    return lhs.id < rhs.id;
}

void discord::from_json(const nlohmann::json &json, discord::user &u)
{
    assert(json.is_object());
    u.id = snowflake(get_safe(json, "id", zero_string));
    u.discriminator = get_safe(json, "discriminator", empty_string);
    u.name = get_safe(json, "username", empty_string);
}

bool discord::operator<(const discord::message &lhs, const discord::message &rhs)
{
    return lhs.id < rhs.id;
}

void discord::from_json(const nlohmann::json &json, discord::message &m)
{
    assert(json.is_object());
    m.id = snowflake(json.at("id").get<std::string>());
    m.channel_id = snowflake(json.at("channel_id").get<std::string>());
    m.author = json.at("author").get<discord::user>();
    m.content = json.at("content").get<std::string>();
    m.type = json.at("type").get<discord::message::message_type>();
}

void discord::from_json(const nlohmann::json &json, discord::voice_state &v)
{
    assert(json.is_object());
    v.guild_id = snowflake(get_safe(json, "guild_id", zero_string));
    v.channel_id = snowflake(get_safe(json, "channel_id", zero_string));
    v.user_id = snowflake(json.at("user_id").get<std::string>());
    v.session_id = json.at("session_id").get<std::string>();
    v.deaf = get_safe(json, "deaf", false);
    v.mute = get_safe(json, "mute", false);
    v.self_deaf = get_safe(json, "self_deaf", false);
    v.self_mute = get_safe(json, "self_mute", false);
    v.suppress = get_safe(json, "suppress", false);
}

void discord::from_json(const nlohmann::json &json, discord::payload &p)
{
    assert(json.is_object());
    p.op = static_cast<discord::gateway_op>(json.at("op").get<int>());
    p.data = json.at("d").get<nlohmann::json>();
    if (p.op == discord::gateway_op::dispatch) {
        p.sequence_num = json.at("s").get<int>();
        p.event_name = json.at("t").get<std::string>();
    } else {
        p.sequence_num = -1;
        p.event_name = {};
    }
}

void discord::from_json(const nlohmann::json &json, discord::voice_payload &vp)
{
    assert(json.is_object());
    vp.op = static_cast<discord::voice_op>(json.at("op").get<int>());
    vp.data = json.at("d");
}

void discord::from_json(const nlohmann::json &json, discord::voice_ready &vr)
{
    assert(json.is_object());
    vr.ssrc = json.at("ssrc").get<uint32_t>();
    vr.port = json.at("port").get<uint16_t>();
}

void discord::from_json(const nlohmann::json &json, discord::voice_session &vs)
{
    assert(json.is_object());
    vs.mode = json.at("mode").get<std::string>();
    vs.secret_key = json.at("secret_key").get<std::vector<uint8_t>>();
}

void discord::event::from_json(const nlohmann::json &json, discord::event::hello &h)
{
    assert(json.is_object());
    h.heartbeat_interval = json.at("heartbeat_interval").get<int>();
}

void discord::event::from_json(const nlohmann::json &json, discord::event::ready &r)
{
    assert(json.is_object());
    r.version = json.at("v").get<int>();
    r.user = json.at("user").get<discord::user>();
    r.session_id = json.at("session_id").get<std::string>();
}

void discord::event::from_json(const nlohmann::json &json, discord::event::voice_server_update &v)
{
    v.guild_id = snowflake(json.at("guild_id").get<std::string>());
    v.token = json.at("token").get<std::string>();
    v.endpoint = json.at("endpoint").get<std::string>();
}
