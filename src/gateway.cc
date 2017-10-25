#include <events/event_listener.h>
#include <events/voice_state_listener.h>
#include <heartbeater.h>
#include <iostream>

#include "gateway.h"

cmd::discord::gateway::gateway(const std::string &token)
    : websocket{}, beater{}, token{token}, state{connection_state::disconnected}
{
    gateway_event_map.emplace("READY", [&](nlohmann::json &data) -> void {
        auto version = data["v"];
        if (version.is_number()) {
            if (version.get<int>() != 6)
                throw std::runtime_error(
                    "Unsupported gateway protocol version: " + std::to_string(version.get<int>()) +
                    ". Support is limited to v6");
        } else {
            throw std::runtime_error("Expected version number in READY event");
        }

        // We got the READY event, we are now connected
        state = connection_state::connected;

        auto user_obj = data["user"];
        if (user_obj.is_object()) {
            auto id = user_obj["id"];
            if (id.is_string()) {
                user_id = id.get<std::string>();
            }
        }
        auto session = data["session_id"];
        if (session.is_string())
            session_id = session.get<std::string>();
    });

    gateway_event_map.emplace(
        "RESUME", [&](nlohmann::json &) -> void { state = connection_state::connected; });

    // Establish the WebSocket connection
    websocket.connect("wss://gateway.discord.gg/?v=6&encoding=json");
}

cmd::discord::gateway::~gateway() {}

void cmd::discord::gateway::next_event()
{
    // Read the next message from the WebSocket and place it in buffer
    auto read = websocket.next_message(buffer);
    if (read == 0) {
        throw std::runtime_error("WebSocket connection interrupted");
    }
    std::cout << "GATEWAY: ";
    std::cout.write((char *) buffer.data(), read);
    std::cout << "\n";

    // Parse the results as a json object
    auto json = nlohmann::json::parse(buffer.begin(), buffer.end());

    auto op = json["op"];
    auto event_data = json["d"];

    if (op.is_number()) {
        int op_val = op.get<int>();
        if (op_val == 0) {
            if (json["s"].is_number()) {
                seq_num = json["s"].get<int>();
            }
        }
        std::string event_name = json["t"].is_string() ? json["t"].get<std::string>() : "";

        auto gateway_op = static_cast<gtw_op_recv>(op_val);
        switch (gateway_op) {
            case gtw_op_recv::dispatch:
                run_public_dispatch(gateway_op, event_data, event_name);
                run_gateway_dispatch(event_data, event_name);
                break;
            case gtw_op_recv::heartbeat:
                heartbeat();  // Respond to heartbeats with a heartbeat
                break;
            case gtw_op_recv::reconnect:
                // We are asked to reconnect, i.e. we are disconnected
                state = connection_state::disconnected;
                resume();
                break;
            case gtw_op_recv::invalid_session:
                if (state == connection_state::disconnected) {
                    throw std::runtime_error("Could not connect to Discord Gateway");
                } else {
                    state = connection_state::disconnected;
                    // Already connected, if we can reconnect, try to resume the connection
                    if (event_data.is_boolean() && event_data.get<bool>())
                        resume();
                    else
                        throw std::runtime_error("");
                }
                break;
            case gtw_op_recv::hello:
                beater.on_hello(*this, event_data);
                break;
            case gtw_op_recv::heartbeat_ack:
                beater.on_heartbeat_ack();
                break;
            default:
                throw std::runtime_error("Unknown opcode: " + std::to_string(op_val));
        }
    }
}

void cmd::discord::gateway::add_listener(const std::string &event_name,
                                         const std::string &handler_name, event_listener::ptr h)
{
    event_name_to_handler_name.emplace(event_name, handler_name);
    handler_name_to_handler_ptr.emplace(handler_name, h);
}

void cmd::discord::gateway::remove_listener(const std::string &event_name,
                                            const std::string &handler_name)
{
    auto range = event_name_to_handler_name.equal_range(event_name);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == handler_name) {
            event_name_to_handler_name.erase(it);
            handler_name_to_handler_ptr.erase(handler_name);
        }
    }
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
    websocket.send(s);
}

void cmd::discord::gateway::join_voice_server(const std::string &guild_id,
                                              const std::string &channel_id)
{
    std::string name = user_id + guild_id + channel_id + "voice_state_listener";
    auto handler = std::make_shared<voice_state_listener>(user_id, channel_id, guild_id);
    add_listener("VOICE_STATE_UPDATE", name, handler);
    add_listener("VOICE_SERVER_UPDATE", name, handler);

    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", channel_id},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    safe_send(json.dump());
}

void cmd::discord::gateway::leave_voice_server(const std::string &guild_id,
                                               const std::string &channel_id)
{
    std::string name = user_id + guild_id + channel_id + "voice_state_listener";
    remove_listener("VOICE_STATE_UPDATE", name);
    remove_listener("VOICE_SERVER_UPDATE", name);
}

void cmd::discord::gateway::identify()
{
    nlohmann::json identify_payload{
        {"op", static_cast<int>(gtw_op_send::identify)},
        {"d",
         {{"token", token},
          {"properties",
           {{"$os", "linux"}, {"$browser", "cmd-discord"}, {"$device", "cmd-discord"}}},
          {"compress", compress},
          {"large_threshold", large_threshold}}}};
    std::string message = identify_payload.dump();
    safe_send(message);
}

void cmd::discord::gateway::resume()
{
    // If we are connected, ignore any resumes
    if (state == connection_state::connected)
        return;

    if (session_id.empty())
        throw std::runtime_error("Could not resume previous session: no such session");

    // Close the previous connection and create a new websocket
    websocket.close(cmd::websocket_status_code::going_away);
    websocket = cmd::websocket{};
    websocket.connect("wss://gateway.discord.gg/?v=6&encoding=json");

    nlohmann::json resume_payload{
        {"op", static_cast<int>(gtw_op_send::resume)},
        {"d", {{"token", token}, {"session_id", session_id}, {"seq", seq_num}}}};
    safe_send(resume_payload.dump());
}

std::string cmd::discord::gateway::get_user_id()
{
    return user_id;
}

std::string cmd::discord::gateway::get_session_id()
{
    return session_id;
}

void cmd::discord::gateway::run_public_dispatch(gtw_op_recv op, nlohmann::json &data,
                                                const std::string &t)
{
    auto range = event_name_to_handler_name.equal_range(t);
    for (auto it = range.first; it != range.second; ++it) {
        auto handler = handler_name_to_handler_ptr.find(it->second);
        if (handler != handler_name_to_handler_ptr.end())
            handler->second->handle(*this, op, data, t);
    }
    // Run all event listeners subscribing to 'ALL'
    auto range_all = event_name_to_handler_name.equal_range("ALL");
    for (auto it = range_all.first; it != range_all.second; ++it) {
        auto handler = handler_name_to_handler_ptr.find(it->second);
        if (handler != handler_name_to_handler_ptr.end())
            handler->second->handle(*this, op, data, t);
    }
}

void cmd::discord::gateway::run_gateway_dispatch(nlohmann::json &data,
                                                 const std::string &event_name)
{
    auto event_it = gateway_event_map.find(event_name);
    if (event_it != gateway_event_map.end())
        event_it->second(data);
}
