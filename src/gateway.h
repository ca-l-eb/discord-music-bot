#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <cmd/websocket.h>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <api.h>
#include <events/event_listener.h>
#include <json.hpp>

namespace cmd
{
namespace discord
{
class gateway
{
public:
    explicit gateway(cmd::websocket::socket &sock, const std::string &token);
    ~gateway();
    void add_listener(const std::string &name, event_listener::ptr h);
    void remove_listener(const std::string &name);
    void next_event();
    void heartbeat();

    void join_voice_server(const std::string &guild_id, const std::string &channel_id);
    void leave_voice_server(const std::string &guild_id, const std::string &channel_id);

private:
    using clock = std::chrono::steady_clock;
    void safe_send(const std::string &s);

    cmd::websocket::socket sock;
    std::vector<unsigned char> buffer;

    std::map<std::string, event_listener::ptr> public_handlers;
    std::multimap<gtw_op_recv, event_listener::ptr> private_handlers;

    std::string token;
    std::mutex write_mutex;

    clock::time_point last_msg_sent;

    std::string user_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
};
}
}

#endif