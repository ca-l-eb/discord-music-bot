#include <iostream>
#include <memory>

#include "errors.h"
#include "gateway.h"
#include "voice/voice_state_listener.h"

discord::gateway::gateway(boost::asio::io_context &ctx, ssl::context &tls, const std::string &token,
                          discord::connection &c)
    : conn{c}, beater{ctx}, token{token}, state{connection_state::disconnected}
{
    event_to_handler.emplace("READY", [&](auto &json) { on_ready(json); });
    event_to_handler.emplace("GUILD_CREATE", [&](auto &json) { store.guild_create(json); });
    event_to_handler.emplace("CHANNEL_CREATE", [&](auto &json) { store.channel_create(json); });
    event_to_handler.emplace("CHANNEL_UPDATE", [&](auto &json) { store.channel_update(json); });
    event_to_handler.emplace("CHANNEL_DELETE", [&](auto &json) { store.channel_delete(json); });
    event_to_handler.emplace("RESUME", [&](auto &) { state = connection_state::connected; });

    auto handler = std::make_shared<voice_state_listener>(ctx, tls, *this, store);
    event_to_handler.emplace("VOICE_STATE_UPDATE",
                             [handler](auto &json) { handler->on_voice_state_update(json); });
    event_to_handler.emplace("VOICE_SERVER_UPDATE",
                             [handler](auto &json) { handler->on_voice_server_update(json); });
    event_to_handler.emplace("MESSAGE_CREATE",
                             [handler](auto &json) { handler->on_message_create(json); });
}

void discord::gateway::run()
{
    conn.connect("wss://gateway.discord.gg/?v=6&encoding=json", [this](auto &ec) {
        if (ec) {
            throw std::runtime_error{"Could not connect: " + ec.message()};
        }
        identify();
    });
}

void discord::gateway::heartbeat()
{
    auto json = nlohmann::json{{"op", static_cast<int>(gateway_op::heartbeat)}, {"d", seq_num}};
    send(json.dump(), ignore_transfer);
}

void discord::gateway::send(const std::string &s, transfer_cb c)
{
    conn.send(s, c);
}

uint64_t discord::gateway::get_user_id() const
{
    return user_id;
}

const std::string &discord::gateway::get_session_id() const
{
    return session_id;
}

void discord::gateway::identify()
{
    auto identify_payload = nlohmann::json{
        {"op", static_cast<int>(gateway_op::identify)},
        {"d",
         {{"token", token},
          {"properties",
           {{"$os", "linux"}, {"$browser", "cmd-discord"}, {"$device", "cmd-discord"}}},
          {"compress", false},
          {"large_threshold", 250}}}};

    auto callback = [this](auto &ec, size_t) {
        if (ec) {
            std::cerr << "[gateway] identify send error: " << ec.message() << "\n";
        } else {
            std::cout << "[gateway] beginning event loop\n";
            next_event();
        }
    };
    send(identify_payload.dump(), callback);
}

void discord::gateway::resume()
{
    // If we are connected, ignore any resumes
    if (state == connection_state::connected)
        return;

    if (session_id.empty())
        throw std::runtime_error("Could not resume previous session: no such session");

    std::cout << "[gateway] attempting to resume connection\n";

    // TODO: Close the previous connection and create a new websocket

    auto resume_payload =
        nlohmann::json{{"op", static_cast<int>(gateway_op::resume)},
                       {"d", {{"token", token}, {"session_id", session_id}, {"seq", seq_num}}}};
    send(resume_payload.dump(), ignore_transfer);
}

void discord::gateway::on_ready(const nlohmann::json &data)
{
    auto ready = data.get<discord::event::ready>();
    if (ready.version != 6) {
        throw std::runtime_error("Unsupported gateway protocol version: " +
                                 std::to_string(ready.version) + ". Support is limited to v6");
    }

    // We got the READY event, we are now connected
    state = connection_state::connected;

    user_id = ready.user.id;
    session_id = std::move(ready.session_id);
}

void discord::gateway::next_event()
{
    // Asynchronously read next message, on message received send it to listeners
    conn.read([=](auto &json) { handle_event(json); });
}

void discord::gateway::handle_event(const nlohmann::json &j)
{
    if (!j.empty()) {
        std::cout << "[gateway] " << j.dump() << "\n";
        try {
            auto payload = j.get<discord::payload>();
            seq_num = payload.sequence_num;

            if (payload.event_name == "MESSAGE_CREATE") {
                auto message = payload.data.get<discord::message>();
            }

            switch (payload.op) {
                case gateway_op::dispatch:
                    run_gateway_dispatch(payload.data, payload.event_name);
                    break;
                case gateway_op::heartbeat:
                    heartbeat();  // Respond to heartbeats with a heartbeat
                    break;
                case gateway_op::reconnect:
                    // We are asked to reconnect, i.e. we are disconnected
                    state = connection_state::disconnected;
                    resume();
                    break;
                case gateway_op::invalid_session:
                    if (state == connection_state::disconnected) {
                        throw std::runtime_error("Could not connect to Discord Gateway");
                    } else {
                        state = connection_state::disconnected;
                        // Already connected, if we can reconnect, try to resume the
                        // connection
                        if (payload.data.is_boolean() && payload.data.get<bool>())
                            resume();
                        else
                            throw std::runtime_error("disconnected");
                    }
                    break;
                case gateway_op::hello:
                    beater.on_hello(payload.data, *this);
                    break;
                case gateway_op::heartbeat_ack:
                    beater.on_heartbeat_ack();
                    break;
                default:
                    throw std::runtime_error("Unknown opcode: " +
                                             std::to_string(static_cast<int>(payload.op)));
            }
        } catch (nlohmann::json::exception &e) {
            std::cerr << "[gateway] " << e.what() << "\n";
        }
    }
    next_event();
}

void discord::gateway::run_gateway_dispatch(nlohmann::json &data, const std::string &event_name)
{
    using namespace std::string_literals;
    auto events = {event_name, "ALL"s};
    for (auto &event : events) {
        auto range = event_to_handler.equal_range(event);
        for (auto it = range.first; it != range.second; ++it) {
            it->second(data);
        }
    }
}
