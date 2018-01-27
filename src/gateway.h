#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <condition_variable>
#include <iostream>
#include <json.hpp>
#include <mutex>
#include <thread>

#include <api.h>
#include <callbacks.h>
#include <delayed_message_sender.h>
#include <discord.h>
#include <events/event_listener.h>
#include <gateway_store.h>
#include <heartbeater.h>
#include <net/websocket.h>
#include <voice/voice_gateway.h>

namespace discord
{
class gateway : public beatable
{
public:
    gateway(boost::asio::io_context &ctx, const std::string &token,
            boost::asio::ip::tcp::resolver &resolver);
    ~gateway();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void heartbeat() override;
    void send(const std::string &s, transfer_cb c);
    uint64_t get_user_id() const;
    const std::string &get_session_id() const;

    boost::asio::ip::tcp::resolver &resolver;

private:
    boost::asio::io_context &ctx;
    std::shared_ptr<websocket> websock;
    discord::delayed_message_sender sender;
    discord::gateway_store store;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler name
    std::multimap<std::string, std::string> event_name_to_handler_name;

    // Map a handler name to a event_lister
    std::map<std::string, event_listener::ptr> handler_name_to_handler_ptr;
    std::map<std::string, std::function<void(nlohmann::json &)>> gateway_event_map;

    std::unique_ptr<heartbeater> beater;
    std::string token;
    std::string session_id;
    uint64_t user_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
    enum class connection_state { disconnected, connected } state;

    void run_public_dispatch(gateway_op op, nlohmann::json &data, const std::string &t);
    void run_gateway_dispatch(nlohmann::json &data, const std::string &event_name);
    void identify();
    void resume();
    void on_connect(const boost::system::error_code &e);
    void on_ready(nlohmann::json &data);
    void event_loop();
    void handle_event(const uint8_t *data, size_t len);
};
}

#endif
