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
    virtual void handle(nlohmann::json &, const std::string &) = 0;
};

struct echo_listener : public base {
    void handle(nlohmann::json &json, const std::string &type);
};

struct hello_responder : public base {
    hello_responder(cmd::discord::api *api);
    void handle(nlohmann::json &json, const std::string &type);

private:
    cmd::discord::api *api;
};

// On HELLO event, extract heartbeat_interval and spawn thread to heartbeat
struct heartbeat_listener : public base {
    heartbeat_listener(cmd::discord::gateway::gateway *gateway);
    ~heartbeat_listener();
    void heartbeat();
    void handle(nlohmann::json &data, const std::string &);
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
