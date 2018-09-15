#ifndef DISCORD_VOICE_STATE_LISTENER_H
#define DISCORD_VOICE_STATE_LISTENER_H

#include <boost/asio/high_resolution_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <deque>
#include <memory>

#include "aliases.h"
#include "audio/opus_encoder.h"
#include "audio/source.h"
#include "discord.h"

namespace discord
{
class gateway;
class voice_gateway;
class voice_state_listener;

struct voice_context : std::enable_shared_from_this<voice_context> {
public:
    voice_context(boost::asio::io_context &ctx, std::shared_ptr<voice_state_listener> listener);
    ~voice_context();
    void on_voice_state_update(discord::voice_state s);
    void on_voice_server_update(discord::event::voice_server_update v, discord::snowflake user_id,
                                ssl::context &tls);
    void notify_audio_source_ready(const boost::system::error_code &ec);
    void disconnect();

    void send_next_frame();
    void next_audio_source();
    void join_channel(const std::string &s);
    void leave_channel();
    void add_queue(const std::string &s);
    void list_queue();
    void skip_current();
    void play();
    void play(const opus_frame &frame);
    void pause();

    discord::snowflake get_channel_id() const;
    discord::snowflake get_guild_id() const;
    const std::string &get_session_id() const;
    const std::string &get_token() const;
    const std::string &get_endpoint() const;
    void set_endpoint(const std::string &s);

    discord::opus_encoder &get_encoder();
    boost::asio::io_context &get_io_context();

private:
    boost::asio::io_context &ctx;
    boost::asio::high_resolution_timer timer;
    std::shared_ptr<audio_source> source;
    std::shared_ptr<discord::voice_gateway> gateway;
    std::shared_ptr<discord::voice_state_listener> listener;
    std::deque<std::string> music_queue;

    discord::opus_encoder encoder{2, 48000};
    discord::snowflake channel_id;
    discord::snowflake guild_id;

    std::string session_id;
    std::string token;
    std::string endpoint;
    enum class state { disconnected, connected, playing, paused } p_state;

    void update_bitrate();
};

class voice_state_listener : public std::enable_shared_from_this<voice_state_listener>
{
public:
    voice_state_listener(boost::asio::io_context &ctx, ssl::context &tls,
                         discord::gateway &gateway);
    ~voice_state_listener();

    void disconnect();
    void on_voice_state_update(const nlohmann::json &data);
    void on_voice_server_update(const nlohmann::json &data);
    void on_message_create(const nlohmann::json &data);
    const discord::gateway &get_gateway() const;

private:
    boost::asio::io_context &ctx;
    ssl::context &tls;
    discord::gateway &gateway;

    // guild_id to voice_context (1 voice connection per guild)
    std::map<discord::snowflake, std::shared_ptr<discord::voice_context>> voice_map;

    void join_voice_server(discord::snowflake guild_id, discord::snowflake channel_id);
    void leave_voice_server(discord::snowflake guild_id);
    void check_command(const discord::message &m);
    void join_channel(const discord::message &m, const std::string &s);
};
}  // namespace discord

#endif
