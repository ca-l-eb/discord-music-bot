#include <cmd/resource_parser.h>
#include <boost/process.hpp>
#include <iostream>
#include <json.hpp>

#include "crypto.h"
#include <sodium/crypto_secretbox.h>
#include "opus_encoder.h"
#include "voice_gateway.h"

cmd::discord::voice_gateway::voice_gateway(const std::string &url, const std::string &user_id,
                                           const std::string &session_id,
                                           const std::string &guild_id, const std::string &token)
    : websocket{}
    , url{url}
    , user_id{user_id}
    , session_id{session_id}
    , guild_id{guild_id}
    , token{token}
    , state{connection_state::disconnected}
    , ssrc{0}
    , udp_port{0}
    , udp_socket{cmd::inet_family::ipv4}

{
    std::cerr << "Connecting to voice gateway='" << url << "' user_id='" << user_id
              << "' session_id='" << session_id << "' token='" << token << "'\n";

    // Make sure we're using voice gateway v3
    websocket.connect("wss://" + url + "/?v=3");
    identify();
}

void cmd::discord::voice_gateway::next_event()
{
    // Read the next message from the WebSocket and place it in buffer
    auto read = websocket.next_message(buffer);
    if (read == 0) {
        throw std::runtime_error("WebSocket connection interrupted");
    }
    std::cout << "VOICE GATEWAY: ";
    std::cout.write((char *) buffer.data(), read);
    std::cout << "\n";
    // Parse the results as a json object
    auto json = nlohmann::json::parse(buffer.begin(), buffer.end());

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
                // We should check if the nonce is the same as the one sent by the heartbeater
                beater.on_heartbeat_ack();
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
        // Discord reference says the hello opcode 8 doesn't contain an 'op' field, so the above if
        // statement will fail, but upon testing, it does have the 'op'. So I resorted to checking
        // both just in case
        auto interval = json["heartbeat_interval"];
        if (interval.is_object()) {
            notify_heartbeater_hello(interval);
        }
    }
}

void cmd::discord::voice_gateway::heartbeat()
{
    // TODO: save the nonce (rand()) and check if it is ACKed
    nlohmann::json json{{"op", static_cast<int>(gtw_voice_op_send::heartbeat)}, {"d", rand()}};
    safe_send(json.dump());
}

void cmd::discord::voice_gateway::safe_send(const std::string &s)
{
    std::lock_guard<std::mutex> guard{write_mutex};
    // Rate limit gateway messages, allow 1 message every 0.5 seconds
    auto now = clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_msg_sent);
    if (diff.count() < 500) {
        std::this_thread::sleep_for(diff);
        last_msg_sent = now + diff;
    } else {
        last_msg_sent = now;
    }
    websocket.send(s);
    std::cout << "Voice gateway sent: " << s << "\n";
}

void cmd::discord::voice_gateway::identify()
{
    nlohmann::json identify{{"op", static_cast<int>(gtw_voice_op_send::identify)},
                            {"d",
                             {{"server_id", guild_id},
                              {"user_id", user_id},
                              {"session_id", session_id},
                              {"token", token}}}};
    safe_send(identify.dump());
}

void cmd::discord::voice_gateway::resume()
{
    state = connection_state::disconnected;
    nlohmann::json resumed{
        {"op", static_cast<int>(gtw_voice_op_send::resume)},
        {"d", {{"server_id", guild_id}, {"session_id", session_id}, {"token", token}}}};
    safe_send(resumed.dump());
}

void cmd::discord::voice_gateway::extract_ready_info(nlohmann::json &data)
{
    auto ssrc = data["ssrc"];
    auto port = data["port"];
    auto modes = data["modes"];

    if (ssrc.is_number())
        this->ssrc = ssrc.get<uint32_t>();
    if (port.is_number())
        this->udp_port = port.get<uint16_t>();

    state = connection_state::connected;

    ip_discovery();
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
        throw std::runtime_error("Expected 32 byte secret key but got " + std::to_string(secret_key.size()));

    audio_thread = std::thread{&voice_gateway::play_audio, this,
                               "https://www.youtube.com/watch?v=iTuhpJBBvCc"};
}

