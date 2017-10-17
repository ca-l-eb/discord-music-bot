#include <iostream>

#include "gateway.h"
#include "gateway_event_listeners.h"

void cmd::discord::gateway::event_listener::echo_listener::handle(op_recv,
                                                                  const nlohmann::json &json,
                                                                  const std::string &type)
{
    std::cout << "Type: " << type << " ";
    if (!json.is_null())
        std::cout << "json: " << json;
    std::cout << "\n";
}

cmd::discord::gateway::event_listener::hello_responder::hello_responder(cmd::discord::api *api)
    : api{api}
{
}

void cmd::discord::gateway::event_listener::hello_responder::handle(op_recv,
                                                                    const nlohmann::json &json,
                                                                    const std::string &type)
{
    if (json.is_null())
        return;
    if (type == "MESSAGE_CREATE") {
        std::string channel = json["channel_id"];
        std::string content = json["content"];
        std::string user = json["author"]["username"];

        std::cout << user << ": " << content << " in " << channel << "\n";

        if (content.find("hi") != std::string::npos) {
            auto success = api->send_message(channel, "hey " + user);
            if (success == api_result::success) {
                std::cout << "Success!\n";
            } else if (success == api_result::failure) {
                std::cout << "Failure\n";
            } else if (success == api_result::rate_limited) {
                std::cout << "Rate limited\n";
            }
        }
    }
}

cmd::discord::gateway::event_listener::heartbeat_listener::heartbeat_listener(
    cmd::discord::gateway::gateway *gateway)
    : gateway{gateway}, heartbeat_interval{0}, first{true}
{
}
cmd::discord::gateway::event_listener::heartbeat_listener::~heartbeat_listener()
{
    // Notify thread we are closing, then join it
    notify();
    join();
}

void cmd::discord::gateway::event_listener::heartbeat_listener::heartbeat_loop()
{
    while (true) {
        std::unique_lock<std::mutex> lock{thread_mutex};
        loop_variable.wait_for(lock, std::chrono::milliseconds{heartbeat_interval});
        if (heartbeat_interval < 0)
            break;
        gateway->heartbeat();
    }
}

void cmd::discord::gateway::event_listener::heartbeat_listener::handle(op_recv op,
                                                                       const nlohmann::json &data,
                                                                       const std::string &)
{
    if (op == op_recv::hello && !data.is_null()) {
        if (data["heartbeat_interval"].is_number())
            heartbeat_interval = data["heartbeat_interval"].get<int>();
        std::cout << "Heartbeating every " << heartbeat_interval << " ms\n";
        if (first)
            heartbeat_thread = std::thread{&heartbeat_listener::heartbeat_loop, this};
        first = false;
    } else if (op == op_recv::heartbeat_ack) {
    }
}

void cmd::discord::gateway::event_listener::heartbeat_listener::notify()
{
    heartbeat_interval = -1;
    loop_variable.notify_all();
}

void cmd::discord::gateway::event_listener::heartbeat_listener::join()
{
    if (heartbeat_thread.joinable())
        heartbeat_thread.join();
}