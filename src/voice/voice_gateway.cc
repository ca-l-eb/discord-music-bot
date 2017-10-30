#include <boost/bind.hpp>
#include <boost/process.hpp>
#include <iostream>
#include <json.hpp>

#include <net/resource_parser.h>
#include <opcodes.h>
#include <voice/crypto.h>
#include <voice/opus_encoder.h>
#include <voice/voice_gateway.h>
#include <voice/voice_state_listener.h>

cmd::discord::voice_gateway::voice_gateway(boost::asio::io_service &service,
                                           cmd::discord::voice_gateway_entry &e,
                                           std::string user_id)
    : service{service}
    , websocket{service}
    , sender{service, websocket, 500}
    , entry{e}
    , socket{service}
    , resolver{service}
    , timer{service}
    , user_id{user_id}
    , ssrc{0}
    , udp_port{0}
    , buffer(512)
    , state{connection_state::disconnected}
{
    std::cerr << "Connecting to voice gateway='" << entry.endpoint << "' user_id='" << user_id
              << " 'session_id='" << entry.session_id << "' token='" << entry.token << "'\n";

    socket.open(boost::asio::ip::udp::v4());
    // Make sure we're using voice gateway v3
    websocket.async_connect("wss://" + entry.endpoint + "/?v=3",
                            [&](const boost::system::error_code &e, size_t transferred) {
                                on_connect(e, transferred);
                            });
}

cmd::discord::voice_gateway::~voice_gateway()
{
    std::cout << "Voice gateway destructor\n";
}

void cmd::discord::voice_gateway::on_connect(const boost::system::error_code &e, size_t)
{
    if (e) {
        std::cerr << "Voice gateway connect error: " << e.message() << "\n";
    } else {
        // Sucessfully connected
        beater = std::make_unique<cmd::discord::heartbeater>(service, *this);
        identify();
    }
}

void cmd::discord::voice_gateway::identify()
{
    nlohmann::json identify{{"op", static_cast<int>(gtw_voice_op_send::identify)},
                            {"d",
                             {{"server_id", entry.guild_id},
                              {"user_id", user_id},
                              {"session_id", entry.session_id},
                              {"token", entry.token}}}};
    send(identify.dump(), [&](const boost::system::error_code &e, size_t) {
        if (e) {
            throw std::runtime_error("Could not connect to voice gateway: " + e.message());
        }
        event_loop();
    });
}

void cmd::discord::voice_gateway::send(const std::string &s,
                                       cmd::websocket::message_sent_callback c)
{
    sender.safe_send(s, c);
}

void cmd::discord::voice_gateway::event_loop()
{
    websocket.async_next_message(
        [&](const boost::system::error_code &e, const uint8_t *data, size_t len) {
            if (e) {
                std::cerr << "Voice gateway websocket error: " << e.message() << "\n";
                return;
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
        });
}

void cmd::discord::voice_gateway::heartbeat()
{
    // TODO: save the nonce (rand()) and check if it is ACKed
    nlohmann::json json{{"op", static_cast<int>(gtw_voice_op_send::heartbeat)}, {"d", rand()}};
    send(json.dump(), print_info);
}

void cmd::discord::voice_gateway::resume()
{
    state = connection_state::disconnected;
    nlohmann::json resumed{{"op", static_cast<int>(gtw_voice_op_send::resume)},
                           {"d",
                            {{"server_id", entry.guild_id},
                             {"session_id", entry.session_id},
                             {"token", entry.token}}}};
    send(resumed.dump(), print_info);
}

void cmd::discord::voice_gateway::extract_ready_info(nlohmann::json &data)
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
    std::tie(std::ignore, host, std::ignore, std::ignore) =
        cmd::resource_parser::parse(entry.endpoint);
    boost::asio::ip::udp::resolver::query query{boost::asio::ip::udp::v4(), host,
                                                std::to_string(udp_port)};
    resolver.async_resolve(query, [&](const boost::system::error_code &e,
                                      boost::asio::ip::udp::resolver::iterator iterator) {
        if (e) {
            std::cerr << "Could not resolve udp " << host << ": " << e.message() << "\n";
        } else {
            timer.cancel();
            send_endpoint = *iterator;
            ip_discovery();
        }
    });
}

void cmd::discord::voice_gateway::extract_session_info(nlohmann::json &data)
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
}

void cmd::discord::voice_gateway::ip_discovery()
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
                std::cerr << "Error receiving udp datagram: " << e.message() << "\n";
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

