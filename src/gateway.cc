#include "gateway.h"
#include <future>
#include <iostream>
#include "json.hpp"

cmd::discord::gateway::gateway(cmd::websocket::socket &sock, const std::string &token)
    : sock{sock}, token{token}
{
    auto hello_event_handler = [&](nlohmann::json &data, const std::string &) {
        if (!data.is_null()) {
            if (data["heartbeat_interval"].is_number())
                heartbeat_interval = data["heartbeat_interval"].get<int>();
            static bool first = true;
            if (first) {
                heartbeat_thread = std::thread{&cmd::discord::gateway::heartbeat, this};
                first = false;
            }
        }
    };

    register_listener(cmd::discord::gateway_op_recv::hello, hello_event_handler);
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

cmd::discord::gateway::~gateway()
{
    heartbeat_interval = -1;  // Notify other thread
    loop_variable.notify_all();
    if (heartbeat_thread.joinable())
        heartbeat_thread.join();
}

void cmd::discord::gateway::next_event()
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

        auto gateway_op = static_cast<cmd::discord::gateway_op_recv>(op_val);
        switch (gateway_op) {
            case cmd::discord::gateway_op_recv::dispatch:
                std::cout << "DISPATCH";
                break;
            case cmd::discord::gateway_op_recv::heartbeat:
                std::cout << "HEARTBEAT";
                break;
            case cmd::discord::gateway_op_recv::reconnect:
                std::cout << "RECONNECT";
                break;
            case cmd::discord::gateway_op_recv::invalid_session:
                std::cout << "INVALID SESSION";
                break;
            case cmd::discord::gateway_op_recv::hello:
                std::cout << "HELLO";
                break;
            case cmd::discord::gateway_op_recv::heatbeat_ack:
                std::cout << "HEARTBEAT ACK";
                break;
            default:
                throw std::runtime_error("Unknown opcode: " + std::to_string(op_val));
        }
        std::cout << "\n";
        auto range = handlers.equal_range(gateway_op);
        for (auto it = range.first; it != range.second; ++it) {
            it->second(d, t_val);
        }
    }
}

void cmd::discord::gateway::register_listener(
    gateway_op_recv e, std::function<void(nlohmann::json &, const std::string &)> h)
{
    handlers.emplace(e, h);
}

void cmd::discord::gateway::heartbeat()
{
    while (true) {
        std::unique_lock<std::mutex> lock{thread_mutex};
        loop_variable.wait_for(lock, std::chrono::milliseconds{heartbeat_interval});
        if (heartbeat_interval < 0)
            break;

        nlohmann::json json{{"op", static_cast<int>(cmd::discord::gateway_op_send::heartbeat)},
                            {"d", seq_num}};
        sock.send(json.dump());
        std::cout << "\n--------------HEARTBEAT SENT--------------\n";
    }
}

void cmd::discord::gateway::safe_send(const std::string &s)
{
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
