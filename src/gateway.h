#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <cmd/websocket.h>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <api.h>
#include <events/event_listener.h>
#include <heartbeater.h>
#include <voice_gateway.h>
#include <json.hpp>

namespace cmd
{
namespace discord
{
class gateway : public beatable
{
public:
    explicit gateway(const std::string &token);
    ~gateway();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void next_event();
    void heartbeat() override;
    void identify();
    void resume();

    void join_voice_server(const std::string &guild_id, const std::string &channel_id);
    void leave_voice_server(const std::string &guild_id, const std::string &channel_id);

    std::string get_user_id();
    std::string get_session_id();

    void add_voice_gateway(const std::string &channel_id);

private:
    using clock = std::chrono::steady_clock;
    void safe_send(const std::string &s);
    void run_public_dispatch(gtw_op_recv op, nlohmann::json &data, const std::string &t);
    void run_gateway_dispatch(nlohmann::json &data, const std::string &event_name);

    cmd::websocket websocket;
    std::vector<unsigned char> buffer;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler name
    std::multimap<std::string, std::string> event_name_to_handler_name;
    // Map a handler name to a event_lister
    std::map<std::string, event_listener::ptr> handler_name_to_handler_ptr;

    std::map<std::string, std::function<void(nlohmann::json &)>> gateway_event_map;

    heartbeater beater;

    std::string token;
    std::mutex write_mutex;

    clock::time_point last_msg_sent;

    std::string user_id, session_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
    enum class connection_state { disconnected, connected } state;
};
}
}

#endif