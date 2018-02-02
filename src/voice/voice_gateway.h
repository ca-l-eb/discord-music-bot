#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "aliases.h"
#include "audio_source/source.h"
#include "callbacks.h"
#include "heartbeater.h"

namespace discord
{
struct voice_gateway_entry;

class voice_gateway : public std::enable_shared_from_this<voice_gateway>
{
public:
    voice_gateway(boost::asio::io_context &ctx, ssl::context &tls,
                  std::shared_ptr<discord::voice_gateway_entry> e, uint64_t user_id);
    ~voice_gateway();

    void heartbeat();
    void send(const std::string &s, transfer_cb c);
    void connect(error_cb c);
    void play(audio_frame frame);
    void stop();

private:
    boost::asio::io_context &ctx;
    ssl::context &tls;
    tcp::resolver tcp_resolver;
    secure_websocket websock;

    std::shared_ptr<discord::voice_gateway_entry> entry;
    boost::beast::multi_buffer multi_buffer;

    udp::socket udp_socket;
    udp::endpoint send_endpoint, receive_endpoint;
    udp::resolver udp_resolver;
    boost::asio::deadline_timer timer;

    uint64_t user_id;
    uint32_t ssrc;
    uint16_t udp_port;
    std::vector<uint8_t> secret_key;
    std::vector<uint8_t> buffer;
    std::string external_ip;
    uint32_t timestamp;
    uint16_t seq_num;

    heartbeater beater;

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
    void handle_event(const std::string &data);
    void extract_ready_info(nlohmann::json &data);
    void extract_session_info(nlohmann::json &data);
    void ip_discovery();
    void send_ip_discovery_datagram();
    void notify_heartbeater_hello(nlohmann::json &data);
    void select(uint16_t local_udp_port);

    void on_resolve(const boost::system::error_code &ec, tcp::resolver::iterator it);
    void on_connect(const boost::system::error_code &ec, tcp::resolver::iterator);
    void on_tls_handshake(const boost::system::error_code &ec);
    void on_websocket_handshake(const boost::system::error_code &ec);
    void on_read(const boost::system::error_code &ec, size_t transferred);
};
}

#endif
