#include "gateway.h"
#include <future>
#include <iostream>
#include "json.hpp"

cmd::discord::gateway::gateway::gateway(cmd::websocket::socket &sock, const std::string &token)
    : sock{sock}, token{token}
{
    // On hello opcode, run the heartbeat_listener, spawns a thread and periodically calls
    // this->heartbeat(). On destruction it stops the heartbeat thread and joins it
    register_listener(op_recv::hello,
                      event_listener::base::make<event_listener::heartbeat_listener>(this));

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

cmd::discord::gateway::gateway::~gateway() {}

void cmd::discord::gateway::gateway::next_event()
{
    sock.next_message(buffer);
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

        auto gateway_op = static_cast<op_recv>(op_val);
        switch (gateway_op) {
            case op_recv::dispatch:
                std::cout << "DISPATCH";
                break;
            case op_recv::heartbeat:
                std::cout << "HEARTBEAT";
                break;
            case op_recv::reconnect:
                std::cout << "RECONNECT";
                break;
            case op_recv::invalid_session:
                std::cout << "INVALID SESSION";
                break;
            case op_recv::hello:
                std::cout << "HELLO";
                break;
            case op_recv::heatbeat_ack:
                std::cout << "HEARTBEAT ACK";
                break;
            default:
                throw std::runtime_error("Unknown opcode: " + std::to_string(op_val));
        }
        std::cout << "\n";
        auto range = handlers.equal_range(gateway_op);
        for (auto it = range.first; it != range.second; ++it) {
            it->second->handle(d, t_val);
        }
    }
}

void cmd::discord::gateway::gateway::register_listener(op_recv e, event_listener::base::ptr h)
{
    handlers.emplace(e, h);
}

void cmd::discord::gateway::gateway::heartbeat()
{
    nlohmann::json json{{"op", static_cast<int>(op_send::heartbeat)}, {"d", seq_num}};
    safe_send(json.dump());
    std::cout << "\n--------------HEARTBEAT SENT--------------\n";
}

void cmd::discord::gateway::gateway::safe_send(const std::string &s)
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
