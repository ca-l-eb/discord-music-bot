#include "gateway.h"
#include <events/heartbeater.h>
#include <events/voice_state_listener.h>

cmd::discord::gateway::gateway(cmd::websocket::socket &sock, const std::string &token)
    : sock{sock}, token{token}
{
    // On hello opcode, spawns a thread and periodically sends a heartbeat message through *this
    // gateway. On destruction it stops the heartbeat thread and joins it
    auto beat = std::make_shared<heartbeater>(this);
    private_handlers.emplace(gtw_op_recv::hello, beat);
    private_handlers.emplace(gtw_op_recv::heartbeat_ack, beat);

    nlohmann::json identify{
        {"op", 2},
        {"d",
         {{"token", token},
          {"properties",
           {{"$os", "linux"}, {"$browser", "cmd-discord"}, {"$device", "cmd-discord"}}},
          {"compress", compress},
          {"large_threshold", large_threshold}}}};
    std::string message = identify.dump(0);
    sock.send(message);
}

cmd::discord::gateway::~gateway() {}

void cmd::discord::gateway::next_event()
{
    // Read the next message from the WebSocket and place it in buffer
    sock.next_message(buffer);
    // Parse the results as a json object
    auto json = nlohmann::json::parse(buffer.begin(), buffer.end());

    auto op = json["op"];
    auto d = json["d"];

    if (op.is_number()) {
        int op_val = op.get<int>();
        if (op_val == 0) {
            if (json["s"].is_number()) {
                seq_num = json["s"].get<int>();
            }
        }
        std::string t_val = json["t"].is_string() ? json["t"].get<std::string>() : "";

        auto gateway_op = static_cast<gtw_op_recv>(op_val);
        switch (gateway_op) {
            case gtw_op_recv::dispatch:
                // Run all public dispactch handlers
                for (auto &handler : public_handlers) {
                    handler.second->handle(gateway_op, d, t_val);
                }
                break;
            case gtw_op_recv::heartbeat:
                heartbeat();  // Respond to heartbeats with a heartbeat
                break;
            case gtw_op_recv::reconnect:
                break;
            case gtw_op_recv::invalid_session:
                break;
            case gtw_op_recv::hello:
                break;
            case gtw_op_recv::heartbeat_ack:
                break;
            default:
                throw std::runtime_error("Unknown opcode: " + std::to_string(op_val));
        }
    }
}

void cmd::discord::gateway::add_listener(const std::string &name, event_listener::ptr h)
{
    public_handlers.emplace(name, h);
}

void cmd::discord::gateway::remove_listener(const std::string &name)
{
    public_handlers.erase(name);
}

void cmd::discord::gateway::heartbeat()
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::heartbeat)}, {"d", seq_num}};
    safe_send(json.dump());
}

void cmd::discord::gateway::safe_send(const std::string &s)
{
    std::lock_guard<std::mutex> guard{write_mutex};
    // Rate limit gateway messages, allow 1 message every 0.5 seconds
    auto now = clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_msg_sent);
    if (diff.count() < 500) {
        std::this_thread::sleep_for(diff);
        last_msg_sent = now + diff;
    } else {
        last_msg_sent = now;
    }
    sock.send(s);
}

void cmd::discord::gateway::join_voice_server(const std::string &guild_id,
                                              const std::string &channel_id)
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", channel_id},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    std::string name = user_id + guild_id + channel_id + "voice_state_listener";
    add_listener(name, std::make_shared<voice_state_listener>(this, user_id, guild_id, channel_id));
    safe_send(json.dump(0));
}

void cmd::discord::gateway::leave_voice_server(const std::string &guild_id,
                                               const std::string &channel_id)
{
}
