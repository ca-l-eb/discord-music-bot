#include <cstdlib>
#include <cstring>
#include <iostream>

#include "errors.h"
#include "net/rtp.h"
#include "voice/crypto.h"

discord::rtp_session::rtp_session(boost::asio::io_context &ctx)
    : sock{ctx}
    , resolver{ctx}
    , timer{ctx}
    , ssrc{0}
    , timestamp{(uint32_t) rand()}
    , seq_num{(uint16_t) rand()}
    , external_port{0}
    , buffer(1024)
{
    sock.open(udp::v4());
}

void discord::rtp_session::connect(const std::string &host, const std::string &port, error_cb c)
{
    auto query = udp::resolver::query{udp::v4(), host, port};
    resolver.async_resolve(query, [=](auto &ec, auto it) {
        if (ec) {
            c(ec);  // host resolve error
        } else {
            sock.connect(*it);
            std::cout << "[RTP] udp local: " << sock.local_endpoint()
                      << " remote: " << sock.remote_endpoint() << "\n";
            c({});
        }
    });
}

void discord::rtp_session::ip_discovery(error_cb c)
{
    // Prepare buffer for ip discovery
    std::memset(buffer.data(), 0, 70);
    buffer[0] = (ssrc >> 24) & 0xFF;
    buffer[1] = (ssrc >> 16) & 0xFF;
    buffer[2] = (ssrc >> 8) & 0xFF;
    buffer[3] = (ssrc >> 0) & 0xFF;

    // Send buffer over socket, timing out after in case of packet loss
    // Receive 70 byte payload containing external ip and udp portno

    // Let's try retry 5 times if we fail to receive response
    send_ip_discovery_datagram(5, c);

    auto udp_recv_cb = [=](auto &ec, auto transferred) {
        if (ec) {
            c(ec);
        } else if (transferred >= 70) {
            // We got our response, cancel the next send
            timer.cancel();

            // First 4 bytes of buffer should be SSRC, next is udp socket's external IP
            external_ip = std::string((char *) &buffer[4]);

            // Last 2 bytes are udp port (little endian)
            external_port = (buffer[69] << 8) | buffer[68];

            std::cout << "[RTP] udp socket external addresses " << external_ip << ":"
                      << external_port << "\n";
            c({});  // success
        }
    };

    // udp_recv_cb isn't called until the socket is closed, or the data is received
    sock.async_receive(boost::asio::buffer(buffer, buffer.size()), udp_recv_cb);
}

void discord::rtp_session::send_ip_discovery_datagram(int retries, error_cb c)
{
    auto udp_sent_cb = [=](auto &ec, auto) {
        if (ec && ec != boost::asio::error::operation_aborted) {
            std::cerr << "[RTP] could not send udp packet to voice server: " << ec.message()
                      << "\n";
        }
        if (retries == 0) {
            // Failed to receive response in a reasonable time.
            // close the socket to complete the async_receive
            sock.close();

            c(voice_errc::ip_discovery_failed);
            return;
        }
        // Next time expires in 200 ms
        timer.expires_from_now(boost::posix_time::milliseconds(200));
        timer.async_wait([=](auto &ec) {
            if (!ec)
                send_ip_discovery_datagram(retries - 1, c);
        });
    };
    sock.async_send(boost::asio::buffer(buffer.data(), 70), udp_sent_cb);
}

static void write_rtp_header(unsigned char *buffer, uint16_t seq_num, uint32_t timestamp,
                             uint32_t ssrc)
{
    buffer[0] = 0x80;
    buffer[1] = 0x78;

    buffer[2] = (seq_num >> 8) & 0xFF;
    buffer[3] = (seq_num >> 0) & 0xFF;

    buffer[4] = (timestamp >> 24) & 0xFF;
    buffer[5] = (timestamp >> 16) & 0xFF;
    buffer[6] = (timestamp >> 8) & 0xFF;
    buffer[7] = (timestamp >> 0) & 0xFF;

    buffer[8] = (ssrc >> 24) & 0xFF;
    buffer[9] = (ssrc >> 16) & 0xFF;
    buffer[10] = (ssrc >> 8) & 0xFF;
    buffer[11] = (ssrc >> 0) & 0xFF;
}

static void print_rtp_send_info(const boost::system::error_code &ec, size_t transferred)
{
    if (ec) {
        std::cerr << "[RTP] error: " << ec.message() << "\n";
    } else {
        std::cout << "[RTP] " << transferred << " bytes sent\r";
    }
}

void discord::rtp_session::send(audio_frame frame)
{
    auto size = frame.opus_encoded_data.size();
    auto encrypted_len = size + 12 + crypto_secretbox_MACBYTES;

    // Make sure we have enough room to store the encoded audio, 12 bytes for
    // RTP header, crypto_secretbox_MACBYTES (for MAC) in buffer
    if (encrypted_len > buffer.size())
        buffer.resize(encrypted_len);

    auto buf = buffer.data();
    auto write_audio = &buf[12];
    auto nonce = std::array<uint8_t, 24>{};

    write_rtp_header(buf, seq_num, timestamp, ssrc);

    // First 12 bytes of nonce are RTP header, next 12 are 0s
    std::memcpy(&nonce[0], buf, 12);
    std::memset(&nonce[12], 0, 12);

    seq_num++;
    timestamp += frame.frame_count;

    auto error = discord::crypto::xsalsa20_poly1305_encrypt(
        frame.opus_encoded_data.data(), write_audio, size, secret_key.data(), nonce.data());

    if (error) {
        std::cerr << "[RTP] error encrypting data\n";
        return;
    }

    sock.async_send(boost::asio::buffer(buf, encrypted_len), print_rtp_send_info);
}

void discord::rtp_session::set_ssrc(uint32_t ssrc)
{
    this->ssrc = ssrc;
}

void discord::rtp_session::set_secret_key(std::vector<uint8_t> key)
{
    secret_key = std::move(key);
}

const std::string &discord::rtp_session::get_external_ip() const
{
    return external_ip;
}

uint16_t discord::rtp_session::get_external_port() const
{
    return external_port;
}
