#include <boost/process.hpp>
#include <iostream>
#include <json.hpp>

#include <net/resource_parser.h>
#include <opcodes.h>
#include <voice/crypto.h>
#include <voice/opus_encoder.h>
#include <voice/voice_gateway.h>
#include <voice/voice_state_listener.h>

discord::voice_gateway::voice_gateway(boost::asio::io_context &ctx,
                                           discord::voice_gateway_entry &e,
                                           std::string user_id)
    : ctx{ctx}
    , websock{ctx}
    , sender{ctx, websock, 500}
    , entry{e}
    , socket{ctx}
    , resolver{ctx}
    , timer{ctx}
    , user_id{user_id}
    , ssrc{0}
    , udp_port{0}
    , buffer(512)
    , timestamp{(uint32_t) rand()}
    , seq_num{(uint16_t) rand()}
    , state{connection_state::disconnected}
{
    std::cerr << "Connecting to voice gateway='" << entry.endpoint << "' user_id='" << user_id
              << " 'session_id='" << entry.session_id << "' token='" << entry.token << "'\n";

    socket.open(boost::asio::ip::udp::v4());
    std::cout << "Created udp socket\n";
}

discord::voice_gateway::~voice_gateway()
{
    std::cout << "Voice gateway destructor\n";
}

void discord::voice_gateway::connect(boost::asio::ip::tcp::resolver &resolver,
                                          connect_callback c)
{
    callback = c;
    // Make sure we're using voice gateway v3
    auto connect_callback = [&](const boost::system::error_code &e, size_t transferred) {
        if (e) {
            ctx.post([&]() { callback(e); });
            std::cerr << "WebSocket connect error: " << e.message() << "\n";
        } else {
            std::cout << "WebSocket connected\n";
            on_connect(e, transferred);
        }
    };
    websock.async_connect("wss://" + entry.endpoint + "/?v=3", resolver, connect_callback);
}

void discord::voice_gateway::on_connect(const boost::system::error_code &e, size_t)
{
    if (e) {
        ctx.post([&](){callback(e); }); 
    } else {
        // Sucessfully connected
        beater = std::make_unique<discord::heartbeater>(ctx, *this);
        identify();
    }
}

void discord::voice_gateway::identify()
{
    nlohmann::json identify{{"op", static_cast<int>(gtw_voice_op_send::identify)},
                            {"d",
                             {{"server_id", entry.guild_id},
                              {"user_id", user_id},
                              {"session_id", entry.session_id},
                              {"token", entry.token}}}};
    send(identify.dump(), [&](const boost::system::error_code &e, size_t) {
        if (e) {
            std::cout << "Voice gateway identify error: " << e.message() << "\n";
            ctx.post([&]() { callback(e); });
        } else {
            std::cout << "Starting event loop\n";
            event_loop();
        }
    });
}

void discord::voice_gateway::send(const std::string &s,
                                       message_sent_callback c)
{
    sender.safe_send(s, c);
}

void discord::voice_gateway::event_loop()
{
    websock.async_next_message([&](const boost::system::error_code &e, const uint8_t *data,
                                     size_t len) {
        if (e) {
            if (e == websocket_error::websocket_connection_closed) {
                auto code = websock.close_code();
                ctx.post([&]() {
                    callback(make_error_code(static_cast<voice_gateway::error>(code)));
                });
            } else {
                std::cerr << "voice gateway websocket read error: " << e.message() << "\n";
                ctx.post([&]() { callback(e); });
            }
        }
        const char *begin = reinterpret_cast<const char *>(data);
        const char *end = begin + len;

        std::cout << "VOICE GATEWAY: ";
        std::cout.write(begin, len);
        std::cout << "\n";
        // Parse the results as a json object
        try {
            nlohmann::json json = nlohmann::json::parse(begin, end);

            auto op = json["op"];
            auto payload_data = json["d"];

            if (op.is_number()) {
                auto gateway_op = static_cast<gtw_voice_op_recv>(op.get<int>());
                switch (gateway_op) {
                    case gtw_voice_op_recv::ready:
                        extract_ready_info(payload_data);
                        break;
                    case gtw_voice_op_recv::session_description:
                        extract_session_info(payload_data);
                        break;
                    case gtw_voice_op_recv::speaking:
                        break;
                    case gtw_voice_op_recv::heartbeat_ack:
                        // We should check if the nonce is the same as the one sent by the
                        // heartbeater
                        beater->on_heartbeat_ack();
                        break;
                    case gtw_voice_op_recv::hello:
                        notify_heartbeater_hello(payload_data);
                        break;
                    case gtw_voice_op_recv::resumed:
                        // Successfully resumed
                        state = connection_state::connected;
                        break;
                    case gtw_voice_op_recv::client_disconnect:
                        break;
                }
            } else {
                // Discord reference says the hello opcode 8 doesn't contain an 'op' field, so
                // the above if statement will fail, but upon testing, it does have the 'op'. So
                // I resorted to checking both just in case
                auto interval = json["heartbeat_interval"];
                if (interval.is_object()) {
                    notify_heartbeater_hello(interval);
                }
            }
            // No error, do the loop again
            event_loop();
            } catch (std::exception &e) {
                std::cerr << "Error in voice gateway: " << e.what() << "\n";
            }
        }
    );
}

