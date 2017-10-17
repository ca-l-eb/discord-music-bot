#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <cmd/websocket.h>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "api.h"
#include "gateway_event_listeners.h"
#include "json.hpp"

namespace cmd
{
namespace discord
{
namespace gateway
{
class gateway
{
public:
    explicit gateway(cmd::websocket::socket &sock, const std::string &token);
    ~gateway();
    void next_event();
    void register_listener(op_recv e, event_listener::base::ptr h);
    void heartbeat();

private:
    using clock = std::chrono::steady_clock;
    void safe_send(const std::string &s);

    cmd::websocket::socket sock;
    std::vector<unsigned char> buffer;
    std::multimap<op_recv, event_listener::base::ptr> handlers;
    std::string token;
    std::mutex write_mutex;

    clock::time_point last_msg_sent;

    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
};
}
}
}

#endif