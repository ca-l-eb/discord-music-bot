#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <condition_variable>
#include <mutex>
#include <thread>

#include <api.h>
#include <discord/guild.h>
#include <events/event_listener.h>
#include <heartbeater.h>
#include <net/websocket.h>
#include <voice/voice_gateway.h>
#include <iostream>
#include <json.hpp>

namespace cmd
{
namespace discord
{
class gateway : public beatable
{
public:
    gateway(boost::asio::io_service &service, const std::string &token);
    ~gateway();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void heartbeat() override;
    void identify();
    void resume();
    void send(const std::string &s, cmd::websocket::message_sent_callback c);

    std::string get_user_id();
    std::string get_session_id();

    cmd::websocket::message_sent_callback ignore_send = [](const boost::system::error_code &,
                                                           size_t) {};
    cmd::websocket::message_sent_callback print_info_send = [](const boost::system::error_code &e,
                                                               size_t transferred) {
        if (e) {
            std::cerr << "Message send error: " << e.message() << "\n";
        } else {
            std::cout << "Sent " << transferred << " bytes\n";
        }
    };

private:
    boost::asio::io_service &service;
    cmd::websocket websocket;
    cmd::discord::delayed_message_sender sender;
    std::vector<cmd::discord::guild> guilds;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler name
    std::multimap<std::string, std::string> event_name_to_handler_name;

    // Map a handler name to a event_lister
    std::map<std::string, event_listener::ptr> handler_name_to_handler_ptr;
    std::map<std::string, std::function<void(nlohmann::json &)>> gateway_event_map;

    std::unique_ptr<heartbeater> beater;
    std::string token;
    std::string user_id, session_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
    enum class connection_state { disconnected, connected } state;

    void run_public_dispatch(gtw_op_recv op, nlohmann::json &data, const std::string &t);
    void run_gateway_dispatch(nlohmann::json &data, const std::string &event_name);
    void on_connect(const boost::system::error_code &e, size_t);
    void event_loop();
};
}
}

#endif