void discord::voice_gateway::heartbeat()
{
    // TODO: save the nonce (rand()) and check if it is ACKed
    nlohmann::json json{{"op", static_cast<int>(gtw_voice_op_send::heartbeat)}, {"d", rand()}};
    send(json.dump(), print_info);
}

void discord::voice_gateway::resume()
{
    state = connection_state::disconnected;
    nlohmann::json resumed{{"op", static_cast<int>(gtw_voice_op_send::resume)},
                           {"d",
                            {{"server_id", entry.guild_id},
                             {"session_id", entry.session_id},
                             {"token", entry.token}}}};
    send(resumed.dump(), print_info);
}

void discord::voice_gateway::extract_ready_info(nlohmann::json &data)
{
    auto ssrc = data["ssrc"];
    auto port = data["port"];

    if (ssrc.is_number())
        this->ssrc = ssrc.get<uint32_t>();
    if (port.is_number())
        this->udp_port = port.get<uint16_t>();

    state = connection_state::connected;

    // Prepare buffer for ip discovery
    std::memset(buffer.data(), 0, 70);
    buffer[0] = (this->ssrc & 0xFF000000) >> 24;
    buffer[1] = (this->ssrc & 0x00FF0000) >> 16;
    buffer[2] = (this->ssrc & 0x0000FF00) >> 8;
    buffer[3] = (this->ssrc & 0x000000FF);

    std::string host;
    // Parse the endpoint url, extracting only the host
    auto parsed_results = resource_parser::parse(entry.endpoint);
    host = parsed_results.host;
    boost::asio::ip::udp::resolver::query query{boost::asio::ip::udp::v4(), host,
                                                std::to_string(udp_port)};
    resolver.async_resolve(query, [&](const boost::system::error_code &e,
                                      boost::asio::ip::udp::resolver::iterator iterator) {
        if (e) {
            ctx.post([&]() { callback(e); });
        } else {
            send_endpoint = *iterator;
            ip_discovery();
        }
    });
}

void discord::voice_gateway::extract_session_info(nlohmann::json &data)
{
    auto mode = data["mode"];
    auto key = data["secret_key"];

    if (mode.is_string())
        if (mode.get<std::string>() != "xsalsa20_poly1305")
            throw std::runtime_error("Unsupported voice mode: " + mode.get<std::string>());

    if (key.is_array())
        secret_key = key.get<std::vector<uint8_t>>();

    if (secret_key.size() != 32)
        throw std::runtime_error("Expected 32 byte secret key but got " +
                                 std::to_string(secret_key.size()));

    // We are ready to start speaking!
    ctx.post([&]() { callback({}); });
}

void discord::voice_gateway::ip_discovery()
{
    // Send buffer over socket, timing out after in case of packet loss
    // Receive 70 byte payload containing external ip and udp portno
    retries = 5;
    timer.expires_from_now(boost::posix_time::milliseconds(0));
    timer.async_wait([&](const boost::system::error_code &e) {
        if (!e)
            send_ip_discovery_datagram();
    });

    socket.async_receive_from(
        boost::asio::buffer(buffer, buffer.size()), receive_endpoint,
        [&](const boost::system::error_code &e, size_t transferred) {
            if (e) {
                ctx.post([&]() { callback(e); });
            } else if (transferred >= 70) {
                // We got our response, cancel the next send
                timer.cancel();

                // First 4 bytes of buffer should be SSRC, next is beginning of this udp socket's
                // external IP
                external_ip = std::string((char *) &buffer[4]);

                // Extract the port the udp socket is on (little-endian)
                uint16_t local_udp_port = (buffer[69] << 8) | buffer[68];

                std::cout << "UDP socket bound at " << external_ip << ":" << local_udp_port << "\n";

                select(local_udp_port);
            }
        });
}