void cmd::discord::voice_gateway::send_ip_discovery_datagram()
{
    socket.async_send_to(
        boost::asio::buffer(buffer.data(), 70), send_endpoint,
        [&](const boost::system::error_code &e, size_t transferred) {
            if (e) {
                std::cerr << "Could not send udp packet to voice server: " << e.message() << "\n";
            }
            if (retries == 0) {
                std::cerr << "No response from Discord IP discover request\n";
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

void cmd::discord::voice_gateway::select(uint16_t local_udp_port)
{
    nlohmann::json select_payload{
        {"op", static_cast<int>(gtw_voice_op_send::select_proto)},
        {"d",
         {{"protocol", "udp"},
          {"data",
           {{"address", external_ip}, {"port", local_udp_port}, {"mode", "xsalsa20_poly1305"}}}}}};

    send(select_payload.dump(), print_info);
}

void cmd::discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
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

void cmd::discord::voice_gateway::start_speaking()
{
    // Apparently this _doesnt_ need the ssrc
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", true}, {"delay", 0}}}};
    send(speaking_payload.dump(), print_info);
}

void cmd::discord::voice_gateway::stop_speaking()
{
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", false}, {"delay", 0}}}};
    send(speaking_payload.dump(), print_info);
}

void cmd::discord::voice_gateway::play_audio(const std::string &youtube_url)
{
    //    cmd::inet_addr other;
    //    unsigned char buffer[512];
    //    while (true) {
    //        auto read = udp_socket.recv(other, buffer, sizeof(buffer), 0, 5000);
    //        std::cout << "Read " << read << " bytes\n";
    //        if (read == 0)
    //            return;
    //        auto flags = std::cerr.flags();
    //        for (int i = 0; i < read; i++) {
    //            std::cerr << std::setw(3) << std::hex << (int) buffer[i];
    //        }
    //        std::cerr.flags(flags);
    //        std::cerr << "\n";
    //    }
    std::cerr << "Using " << youtube_url << "\n";
    boost::process::pipe audio_transport_pipe;
    boost::process::pipe audio_read_src;

    boost::process::child youtube_dl{"youtube-dl -f 251 -o - " + youtube_url,
                                     boost::process::std_out > audio_transport_pipe,
                                     boost::process::std_err > boost::process::null};

    boost::process::child ffmpeg{
        "ffmpeg -i - -ac 2 -f s16le -", boost::process::std_out > audio_read_src,
        boost::process::std_in<audio_transport_pipe, boost::process::std_err> boost::process::null};

    start_speaking();

    // 960 samples per channel per datagram we send. At 48000 Hz, this is 20ms
    const int frame_size = 960;
    const int channels = 2;
    opus_encoder encoder{channels, 48000};

    uint8_t rtp_buffer[512];
    uint8_t opus_encoded_buffer[512];
    int16_t read_buffer[frame_size * channels];
    auto write_audio = &rtp_buffer[12];

    uint32_t timestamp = (uint32_t) rand();
    uint16_t seq_num = (uint16_t) rand();
    uint8_t nonce[24];
    // Set the lower 12 bytes of nonce to 0
    std::memset(&nonce[12], 0, 12);

    std::chrono::system_clock::time_point prev;
    while (true) {
        std::cerr << "[" << timestamp << "] ";
        std::cerr << "[" << seq_num << "] ";
        write_header(rtp_buffer, seq_num, timestamp);
        seq_num++;
        timestamp += frame_size;

        // Copy the RTP header to first 12 bytes of nonce
        std::memcpy(nonce, rtp_buffer, 12);

        auto read = audio_read_src.read((char *) read_buffer, sizeof(read_buffer));
        if (read == 0 || read == -1) {
            std::cerr << "\nAUDIO STREAM COMPLETE\n";
            break;  // Stream is done
        }

        std::cerr << "audio-read=" << read << " ";

        if ((size_t) read < sizeof(read_buffer)) {
            // Fill the rest of the buffer with 0
            std::memset(read_buffer + read, 0, sizeof(read_buffer) - read);
        }

        int encoded_len = encoder.encode(read_buffer, frame_size, opus_encoded_buffer,
                                         sizeof(opus_encoded_buffer));
        std::cerr << "encoded-len=" << encoded_len << " ";

        std::cerr << "[";
        auto flags = std::cerr.flags();
        for (int i = 0; i < 12; i++)
            std::cerr << std::setw(3) << std::hex << (int) rtp_buffer[i];
        std::cerr.flags(flags);
        std::cerr << " ]";

        auto error = cmd::discord::crypto::xsalsa20_poly1305_encrypt(
            opus_encoded_buffer, write_audio, encoded_len, secret_key.data(), nonce);

        if (error) {
            std::cerr << " ENCRYPT FAILED ";
        }

        // Wait a while before sending next packet
        std::this_thread::sleep_for(std::chrono::milliseconds(17));

        /*
        auto wrote = udp_socket.send(voice_addr, rtp_buffer,
                                     12 + encoded_len + crypto_secretbox_MACBYTES, 0);
        if (wrote != 12 + encoded_len + crypto_secretbox_MACBYTES) {
            std::cerr << "\nCould not send entire RTP frame. Sent " << wrote << " out of "
                      << (12 + encoded_len) << " bytes\n";
            break;
        }
        std::cerr << " wrote " << wrote << " " << voice_addr.to_string() << " "
                  << voice_addr.get_port() << "\n";
        std::cerr.flush();
         */
    }
    stop_speaking();

    if (youtube_dl.joinable())
        youtube_dl.join();
    if (ffmpeg.joinable())
        ffmpeg.join();
}

void cmd::discord::voice_gateway::write_header(unsigned char *buffer, uint16_t seq_num,
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
