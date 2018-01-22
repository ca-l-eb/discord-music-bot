#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <boost/asio.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <audio_source/source.h>
#include <callbacks.h>
#include <delayed_message_sender.h>
#include <heartbeater.h>
#include <net/websocket.h>

namespace discord
{
struct voice_gateway_entry;

class voice_gateway : public beatable
{
public:
    enum class error {
        ip_discovery_failed = 1,
        unknown_opcode = 4001,
        not_authenticated = 4003,
        authentication_failed = 4004,
        already_authenticated = 4005,
        session_no_longer_valid = 4006,
        session_timeout = 4009,
        server_not_found = 4011,
        unknown_protocol = 4012,
        disconnected = 4014,
        voice_server_crashed = 4015,
        unknown_encryption_mode = 4016
    };

    struct error_category : public boost::system::error_category {
        virtual const char *name() const noexcept;
        virtual std::string message(int ev) const noexcept;
        virtual bool equivalent(const boost::system::error_code &code, int condition) const
            noexcept;
        static const boost::system::error_category &instance();
    };

    voice_gateway(boost::asio::io_context &ctx, discord::voice_gateway_entry &e, uint64_t user_id);
    ~voice_gateway();

    void heartbeat() override;
    void send(const std::string &s, transfer_cb c);
    void connect(boost::asio::ip::tcp::resolver &resolver, error_cb c);
    void play(audio_frame frame);
    void stop();

private:
    boost::asio::io_context &ctx;
    websocket websock;
    discord::delayed_message_sender sender;
    discord::voice_gateway_entry &entry;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint send_endpoint, receive_endpoint;
    boost::asio::ip::udp::resolver resolver;
    boost::asio::deadline_timer timer;

    uint64_t user_id;
    uint32_t ssrc;
    uint16_t udp_port;
    std::vector<uint8_t> secret_key;
    std::vector<uint8_t> buffer;
    std::string external_ip;
    uint32_t timestamp;
    uint16_t seq_num;

    std::unique_ptr<heartbeater> beater;
    enum class connection_state { disconnected, connected } state;

    int retries;
    bool is_speaking;

    error_cb voice_connect_callback;

    void start_speaking(transfer_cb c);
    void stop_speaking(transfer_cb c);
    void send_audio(audio_frame audio);
    void identify();
    void resume();
    void event_loop();
    void handle_event(const uint8_t *data, size_t len);
    void on_connect(const boost::system::error_code &e);
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void ip_discovery();
    void send_ip_discovery_datagram();
    void notify_heartbeater_hello(nlohmann::json &data);
    void select(uint16_t local_udp_port);
};

boost::system::error_code make_error_code(discord::voice_gateway::error code) noexcept;
}

template<>
struct boost::system::is_error_code_enum<discord::voice_gateway::error> : public boost::true_type {
};

#endif
