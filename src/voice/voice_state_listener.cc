#include <cmd/tls_socket.h>
#include <iostream>

#include "voice_state_listener.h"

cmd::discord::voice_state_listener::voice_state_listener(const std::string &user_id,
                                                         const std::string &channel_id,
                                                         const std::string &guild_id)
    : user_id{user_id}, channel_id{channel_id}, guild_id{guild_id}
{
}

cmd::discord::voice_state_listener::~voice_state_listener()
{
    alive = false;
    if (voice_gateway_thread.joinable())
        voice_gateway_thread.join();
}

void cmd::discord::voice_state_listener::handle(cmd::discord::gateway &, gtw_op_recv,
                                                const nlohmann::json &data, const std::string &type)
{
    if (type == "VOICE_STATE_UPDATE") {
        // We're looking for voice state update for this user_id, in this channel_id, for this
        // guild_id
        auto user = data["user_id"];
        if (!(user.is_string() && user == user_id))
            return;

        auto channel = data["channel_id"];
        if (!(channel.is_string() && channel == channel_id))
            return;

        // This must be the session_id we want
        session_id = data.at("session_id").get<std::string>();
    } else if (type == "VOICE_SERVER_UPDATE") {
        if (session_id.empty())
            return;

        if (data.at("guild_id").get<std::string>() != guild_id)
            return;

        std::string token = data.at("token").get<std::string>();
        std::string endpoint = data.at("endpoint").get<std::string>();

        // We are ready to create the voice_gateway
        voice_gateway_ptr =
            std::make_unique<voice_gateway>(endpoint, user_id, session_id, guild_id, token);
        voice_gateway_thread = std::thread{&voice_state_listener::voice_event_loop, this};
        alive = true;
    }
}

void cmd::discord::voice_state_listener::voice_event_loop()
{
    int errors = 0;
    while (alive) {
        try {
            voice_gateway_ptr->next_event();
        } catch (std::exception &e) {
            std::cerr << "Voice gateway exception: " << e.what() << "\n";
            errors++;
            if (errors == 10)
                break;
        }
    }
}
