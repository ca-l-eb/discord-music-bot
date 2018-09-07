#ifndef DISCORD_VOICE_GATEWAY_H
#define DISCORD_VOICE_GATEWAY_H

#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aliases.h"
#include "audio/source.h"
#include "callbacks.h"
#include "heartbeater.h"
#include "net/connection.h"
#include "net/rtp.h"

namespace discord
{
struct voice_gateway_entry;

class voice_gateway : public std::enable_shared_from_this<voice_gateway>
{
public:
    voice_gateway(boost::asio::io_context &ctx, boost::asio::ssl::context &tls,
                  std::shared_ptr<discord::voice_gateway_entry> e, discord::snowflake user_id);
    void heartbeat();
    void send(const std::string &s, transfer_cb c);
    void connect(error_cb c);
    void disconnect();
    void play(const opus_frame &frame);
    void stop();

private:
    boost::asio::io_context &ctx;
    std::shared_ptr<discord::voice_gateway_entry> entry;
    discord::connection conn;
    discord::rtp_session rtp;
    discord::heartbeater beater;

    discord::snowflake user_id;
    enum class connection_state { disconnected, connected } state;
    bool is_speaking;
    error_cb voice_connect_callback;

    void start_speaking(transfer_cb c);
    void stop_speaking(transfer_cb c);
    void send_audio(opus_frame audio);
    void identify();
    void resume();
    void next_event();
    void handle_event(const nlohmann::json &data);
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void notify_heartbeater_hello(nlohmann::json &data);
    void select();
};
}  // namespace discord

#endif
