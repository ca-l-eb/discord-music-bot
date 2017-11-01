#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <memory>
#include <iostream>
#include <vector>
#include <cstdint>

#include <delayed_message_sender.h>
#include <heartbeater.h>
#include <voice/opus_encoder.h>
#include <net/websocket.h>

namespace cmd
{
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

    class error_category : public boost::system::error_category
    {
    public:
        virtual const char *name() const noexcept
        {
            return "voice_gateway";
        }

        virtual std::string message(int ev) const noexcept
        {
            switch (error(ev)) {
                case error::ip_discovery_failed:
                    return "ip discovery failed";
                case error::unknown_opcode:
                    return "invalid opcode";
                case error::not_authenticated:
                    return "sent payload before identified";
                case error::authentication_failed:
                    return "incorrect token in identify payload";
                case error::already_authenticated:
                    return "sent more than one identify payload";
                case error::session_no_longer_valid:
                    return "session is no longer valid";
                case error::session_timeout:
                    return "session has timed out";
                case error::server_not_found:
                    return "server not found";
                case error::unknown_protocol:
                    return "unrecognized protocol";
                case error::disconnected:
                    return "disconnected";
                case error::voice_server_crashed:
                    return "voice server crashed";
                case error::unknown_encryption_mode:
                    return "unrecognized encryption";
            }
            return "Unknown voice gateway error";
        }

        virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept
        {
            return &code.category() == this && static_cast<int>(code.value()) == condition;
        }
    };

    static const boost::system::error_category &category()
    {
        static voice_gateway::error_category instance;
        return instance;
    }

    static boost::system::error_code make_error_code(voice_gateway::error code) noexcept
    {
        return boost::system::error_code{(int) code, category()};
    }

    using connect_callback = std::function<void(const boost::system::error_code &)>;

    voice_gateway(boost::asio::io_service &service, cmd::discord::voice_gateway_entry &e,
                  std::string user_id);
    ~voice_gateway();

    void heartbeat() override;
    void send(const std::string &s, cmd::websocket::message_sent_callback c);
    void connect(connect_callback c);
    void play(const int16_t *pcm, size_t frame_size);
    void stop();

private:
    boost::asio::io_service &service;
    cmd::websocket websocket;
    cmd::discord::delayed_message_sender sender;
    cmd::discord::voice_gateway_entry &entry;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint send_endpoint, receive_endpoint;
    boost::asio::ip::udp::resolver resolver;
    boost::asio::deadline_timer timer;
    std::string user_id;

    uint32_t ssrc;
    uint16_t udp_port;
    std::vector<uint8_t> secret_key;
    std::vector<uint8_t> buffer;
    std::string external_ip;
    cmd::discord::opus_encoder encoder;
    uint32_t timestamp;
    uint16_t seq_num;

    std::unique_ptr<heartbeater> beater;
    enum class connection_state { disconnected, connected } state;

    int retries;

    connect_callback callback;
    
    void start_speaking();
    void stop_speaking();
    void identify();
    void resume();
    void event_loop();
    void on_connect(const boost::system::error_code &e, size_t transferred);
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void ip_discovery();
    void send_ip_discovery_datagram();
    void notify_heartbeater_hello(nlohmann::json &data);
    void select(uint16_t local_udp_port);
    void write_header(unsigned char *buffer, uint16_t seq_num, uint32_t timestamp);

    cmd::websocket::message_sent_callback print_info = [](const boost::system::error_code &e,
                                                          size_t transferred) {
        if (e) {
            std::cerr << "Voice gateway send error: " << e.message() << "\n";
        } else {
            std::cout << "Voice gateway sent " << transferred << " bytes\n";
        }
    };
};
}
}

template<>
struct std::is_error_code_enum<cmd::discord::voice_gateway::error> : public std::true_type {
};

template<>
struct boost::system::is_error_code_enum<cmd::discord::voice_gateway::error> : public boost::true_type {
};


#endif
