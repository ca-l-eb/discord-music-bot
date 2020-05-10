#ifndef DISCORD_GATEWAY_H
#define DISCORD_GATEWAY_H

#include <memory>

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include "aliases.h"
#include "callbacks.h"
#include "discord.h"
#include "gateway_store.h"
#include "heartbeater.h"
#include "net/connection.h"

namespace discord
{
class gateway : public std::enable_shared_from_this<gateway>
{
public:
    gateway(boost::asio::io_context &ctx, ssl::context &tls, const std::string &token,
            discord::connection &c);
    ~gateway() = default;
    void run();
    void disconnect();
    void heartbeat();
    void send(const std::string &s, transfer_cb c);
    discord::snowflake get_user_id() const;
    const std::string &get_session_id() const;
    const discord::gateway_store &get_gateway_store() const;

    using discord_event_cb = std::function<void(const nlohmann::json &)>;

private:
    discord::connection &conn;
    discord::gateway_store store;
    discord::heartbeater beater;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler
    std::multimap<std::string, discord_event_cb> event_to_handler;

    std::string token;
    std::string session_id;
    discord::snowflake user_id;
    int seq_num;
    enum class connection_state { disconnected, connecting, connected } state;

    void identify();
    void resume();
    void on_ready(const nlohmann::json &data);
    void next_event();
    void handle_event(const nlohmann::json &j);
    void run_gateway_dispatch(const nlohmann::json &data, const std::string &event_name);
};
}  // namespace discord

#endif