void cmd::discord::voice_gateway::ip_discovery()
{
    std::string host;
    // Parse the url, extracting only the host
    std::tie(std::ignore, host, std::ignore, std::ignore) = cmd::resource_parser::parse(url);
    // Connect to the host on the port specified by the Voice Ready Payload
    cmd::inet_resolver resolver{host.c_str(), udp_port, cmd::inet_proto::udp,
                                cmd::inet_family::ipv4};
    voice_addr = resolver.addresses.front();

    std::cout << "Voice server located at " << voice_addr.to_string() << ":"
              << voice_addr.get_port() << "\n";

    unsigned char buffer[70];
    std::memset(buffer, 0, sizeof(buffer));

    // Write the SSRC
    buffer[0] = (ssrc & 0xFF000000) >> 24;
    buffer[1] = (ssrc & 0x00FF0000) >> 16;
    buffer[2] = (ssrc & 0x0000FF00) >> 8;
    buffer[3] = (ssrc & 0x000000FF);

    int retries = 0;
    ssize_t ret;
    while (true) {
        ret = udp_socket.send(voice_addr, buffer, sizeof(buffer));
        if (ret != 70)
            throw std::runtime_error("UDP error, could not send datagram");
        ret = udp_socket.recv(buffer, sizeof(buffer), 0, 1000);
        if (ret == 70)  // We got our response
            break;
        if (retries++ == 4)
            throw std::runtime_error("IP discovery failed, no response from Discord");
    }

    // First 4 bytes of buffer should be SSRC, next is beginning of this udp socket's external IP
    external_ip = std::string((char *) &buffer[4]);

    // Extract the port the udp socket is on (little-endian)
    uint16_t local_udp_port = (buffer[69] << 8) | buffer[68];

    std::cout << "UDP socket bound at " << external_ip << ":" << local_udp_port << "\n";

    select(local_udp_port);
}

void cmd::discord::voice_gateway::select(uint16_t local_udp_port)
{
    nlohmann::json select_payload{
        {"op", static_cast<int>(gtw_voice_op_send::select_proto)},
        {"d",
         {{"protocol", "udp"},
          {"data",
           {{"address", external_ip}, {"port", local_udp_port}, {"mode", "xsalsa20_poly1305"}}}}}};

    safe_send(select_payload.dump());
}

void cmd::discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
{
    // Override the heartbeat_interval with value 75% of current
    // This is a bug with Discord apparently
    if (data["heartbeat_interval"].is_number()) {
        int val = data.at("heartbeat_interval").get<int>();
        val = (val / 4) * 3;
        data["heartbeat_interval"] = val;
        beater.on_hello(*this, data);
    }
}

void cmd::discord::voice_gateway::start_speaking()
{
    // Apparently this _doesnt_ need the ssrc
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", true}, {"delay", 0}}}};
    safe_send(speaking_payload.dump());
}

void cmd::discord::voice_gateway::stop_speaking()
{
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", false}, {"delay", 0}}}};
    safe_send(speaking_payload.dump());
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
        boost::process::std_in < audio_transport_pipe, boost::process::std_err > boost::process::null};

    start_speaking();
    stop_speaking();
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

        if (read < sizeof(read_buffer)) {
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

        auto wrote = udp_socket.send(voice_addr, rtp_buffer, 12 + encoded_len + crypto_secretbox_MACBYTES, 0);
        if (wrote != 12 + encoded_len + crypto_secretbox_MACBYTES) {
            std::cerr << "\nCould not send entire RTP frame. Sent " << wrote << " out of "
                      << (12 + encoded_len) << " bytes\n";
            break;
        }
        std::cerr << " wrote " << wrote << " " << voice_addr.to_string() << " " << voice_addr.get_port() << "\n";
        std::cerr.flush();

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

    buffer[8] =  (ssrc & 0xFF000000) >> 24;
    buffer[9] =  (ssrc & 0x00FF0000) >> 16;
    buffer[10] = (ssrc & 0x0000FF00) >> 8;
    buffer[11] = (ssrc & 0x000000FF);
}
