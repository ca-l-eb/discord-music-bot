#ifndef CMD_DISCORD_VOICE_STATE_LISTENER_H
#define CMD_DISCORD_VOICE_STATE_LISTENER_H

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <deque>
#include <memory>

#include "audio_source/source.h"
#include "events/event_listener.h"
#include "gateway.h"
#include "voice/opus_encoder.h"

namespace discord
{
class voice_gateway;

struct music_process {
    boost::asio::io_context &ctx;
    boost::asio::deadline_timer timer;
    discord::opus_encoder encoder{2, 48000};
    std::shared_ptr<audio_source> source;

    music_process(boost::asio::io_context &ctx) : ctx{ctx}, timer{ctx} {}
};

struct voice_gateway_entry {
    uint64_t channel_id;
    uint64_t guild_id;
    std::string session_id;
    std::string token;
    std::string endpoint;

    enum class state { disconnected, connected, playing, paused } p_state;

    std::deque<std::string> music_queue;
    std::shared_ptr<discord::voice_gateway> gateway;
    std::unique_ptr<music_process> process;
};

class voice_state_listener : public event_listener,
                             public std::enable_shared_from_this<voice_state_listener>
{
public:
    voice_state_listener(boost::asio::io_context &ctx, discord::gateway &gateway,
                         discord::gateway_store &store, ssl::context &tls);
    ~voice_state_listener() = default;
    void handle(discord::gateway &gateway, gateway_op, const nlohmann::json &json,
                const std::string &type) override;

private:
    boost::asio::io_context &ctx;
    discord::gateway &gateway;
    discord::gateway_store &store;
    ssl::context &tls;

    // guild_id to voice_gateway_entry (1 voice connection per guild)
    std::map<uint64_t, std::shared_ptr<voice_gateway_entry>> voice_gateways;

    void voice_state_update(const nlohmann::json &data);
    void voice_server_update(const nlohmann::json &data);
    void message_create(const nlohmann::json &data);

    void join_voice_server(uint64_t guild_id, uint64_t channel_id);
    void leave_voice_server(uint64_t guild_id);
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
}

#endif
