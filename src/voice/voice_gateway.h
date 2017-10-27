#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <delayed_message_sender.h>
#include <heartbeater.h>
#include <net/websocket.h>
#include <memory>
#include <iostream>

namespace cmd
{
namespace discord
{
struct voice_gateway_entry;

class voice_gateway : public beatable
{
public:
    voice_gateway(boost::asio::io_service &service, cmd::discord::voice_gateway_entry &e,
                  std::string user_id);

    void next_event();
    void identify();
    void resume();
    void heartbeat() override;
    void send(const std::string &s, cmd::websocket::message_sent_callback c);
    void start_speaking();
    void stop_speaking();

private:
    boost::asio::io_service &service;
    cmd::websocket websocket;
    cmd::discord::delayed_message_sender sender;
    cmd::discord::voice_gateway_entry &entry;
    std::string user_id;

    uint32_t ssrc;
    uint16_t udp_port;
    std::vector<uint8_t> secret_key;
    std::string external_ip;

    std::unique_ptr<heartbeater> beater;
    enum class connection_state { disconnected, connected } state;

    void on_connect(const boost::system::error_code &e, size_t transferred);
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void ip_discovery();
    void notify_heartbeater_hello(nlohmann::json &data);
    void select(uint16_t local_udp_port);
    void play_audio(const std::string &youtube_url);
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

#endif
