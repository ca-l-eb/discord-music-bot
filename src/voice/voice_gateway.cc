#include <boost/asio/connect.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <iostream>
#include <json.hpp>

#include "discord.h"
#include "errors.h"
#include "net/resource_parser.h"
#include "voice/crypto.h"
#include "voice/opus_encoder.h"
#include "voice/voice_gateway.h"
#include "voice/voice_state_listener.h"

static void write_rtp_header(unsigned char *buffer, uint16_t seq_num, uint32_t timestamp,
                             uint32_t ssrc);

discord::voice_gateway::voice_gateway(boost::asio::io_context &ctx, ssl::context &tls,
                                      std::shared_ptr<voice_gateway_entry> e, uint64_t user_id)
    : ctx{ctx}
    , tcp_resolver{ctx}
    , websock{ctx, tls}
    , entry{e}
    , udp_socket{ctx}
    , udp_resolver{ctx}
    , timer{ctx}
    , user_id{user_id}
    , ssrc{0}
    , udp_port{0}
    , buffer(1024)
    , timestamp{(uint32_t) rand()}
    , seq_num{(uint16_t) rand()}
    , beater{ctx}
    , state{connection_state::disconnected}
    , is_speaking{false}
{
    std::cout << "[voice] connecting to gateway " << entry->endpoint << " session_id["
              << entry->session_id << "] token[" << entry->token << "]\n";

    udp_socket.open(udp::v4());
    std::cout << "[voice] created udp socket\n";
}

void discord::voice_gateway::connect(error_cb c)
{
    voice_connect_callback = c;

    // entry->endpoint contains both hostname and (bogus) port, only care about hostname
    auto parsed = resource_parser::parse(entry->endpoint);
    entry->endpoint = std::move(parsed.host);

    tcp::resolver::query query{entry->endpoint, "443"};
    tcp_resolver.async_resolve(
        query, [self = shared_from_this()](auto &ec, auto it) { self->on_resolve(ec, it); });
}

void discord::voice_gateway::on_resolve(const boost::system::error_code &ec,
                                        tcp::resolver::iterator it)
{
    if (ec) {
        throw std::runtime_error("Could not resolve host: " + ec.message());
    }

    boost::asio::async_connect(
        websock.next_layer().lowest_layer(),
        it, [self = shared_from_this()](auto &ec, auto it) { self->on_connect(ec, it); });
}

void discord::voice_gateway::on_connect(const boost::system::error_code &ec,
                                        tcp::resolver::iterator)
{
    if (ec) {
        throw std::runtime_error("Could not connect: " + ec.message());
    }

    websock.next_layer().async_handshake(
        ssl::stream_base::client, [self = shared_from_this()](auto &ec) {
            self->on_tls_handshake(ec);
        });
}

void discord::voice_gateway::on_tls_handshake(const boost::system::error_code &ec)
{
    if (ec) {
        throw std::runtime_error{"TLS handshake error: " + ec.message()};
    }

    websock.async_handshake(entry->endpoint, "/?v=3", [self = shared_from_this()](auto &ec) {
        self->on_websocket_handshake(ec);
    });
}

void discord::voice_gateway::on_websocket_handshake(const boost::system::error_code &ec)
{
    if (ec) {
        std::cerr << "[voice] websocket connect error: " << ec.message() << "\n";
        boost::asio::post(ctx, [&]() { voice_connect_callback(ec); });
    } else {
        std::cout << "[voice] websocket connected\n";
        identify();
    }
}

void discord::voice_gateway::identify()
{
    nlohmann::json identify{{"op", static_cast<int>(voice_op::identify)},
                            {"d",
                             {{"server_id", entry->guild_id},
                              {"user_id", user_id},
                              {"session_id", entry->session_id},
                              {"token", entry->token}}}};
    auto identify_sent_cb = [&](auto &ec, auto) {
        if (ec) {
            std::cout << "[voice] gateway identify error: " << ec.message() << "\n";
            boost::asio::post(ctx, [&]() { voice_connect_callback(ec); });
        } else {
            std::cout << "[voice] starting event loop\n";
            event_loop();
        }
    };
    send(identify.dump(), identify_sent_cb);
}

void discord::voice_gateway::send(const std::string &s, transfer_cb c)
{
    // TODO: use strand + message queue + timer for delay
    boost::system::error_code ec;
    auto wrote = websock.write(boost::asio::buffer(s), ec);
    c(ec, wrote);
}

void discord::voice_gateway::on_read(const boost::system::error_code &ec, size_t transferred)
{
    if (ec) {
        // TODO: if close code, convert to gate ay error code
        std::cerr << "[voice] error reading message: " << ec.message() << "\n";
    } else {
        auto data = boost::beast::buffers_to_string(multi_buffer.data());
        handle_event(data);
        multi_buffer.consume(transferred);
    }
}

void discord::voice_gateway::event_loop()
{
    websock.async_read(multi_buffer, [self = shared_from_this()](auto &ec, auto transferred) {
        self->on_read(ec, transferred);
    });
}

