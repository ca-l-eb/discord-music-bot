#include "voice_state_listener.h"

cmd::discord::voice_state_listener::voice_state_listener(cmd::discord::gateway *gateway,
                                                         const std::string &user_id,
                                                         const std::string &channel_id,
                                                         const std::string &guild_id)
    : gateway{gateway}, user_id{user_id}, channel_id{channel_id}, guild_id{guild_id}
{
}

void cmd::discord::voice_state_listener::handle(gtw_op_recv, const nlohmann::json &data,
                                                const std::string &type)
{
    if (type == "VOICE_STATE_UPDATE") {
        // We're looking for voice state update for this user_id, in this channel_id, for this
        // guild_id
        auto user = data["user_id"];
        if (user.is_string() && user != user_id)
            return;

        auto channel = data["channel_id"];
        if (channel.is_string() && channel != channel_id)
            return;

        auto guild = data["guild_id"];
        if (guild.is_string() && guild != guild_id)
            return;

        // This must be the session_id we want
        session_id = data["session_id"];
    }
    if (type == "VOICE_SERVER_UPDATE") {
        std::string token = data["token"];
        std::string guild_id = data["guild_id"];
        std::string endpoint = data["endpoint"];

        if (guild_id == this->guild_id && !session_id.empty()) {
        }
    }
}