void discord::voice_gateway::send_ip_discovery_datagram()
{
    socket.async_send_to(
        boost::asio::buffer(buffer.data(), 70), send_endpoint,
        [&](const boost::system::error_code &e, size_t) {
            if (e) {
                std::cerr << "Could not send udp packet to voice server: " << e.message() << "\n";
            }
            if (retries == 0) {
                ctx.post([&]() {
                    callback(make_error_code(error::ip_discovery_failed));
                });
                socket.close();
                return;
            }
            retries--;
            // Next time expires in 200 ms
            timer.expires_from_now(boost::posix_time::milliseconds(200));
            timer.async_wait([&](const boost::system::error_code &e) {
                if (!e)
                    send_ip_discovery_datagram();
            });
        });
}

void discord::voice_gateway::select(uint16_t local_udp_port)
{
    nlohmann::json select_payload{
        {"op", static_cast<int>(gtw_voice_op_send::select_proto)},
        {"d",
         {{"protocol", "udp"},
          {"data",
           {{"address", external_ip}, {"port", local_udp_port}, {"mode", "xsalsa20_poly1305"}}}}}};

    send(select_payload.dump(), print_info);
}

void discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
{
    // Override the heartbeat_interval with value 75% of current
    // This is a bug with Discord apparently
    if (data["heartbeat_interval"].is_number()) {
        int val = data.at("heartbeat_interval").get<int>();
        val = (val / 4) * 3;
        data["heartbeat_interval"] = val;
        beater->on_hello(data);
    }
}

void discord::voice_gateway::start_speaking(message_sent_callback c)
{
    // Apparently this _doesnt_ need the ssrc
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", true}, {"delay", 0}}}};
    send(speaking_payload.dump(), c);
}

void discord::voice_gateway::stop_speaking(message_sent_callback c)
{
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", false}, {"delay", 0}}}};
    send(speaking_payload.dump(), c);
}

void discord::voice_gateway::play(const uint8_t *opus_encoded, size_t encoded_len,
                                       size_t frame_size)
{
    if (!is_speaking) {
        is_speaking = true;
        start_speaking([=](const boost::system::error_code &e, size_t) {
            if (!e) {
                send_audio(opus_encoded, encoded_len, frame_size);
            }
        });
    } else {
        send_audio(opus_encoded, encoded_len, frame_size);
    }
}

void discord::voice_gateway::stop()
{
    is_speaking = false;
    stop_speaking([](const boost::system::error_code &, size_t) {});
}

void discord::voice_gateway::send_audio(const uint8_t *opus_encoded, size_t encoded_len,
                                             size_t frame_size)
{
    uint8_t *buf = buffer.data();
    auto write_audio = &buf[12];
    uint8_t nonce[24];

    write_header(buf, seq_num, timestamp);

    // First 12 bytes of nonce are RTP header, next 12 are 0s
    std::memcpy(nonce, buf, 12);
    std::memset(&nonce[12], 0, 12);

    seq_num++;
    timestamp += frame_size;

    auto error = discord::crypto::xsalsa20_poly1305_encrypt(
            opus_encoded, write_audio, encoded_len, secret_key.data(), nonce);

    if (error) {
        std::cerr << "Error encrypting data\n";
        return;  // There was a problem encrypting the data
    }

    // Make sure we also count the RTP header and the MAC from encrypting
    encoded_len += 12 + crypto_secretbox_MACBYTES;

    socket.async_send_to(boost::asio::buffer(buf, encoded_len), send_endpoint,
                         [](const boost::system::error_code &, size_t) {});
    std::cout << "[RTP] " << encoded_len << " bytes sent\r";
}

void discord::voice_gateway::write_header(unsigned char *buffer, uint16_t seq_num,
                                               uint32_t timestamp)
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