void discord::voice_gateway::handle_event(const std::string &data)
{
    std::cout << "[voice] ";
    std::cout.write(data.c_str(), data.size());
    std::cout << "\n";
    // Parse the results as a json object
    try {
        nlohmann::json json = nlohmann::json::parse(data);
        auto payload = json.get<discord::voice_payload>();

        switch (payload.op) {
            case voice_op::ready:
                extract_ready_info(payload.data);
                break;
            case voice_op::session_description:
                extract_session_info(payload.data);
                break;
            case voice_op::speaking:
                break;
            case voice_op::heartbeat_ack:
                // We should check if the nonce is the same as the one sent by the
                // heartbeater
                beater.on_heartbeat_ack();
                break;
            case voice_op::hello:
                notify_heartbeater_hello(payload.data);
                break;
            case voice_op::resumed:
                // Successfully resumed
                state = connection_state::connected;
                break;
            case voice_op::client_disconnect:
                break;
            default:
                break;
        }
    } catch (std::exception &e) {
        std::cerr << "[voice] gateway error: " << e.what() << "\n";
    }
    event_loop();
}

void discord::voice_gateway::heartbeat()
{
    // TODO: save the nonce (rand()) and check if it is ACKed
    nlohmann::json json{{"op", static_cast<int>(voice_op::heartbeat)}, {"d", rand()}};
    send(json.dump(), ignore_transfer);
}

void discord::voice_gateway::resume()
{
    state = connection_state::disconnected;
    nlohmann::json resumed{{"op", static_cast<int>(voice_op::resume)},
                           {"d",
                            {{"server_id", entry->guild_id},
                             {"session_id", entry->session_id},
                             {"token", entry->token}}}};
    send(resumed.dump(), ignore_transfer);
}

void discord::voice_gateway::extract_ready_info(nlohmann::json &data)
{
    auto ready_info = data.get<discord::voice_ready>();
    ssrc = ready_info.ssrc;
    udp_port = ready_info.port;
    state = connection_state::connected;

    // Prepare buffer for ip discovery
    std::memset(buffer.data(), 0, 70);
    buffer[0] = (this->ssrc >> 24) & 0xFF;
    buffer[1] = (this->ssrc >> 16) & 0xFF;
    buffer[2] = (this->ssrc >> 8) & 0xFF;
    buffer[3] = (this->ssrc >> 0) & 0xFF;

    std::string host;
    // Parse the endpoint url, extracting only the host
    auto parsed_results = resource_parser::parse(entry->endpoint);
    host = parsed_results.host;
    udp::resolver::query query{udp::v4(), host, std::to_string(udp_port)};
    udp_resolver.async_resolve(
        query, [&](const boost::system::error_code &e, udp::resolver::iterator iterator) {
            if (e) {
                boost::asio::post(ctx, [&]() { voice_connect_callback(e); });
            } else {
                send_endpoint = *iterator;
                ip_discovery();
            }
        });
}

void discord::voice_gateway::extract_session_info(nlohmann::json &data)
{
    auto session_info = data.get<discord::voice_session>();
    if (session_info.mode != "xsalsa20_poly1305")
        throw std::runtime_error("Unsupported voice mode: " + session_info.mode);

    secret_key = std::move(session_info.secret_key);

    if (secret_key.size() != 32)
        throw std::runtime_error("Expected 32 byte secret key but got " +
                                 std::to_string(secret_key.size()));

    // We are ready to start speaking!
    boost::asio::post(ctx, [&]() { voice_connect_callback({}); });
}

void discord::voice_gateway::ip_discovery()
{
    // Send buffer over socket, timing out after in case of packet loss
    // Receive 70 byte payload containing external ip and udp portno

    // Let's try retry 5 times if we fail to receive response
    retries = 5;

    send_ip_discovery_datagram();
    auto udp_recv_cb = [&](auto &ec, auto transferred) {
        if (ec) {
            boost::asio::post(ctx, [&]() { voice_connect_callback(ec); });
        } else if (transferred >= 70) {
            // We got our response, cancel the next send
            timer.cancel();

            // First 4 bytes of buffer should be SSRC, next is beginning
            // of this udp socket's external IP
            external_ip = std::string((char *) &buffer[4]);

            // Extract the port the udp socket is on (little-endian)
            uint16_t local_udp_port = (buffer[69] << 8) | buffer[68];

            std::cout << "[voice] udp socket bound at " << external_ip << ":" << local_udp_port
                      << "\n";

            select(local_udp_port);
        }
    };

    // udp_recv_cb isn't called until the socket is closed, or the data is received
    udp_socket.async_receive_from(boost::asio::buffer(buffer, buffer.size()), receive_endpoint,
                                  udp_recv_cb);
}

