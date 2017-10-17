#ifndef CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H
#define CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H

#include <condition_variable>
#include <memory>
#include <thread>

#include "api.h"
#include "json.hpp"

namespace cmd
{
namespace discord
{
namespace gateway
{
class gateway;

enum class op_recv {
    dispatch = 0,
    heartbeat = 1,
    reconnect = 7,
    invalid_session = 9,
    hello = 10,
    heartbeat_ack = 11
};

enum class op_send {
    heartbeat = 1,
    identify = 2,
    status_update = 3,
    voice_state_update = 4,
    voice_server_ping = 5,
    resume = 6,
    request_guild_members = 8
};
namespace event_listener
{
struct base {
    using ptr = std::shared_ptr<base>;
    // Maybe convenient way of making base::ptr of derived types, in case the ptr type changes
    template<typename T, typename... Args>
    static ptr make(Args &&... args)
    {
        return std::make_shared<T>(args...);
    }
    virtual void handle(op_recv op, const nlohmann::json &, const std::string &) = 0;
};

struct echo_listener : public base {
    void handle(op_recv op, const nlohmann::json &json, const std::string &type);
};

struct hello_responder : public base {
    hello_responder(cmd::discord::api *api);
    void handle(op_recv op, const nlohmann::json &json, const std::string &type);

private:
    cmd::discord::api *api;
};

// On HELLO event, extract heartbeat_interval and spawn thread to heartbeat
struct heartbeat_listener : public base {
    heartbeat_listener(cmd::discord::gateway::gateway *gateway);
    ~heartbeat_listener();
    void heartbeat_loop();
    void handle(op_recv op, const nlohmann::json &data, const std::string &);
    void notify();
    void join();

private:
    cmd::discord::gateway::gateway *gateway;
    int heartbeat_interval;
    bool first;

    std::thread heartbeat_thread;
    std::mutex thread_mutex;
    std::condition_variable loop_variable;
};
}
}
}
}

#endif
