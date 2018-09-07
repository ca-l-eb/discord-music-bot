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

struct music_process {
    boost::asio::io_context &ctx;
    boost::asio::high_resolution_timer timer;
    discord::opus_encoder encoder;
    std::shared_ptr<audio_source> source;

    music_process(boost::asio::io_context &ctx) : ctx{ctx}, timer{ctx}, encoder{2, 48000} {}
};

struct voice_gateway_entry {
    discord::snowflake channel_id;
    discord::snowflake guild_id;
    std::string session_id;
    std::string token;
    std::string endpoint;

    enum class state { disconnected, connected, playing, paused } p_state;

    std::shared_ptr<discord::voice_gateway> gateway;
    std::unique_ptr<music_process> process;
    std::deque<std::string> music_queue;
};

void send_audio(voice_gateway_entry *entry);

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

private:
    boost::asio::io_context &ctx;
    ssl::context &tls;
    discord::gateway &gateway;

    // guild_id to voice_gateway_entry (1 voice connection per guild)
    std::map<discord::snowflake, std::shared_ptr<voice_gateway_entry>> voice_gateways;

    void join_voice_server(discord::snowflake guild_id, discord::snowflake channel_id);
    void leave_voice_server(discord::snowflake guild_id);
    void check_command(const discord::message &m);
    void do_join(const discord::message &m, const std::string &s);
    void do_leave(discord::voice_gateway_entry &entry);
    void do_list(discord::voice_gateway_entry &entry);
    void do_add(discord::voice_gateway_entry &entry, const std::string &s);
    void do_skip(discord::voice_gateway_entry &entry);
    void do_play(discord::voice_gateway_entry &entry);
    void do_pause(discord::voice_gateway_entry &entry);

    void play(voice_gateway_entry &entry);
    void next_audio_source(voice_gateway_entry &entry);
    void send_audio(voice_gateway_entry &entry);
};
}  // namespace discord

#endif