void discord::voice_gateway::send_ip_discovery_datagram()
{
    auto udp_sent_cb = [&](auto &ec, auto) {
        if (ec && ec != boost::asio::error::operation_aborted) {
            std::cerr << "[voice] could not send udp packet to voice server: " << ec.message()
                      << "\n";
        }
        if (retries == 0) {
            // Alert the caller that we failed
            boost::asio::post(ctx,
                              [&]() { voice_connect_callback(voice_errc::ip_discovery_failed); });
            // close the socket to complete the async_receive_from
            udp_socket.close();
            return;
        }
        retries--;
        auto timer_cb = [&](auto &e) {
            if (!e)
                send_ip_discovery_datagram();
        };
        // Next time expires in 200 ms
        timer.expires_from_now(boost::posix_time::milliseconds(200));
        timer.async_wait(timer_cb);
    };
    udp_socket.async_send_to(boost::asio::buffer(buffer.data(), 70), send_endpoint, udp_sent_cb);
}

void discord::voice_gateway::select(uint16_t local_udp_port)
{
    nlohmann::json select_payload{
        {"op", static_cast<int>(voice_op::select_proto)},
        {"d",
         {{"protocol", "udp"},
          {"data",
           {{"address", external_ip}, {"port", local_udp_port}, {"mode", "xsalsa20_poly1305"}}}}}};

    send(select_payload.dump(), ignore_transfer);
}

void discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
{
    // Override the heartbeat_interval with value 75% of current
    // This is a bug with Discord apparently
    if (data["heartbeat_interval"].is_number()) {
        int val = data.at("heartbeat_interval").get<int>();
        val = (val / 4) * 3;
        data["heartbeat_interval"] = val;
        beater.on_hello(data, *this);
    }
}

static void do_speak(discord::voice_gateway &vg, transfer_cb c, bool speak)
{
    // Apparently this _doesnt_ need the ssrc
    nlohmann::json speaking_payload{{"op", static_cast<int>(discord::voice_op::speaking)},
                                    {"d", {{"speaking", speak}, {"delay", 0}}}};
    vg.send(speaking_payload.dump(), c);
}

void discord::voice_gateway::start_speaking(transfer_cb c)
{
    do_speak(*this, c, true);
}

void discord::voice_gateway::stop_speaking(transfer_cb c)
{
    do_speak(*this, c, false);
}

void discord::voice_gateway::play(audio_frame frame)
{
    if (!is_speaking) {
        auto speak_sent_cb = [=](auto &ec, auto) {
            if (!ec) {
                std::cout << "[voice] now speaking\n";
                is_speaking = true;
                send_audio(frame);
            }
        };
        start_speaking(speak_sent_cb);
    } else {
        send_audio(frame);
    }
}

void discord::voice_gateway::stop()
{
    is_speaking = false;
    std::cout << "[voice] stopped speaking\n";
    stop_speaking(ignore_transfer);
}

static void print_rtp_send_info(const boost::system::error_code &ec, size_t transferred)
{
    if (ec) {
        std::cerr << "[RTP] error: " << ec.message() << "\n";
    } else {
        std::cout << "[RTP] " << transferred << " bytes sent\r";
    }
}

void discord::voice_gateway::send_audio(audio_frame frame)
{
    auto size = frame.opus_encoded_data.size();
    auto encrypted_len = size + 12 + crypto_secretbox_MACBYTES;

    // Make sure we have enough room to store the encoded audio, 12 bytes for RTP header,
    // crypto_secretbo_MACBYTES (for MAC) in buffer
    if (encrypted_len > buffer.size())
        buffer.resize(size + 12 + crypto_secretbox_MACBYTES);

    uint8_t *buf = buffer.data();
    auto write_audio = &buf[12];
    uint8_t nonce[24];

    write_rtp_header(buf, seq_num, timestamp, ssrc);

    // First 12 bytes of nonce are RTP header, next 12 are 0s
    std::memcpy(nonce, buf, 12);
    std::memset(&nonce[12], 0, 12);

    seq_num++;
    timestamp += frame.frame_count;

    auto error = discord::crypto::xsalsa20_poly1305_encrypt(
        frame.opus_encoded_data.data(), write_audio, size, secret_key.data(), nonce);

    if (error) {
        std::cerr << "[voice] error encrypting data\n";
        return;  // There was a problem encrypting the data
    }

    udp_socket.async_send_to(boost::asio::buffer(buf, encrypted_len), send_endpoint,
                             print_rtp_send_info);
}

static void write_rtp_header(unsigned char *buffer, uint16_t seq_num, uint32_t timestamp,
                             uint32_t ssrc)
{
    buffer[0] = 0x80;
    buffer[1] = 0x78;

    buffer[2] = (seq_num & 0xFF00) >> 8;
    buffer[3] = (seq_num & 0x00FF);

    buffer[4] = (timestamp & 0xFF000000) >> 24;
    buffer[5] = (timestamp & 0x00FF0000) >> 16;
    buffer[6] = (timestamp & 0x0000FF00) >> 8;
    buffer[7] = (timestamp & 0x000000FF);

    buffer[8] = (ssrc & 0xFF000000) >> 24;
    buffer[9] = (ssrc & 0x00FF0000) >> 16;
    buffer[10] = (ssrc & 0x0000FF00) >> 8;
    buffer[11] = (ssrc & 0x000000FF);
}
