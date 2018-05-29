#ifndef DISCORD_NET_RTP_H
#define DISCORD_NET_RTP_H

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "aliases.h"
#include "audio_source/source.h"
#include "callbacks.h"

namespace discord
{
class rtp_session
{
public:
    rtp_session(boost::asio::io_context &ctx);
    void connect(const std::string &host, const std::string &port, error_cb c);
    void ip_discovery(error_cb c);
    void send(opus_frame frame);
    void set_ssrc(uint32_t ssrc);
    void set_secret_key(std::vector<uint8_t> key);
    const std::string &get_external_ip() const;
    uint16_t get_external_port() const;

private:
    udp::socket sock;
    udp::resolver resolver;
    boost::asio::deadline_timer timer;
    uint32_t ssrc;
    uint32_t timestamp;
    uint16_t seq_num;
    uint16_t external_port;
    std::string external_ip;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> secret_key;

    void send_ip_discovery_datagram(int retries, error_cb c);
};

}  // namespace discord

#endif
