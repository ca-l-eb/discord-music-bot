#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <cmd/websocket.h>
#include <iostream>
#include "json.hpp"
#include <boost/lockfree/queue.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace cmd
{
namespace discord
{
enum class gateway_op_recv {
    dispatch = 0,
    heartbeat = 1,
    reconnect = 7,
    invalid_session = 9,
    hello = 10,
    heatbeat_ack = 11
};

enum class gateway_op_send {
    heartbeat = 1,
    identify = 2,
    status_update = 3,
    voice_state_update = 4,
    voice_server_ping = 5,
    resume = 6,
    request_guild_members = 8
};

struct echo_dispatch_handler {
    void operator()(nlohmann::json &data, const std::string &type)
    {
        if (data.is_object())
            std::cout << "d: " << data.dump(4) << "\n";
        else if (!data.is_null())
            std::cout << "d: " << data << "\n";
    }
};

class gateway
{
public:
    explicit gateway(cmd::websocket::socket &sock, const std::string &token);
    ~gateway();
    void next_event();
    void register_listener(gateway_op_recv e, std::function<void(nlohmann::json &, const std::string &)> h);

private:
    using clock = std::chrono::steady_clock;
    void heartbeat();
    void safe_send(const std::string &s);

    cmd::websocket::socket sock;
    std::vector<unsigned char> buffer;
    std::multimap<gateway_op_recv, std::function<void(nlohmann::json &, const std::string &)>> handlers;
    std::string token;

    std::thread heartbeat_thread;
    std::mutex thread_mutex;
    std::condition_variable loop_variable;

    clock::time_point last_msg_sent;

    int heartbeat_interval;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
};
}
}

#endif
