#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <cmd/udp_socket.h>
#include <cmd/websocket.h>
#include <events/event_listener.h>
#include "heartbeater.h"

namespace cmd
{
namespace discord
{
enum class gtw_voice_op_send {
    identify = 0,
    select_proto = 1,
    heartbeat = 3,
    speaking = 5,
    resume = 7
};

enum class gtw_voice_op_recv {
    ready = 2,
    session_description = 4,
    speaking = 5,
    heartbeat_ack = 6,
    hello = 8,
    resumed = 9,
    client_disconnect = 13
};

class voice_gateway : public beatable
{
public:
    voice_gateway(const std::string &url, const std::string &user_id, const std::string &session_id,
                  const std::string &guild_id, const std::string &token);

    void next_event();
    void identify();
    void resume();
    void heartbeat() override;
    void safe_send(const std::string &s);

    void start_speaking();
    void stop_speaking();

private:
    using clock = std::chrono::steady_clock;
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void ip_discovery();
    void notify_heartbeater_hello(nlohmann::json &data);
    void select(uint16_t local_udp_port);
    void play_audio(const std::string &youtube_url);
    void write_header(unsigned char *buffer, uint16_t seq_num, uint32_t timestamp);

    cmd::websocket websocket;
    std::vector<unsigned char> buffer;
    std::string url, user_id, session_id, guild_id, token;

    std::mutex write_mutex;
    clock::time_point last_msg_sent;

    heartbeater beater;
    enum class connection_state { disconnected, connected } state;

    std::thread audio_thread;

    uint32_t ssrc;
    uint16_t udp_port;
    std::vector<uint8_t> secret_key;

    cmd::udp_socket udp_socket;
    cmd::inet_addr voice_addr;
    std::string external_ip;
};
}
}

#endif
