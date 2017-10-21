#include <cmd/resource_parser.h>
#include <cmd/udp_socket.h>
#include <iostream>
#include <json.hpp>

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

{
    std::cout << "Connecting to voice gateway " << url << " using user_id: " << user_id
              << " session_id: " << session_id << " token: " << token << "\n";

    // Make sure we're using voice gateway v3
    websocket.connect(url + "/?v=3");
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

    if (modes.is_array())
        this->voice_modes = modes.get<std::vector<std::string>>();

    for (auto &mode : voice_modes)
        std::cout << "Supported mode: " << mode << "\n";

    state = connection_state::connected;

    ip_discovery();
}

void cmd::discord::voice_gateway::extract_session_info(nlohmann::json &data)
{
    auto mode = data["mode"];
    auto key = data["secret_key"];

    if (mode.is_string())
        voice_mode_using = mode.get<std::string>();

    if (key.is_array())
        secret_key = key.get<std::vector<uint8_t>>();

    std::cout << "Secret key:\n";
    auto flags = std::cout.flags();
    for (auto byte : secret_key)
        std::cout << std::setw(3) << std::hex << byte;
    std::cout.flags(flags);
    std::cout << "\n";

    if (secret_key.size() != 32)
        std::cerr << "SECRET KEY SHOULD BE 32 BYTES BUT WAS " << secret_key.size() << "\n";

    std::cout << "READY FOR COMMUNICATION\n";
}

void cmd::discord::voice_gateway::ip_discovery()
{
    std::string host;
    std::tie(std::ignore, host, std::ignore, std::ignore) = cmd::resource_parser::parse(url);
    cmd::udp_socket udp_sock{cmd::inet_family::ipv4};
    cmd::inet_resolver resolver{host.c_str(), udp_port, cmd::inet_proto::udp,
                                cmd::inet_family::ipv4};
    cmd::inet_addr addr = resolver.addresses.front();
    cmd::inet_addr other;
    std::cout << "Voice server located at " << addr.to_string() << " " << addr.get_port() << "\n";

    unsigned char buffer[70];
    std::memset(buffer, 0, sizeof(buffer));

    // Write the SSRC
    uint32_t *buf_i32 = (uint32_t *) &buffer[0];
    *buf_i32 = ssrc;

    auto ret = udp_sock.send(addr, buffer, sizeof(buffer));
    std::cout << "Sent " << ret << " bytes\n";
    auto flags = std::cout.flags();
    for (int i = 0; i < sizeof(buffer); i++)
        std::cout << std::setw(3) << std::hex << (int) buffer[i];

    std::cout.flags(flags);
    std::cout << "\n";

    // TODO: Fail after a certain amount of time and retry
    ret = udp_sock.recv(other, buffer, sizeof(buffer));
    std::cout << "Received " << ret << " from " << other.to_string() << " " << other.get_port()
              << "\n";
    flags = std::cout.flags();
    for (int i = 0; i < sizeof(buffer); i++)
        std::cout << std::setw(3) << std::hex << (int) buffer[i];

    std::cout.flags(flags);
    std::cout << "\n";

    // First 4 bytes of buffer should be SSRC, next is beginning of this udp socket's external IP
    external_ip = std::string((char *) &buffer[4]);
    std::cout << "External IP " << external_ip << "\n";

    // Extract the port the udp socket is on (little-endian)
    uint16_t local_udp_port = (buffer[69] << 8) | buffer[68];

    std::cout << "udp port " << local_udp_port << "\n";

    //    select(local_udp_port);
}

void cmd::discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
{
    // Override the heartbeat_interval with value 75% of current
    // This is a bug with Discord apparently
    if (data["heartbeat_interval"].is_number()) {
        int val = data.at("heartbeat_interval").get<int>();
        val = (val * 3) / 4;
        data["heartbeat_interval"] = val;
        beater.on_hello(*this, data);
    }
}

void cmd::discord::voice_gateway::start_speaking()
{
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", true}, {"delay", 0}, {"ssrc", ssrc}}}};
}

void cmd::discord::voice_gateway::stop_speaking()
{
    nlohmann::json speaking_payload{{"op", static_cast<int>(gtw_voice_op_send::speaking)},
                                    {"d", {{"speaking", false}, {"delay", 0}, {"ssrc", ssrc}}}};
}

void cmd::discord::voice_gateway::select(uint16_t local_udp_port)
{
    nlohmann::json select_payload{
        {"op", static_cast<int>(gtw_voice_op_send::select_proto)},
        {"d",
         {"protocol", "udp"},
         {"data",
          {{"address", external_ip}, {"port", local_udp_port}, {"mode", "xsalsa20_poly1305"}}}}};

    safe_send(select_payload.dump());
}
