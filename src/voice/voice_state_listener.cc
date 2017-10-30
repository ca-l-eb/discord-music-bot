#include <string_utils.h>
#include <regex>

#include <voice/voice_state_listener.h>

cmd::discord::voice_state_listener::voice_state_listener(boost::asio::io_service &service,
                                                         cmd::discord::gateway &gateway,
                                                         cmd::discord::gateway_store &store)
    : service{service}, gateway{gateway}, store{store}
{
}

cmd::discord::voice_state_listener::~voice_state_listener() {}

void cmd::discord::voice_state_listener::handle(cmd::discord::gateway &, gtw_op_recv,
                                                const nlohmann::json &data, const std::string &type)
{
    if (type == "VOICE_STATE_UPDATE") {
        voice_state_update(data);
    } else if (type == "VOICE_SERVER_UPDATE") {
        voice_server_update(data);
    } else if (type == "MESSAGE_CREATE") {
        message_create(data);
    }
}

void cmd::discord::voice_state_listener::voice_state_update(const nlohmann::json &data)
{
    // We're looking for voice state update for this user_id
    auto user = data["user_id"];
    if (!(user.is_string() && user == gateway.get_user_id()))
        return;

    auto guild = data["guild_id"];
    if (!guild.is_string())
        return;

    std::string guild_id = guild.get<std::string>();

    // Create the entry if there is not one for this guild already
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        voice_gateways[guild_id] = {};

    // Get a reference to the entry fot his guild
    auto &vg = voice_gateways[guild_id];
    vg.guild_id = guild_id;

    auto session = data["session_id"];
    if (session.is_string())
        vg.session_id = session.get<std::string>();

    auto channel = data["channel_id"];
    if (channel.is_string())
        vg.channel_id = channel.get<std::string>();
}

void cmd::discord::voice_state_listener::voice_server_update(const nlohmann::json &data)
{
    auto guild = data["guild_id"];
    if (!guild.is_string())
        return;
    std::string guild_id = guild.get<std::string>();

    // Lookup the voice_gateway_entry from the map, if it doesn't exist, or doesn't contain a
    // session_id and channel_id, don't continue
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &vg = it->second;
    if (vg.session_id.empty() || vg.channel_id.empty())
        return;

    vg.token = data.at("token").get<std::string>();
    vg.endpoint = data.at("endpoint").get<std::string>();

    // We got all the information needed to join a voice gateway now
    vg.gateway = std::make_unique<cmd::discord::voice_gateway>(service, vg, gateway.get_user_id());
}

void cmd::discord::voice_state_listener::message_create(const nlohmann::json &data)
{
    // If this isn't a guild text, ignore the message
    auto msg_type = data["type"];
    if (!msg_type.is_number())
        return;
    if (msg_type.get<int>() != static_cast<int>(cmd::discord::channel::channel_type::guild_text))
        return;

    // Otherwise lookup the message contents and check if it is a command to change the voice
    // state
    auto msg = data["content"];
    if (!msg.is_string())
        return;
    std::string msg_str = msg.get<std::string>();
    if (msg_str.empty())
        return;

    if (msg_str[0] == ':')
        check_command(msg_str, data);
}

void cmd::discord::voice_state_listener::check_command(const std::string &content,
                                                       const nlohmann::json &json)
{
    static std::regex command_re{R"(^:(\S+)(?:\s+(.+))?$)"};
    std::smatch matcher;
    std::regex_search(content, matcher, command_re);

    if (matcher.empty())
        return;

    std::string command = cmd::string_utils::to_lower(matcher.str(1));
    std::string params = matcher.str(2);

    // TODO: consider making this a lookup table setup in constructor
    if (command == "join")
        do_join(params, json);
    else if (command == "leave")
        do_leave(json);
    else if (command == "list" || command == "l")
        do_list(json);
    else if (command == "add" || command == "a")
        do_add(params, json);
    else if (command == "skip" || command == "next")
        do_skip(json);
}

void cmd::discord::voice_state_listener::do_join(const std::string &params,
                                                 const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    // Look up what guild this channel is in
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;  // We couldn't find the guild for this channel

    // Look through the guild's channels for channel <params>, if it exists, join, else fail
    // silently
    auto guild = store.get_guild(guild_id);
    if (guild.id != guild_id)
        return;

    for (auto &channel : guild.channels) {
        if (channel.type == cmd::discord::channel::channel_type::guild_voice &&
            channel.name == params) {
            // Found a matching voice channel name! Join it
            join_voice_server(guild.id, channel.id.c_str());
            return;
        }
    }
}

void cmd::discord::voice_state_listener::do_leave(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    // Look up what guild this channel is in
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;  // We couldn't find the guild for this channel

    // If we are currently connected to a voice channel in this guild, disconnect
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    if (it->second.gateway) {
        it->second.gateway = nullptr;
        leave_voice_server(guild_id);
    }
}

void cmd::discord::voice_state_listener::do_list(const nlohmann::json &json) {}
void cmd::discord::voice_state_listener::do_add(const std::string &params,
                                                const nlohmann::json &json)
{
}

void cmd::discord::voice_state_listener::do_skip(const nlohmann::json &json) {}

// Join and leave can't be refactored because the nlohmann::json converts char*s
// into strings before serializing the json, I guess. Doesn't like using nullptrs with char*
void cmd::discord::voice_state_listener::join_voice_server(const std::string &guild_id,
                                                           const std::string &channel_id)
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", channel_id},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), [](const boost::system::error_code &, size_t) {});
}

void cmd::discord::voice_state_listener::leave_voice_server(const std::string &guild_id)
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", nullptr},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), [](const boost::system::error_code &, size_t) {});
}
