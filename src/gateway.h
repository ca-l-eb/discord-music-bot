#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <json.hpp>

#include "aliases.h"
#include "callbacks.h"
#include "discord.h"
#include "events/event_listener.h"
#include "gateway_store.h"
#include "heartbeater.h"
#include "voice/voice_gateway.h"

namespace discord
{
class gateway : public std::enable_shared_from_this<gateway>
{
public:
    gateway(boost::asio::io_context &ctx, ssl::context &tls, const std::string &token);
    ~gateway() = default;
    void run();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void heartbeat();
    void send(const std::string &s, transfer_cb c);
    uint64_t get_user_id() const;
    const std::string &get_session_id() const;

private:
    boost::asio::io_context &ctx;
    tcp::resolver resolver;
    secure_websocket websock;
    boost::beast::multi_buffer buffer;
    discord::gateway_store store;
    heartbeater beater;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler name
    std::multimap<std::string, std::string> event_name_to_handler_name;

    // Map a handler name to a event_lister
    std::map<std::string, event_listener::ptr> handler_name_to_handler_ptr;
    std::map<std::string, std::function<void(nlohmann::json &)>> gateway_event_map;

    std::string token;
    std::string session_id;
    uint64_t user_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
    enum class connection_state { disconnected, connected } state;

    void on_resolve(const boost::system::error_code &ec, tcp::resolver::iterator it);
    void on_connect(const boost::system::error_code &ec, tcp::resolver::iterator);
    void on_tls_handshake(const boost::system::error_code &ec);
    void on_websocket_handshake(const boost::system::error_code &ec);
    void on_read(const boost::system::error_code &ec, size_t transferred);

    void run_public_dispatch(gateway_op op, nlohmann::json &data, const std::string &t);
    void run_gateway_dispatch(nlohmann::json &data, const std::string &event_name);
    void identify();
    void resume();
    void on_ready(nlohmann::json &data);
    void event_loop();
    void handle_event(const std::string &data);
};
}

#endif
