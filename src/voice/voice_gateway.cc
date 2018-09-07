#include <array>
#include <iostream>
#include <json.hpp>

#include "discord.h"
#include "errors.h"
#include "net/uri.h"
#include "voice/voice_gateway.h"
#include "voice/voice_state_listener.h"

discord::voice_gateway::voice_gateway(boost::asio::io_context &ctx, ssl::context &tls,
                                      std::shared_ptr<voice_gateway_entry> e,
                                      discord::snowflake user_id)
    : ctx{ctx}
    , entry{e}
    , conn{ctx, tls}
    , rtp{ctx}
    , beater{ctx}
    , user_id{user_id}
    , state{connection_state::disconnected}
    , is_speaking{false}
{
    std::cout << "[voice] connecting to gateway " << entry->endpoint << " session_id["
              << entry->session_id << "] token[" << entry->token << "]\n";
}

void discord::voice_gateway::connect(error_cb c)
{
    voice_connect_callback = c;

    // entry->endpoint contains both hostname and (bogus) port, only care about hostname
    auto parsed = uri::parse(entry->endpoint);
    entry->endpoint = std::move(parsed.authority);

    conn.connect("wss://" + entry->endpoint + "/?v=3", [weak = weak_from_this()](const auto &ec) {
        if (auto self = weak.lock()) {
            if (ec) {
                std::cerr << "[voice] websocket connect error: " << ec.message() << "\n";
                boost::asio::post(self->ctx, [&]() { self->voice_connect_callback(ec); });
            } else {
                std::cout << "[voice] websocket connected\n";
                self->state = connection_state::connected;
                self->identify();
            }
        }
    });
}

void discord::voice_gateway::disconnect()
{
    state = connection_state::disconnected;
    conn.disconnect();
}

void discord::voice_gateway::identify()
{
    auto identify = nlohmann::json{{"op", static_cast<int>(voice_op::identify)},
                                   {"d",
                                    {{"server_id", entry->guild_id},
                                     {"user_id", user_id},
                                     {"session_id", entry->session_id},
                                     {"token", entry->token}}}};
    auto identify_sent_cb = [&](const auto &ec, auto) {
        if (ec) {
            std::cout << "[voice] gateway identify error: " << ec.message() << "\n";
            boost::asio::post(ctx, [&]() { voice_connect_callback(ec); });
        } else {
            std::cout << "[voice] starting event loop\n";
            next_event();
        }
    };
    send(identify.dump(), identify_sent_cb);
}

void discord::voice_gateway::send(const std::string &s, transfer_cb c)
{
    conn.send(s, c);
}

void discord::voice_gateway::next_event()
{
    if (state == connection_state::connected)
        conn.read([weak = weak_from_this()](const auto &ec, auto &json) {
            if (ec) {
                std::cerr << "[voice] error: " << ec.message() << "\n";
                return;
            }
            if (auto self = weak.lock())
                self->handle_event(json);
        });
}

void discord::voice_gateway::handle_event(const nlohmann::json &data)
{
    std::cout << "[voice] " << data.dump() << "\n";
    try {
        auto payload = data.get<discord::voice_payload>();

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
        next_event();
    } catch (nlohmann::json::exception &e) {
        std::cerr << "[voice] gateway error: " << e.what() << "\n";
    }
}

void discord::voice_gateway::heartbeat()
{
    // TODO: save the nonce (rand()) and check if it is ACKed
    auto json = nlohmann::json{{"op", static_cast<int>(voice_op::heartbeat)}, {"d", rand()}};
    send(json.dump(), ignore_transfer);
}

void discord::voice_gateway::extract_ready_info(nlohmann::json &data)
{
    auto ready_info = data.get<discord::voice_ready>();
    rtp.set_ssrc(ready_info.ssrc);

    auto connect_cb = [weak = weak_from_this()](const auto &ec) {
        if (auto self = weak.lock()) {
            if (ec) {
                boost::asio::post(self->ctx, [=]() { self->voice_connect_callback(ec); });
            } else {
                self->rtp.ip_discovery([weak](const auto &ecc) {
                    if (auto self = weak.lock()) {
                        if (ecc) {
                            self->voice_connect_callback(ecc);
                        } else {
                            self->select();
                        }
                    }
                });
            }
        }
    };
    rtp.connect(entry->endpoint, std::to_string(ready_info.port), connect_cb);
}

void discord::voice_gateway::extract_session_info(nlohmann::json &data)
{
    auto session_info = data.get<discord::voice_session>();
    if (session_info.mode != "xsalsa20_poly1305")
        throw std::runtime_error("Unsupported voice mode: " + session_info.mode);

    if (session_info.secret_key.size() != 32)
        throw std::runtime_error("Expected 32 byte secret key but got " +
                                 std::to_string(session_info.secret_key.size()));

    rtp.set_secret_key(std::move(session_info.secret_key));

    // We are ready to start speaking!
    boost::asio::post(ctx, [&]() { voice_connect_callback({}); });
}

void discord::voice_gateway::select()
{
    auto select_payload = nlohmann::json{{"op", static_cast<int>(voice_op::select_proto)},
                                         {"d",
                                          {{"protocol", "udp"},
                                           {"data",
                                            {{"address", rtp.get_external_ip()},
                                             {"port", rtp.get_external_port()},
                                             {"mode", "xsalsa20_poly1305"}}}}}};

    send(select_payload.dump(), ignore_transfer);
}

void discord::voice_gateway::notify_heartbeater_hello(nlohmann::json &data)
{
    // Override the heartbeat_interval with value 75% of current
    // This is a bug with Discord apparently
    if (data["heartbeat_interval"].is_number()) {
        auto val = data.at("heartbeat_interval").get<int>();
        val = (val / 4) * 3;
        data["heartbeat_interval"] = val;
        beater.on_hello(data, *this);
    } else {
        std::cerr << "[voice] no heartbeat_interval in hello payload\n";
    }
}

void discord::voice_gateway::resume()
{
    state = connection_state::disconnected;
    auto resumed = nlohmann::json{{"op", static_cast<int>(voice_op::resume)},
                                  {"d",
                                   {{"server_id", entry->guild_id},
                                    {"session_id", entry->session_id},
                                    {"token", entry->token}}}};
    send(resumed.dump(), ignore_transfer);
}

static void do_speak(discord::voice_gateway *vg, transfer_cb c, bool speak)
{
    // Apparently this _doesnt_ need the ssrc
    auto speaking_payload = nlohmann::json{{"op", static_cast<int>(discord::voice_op::speaking)},
                                           {"d", {{"speaking", speak}, {"delay", 0}}}};
    vg->send(speaking_payload.dump(), c);
}

void discord::voice_gateway::start_speaking(transfer_cb c)
{
    do_speak(this, c, true);
}

void discord::voice_gateway::stop_speaking(transfer_cb c)
{
    do_speak(this, c, false);
}

void discord::voice_gateway::play(const opus_frame &frame)
{
    if (!is_speaking) {
        auto speak_sent_cb = [=](const auto &ec, auto) {
            if (!ec) {
                is_speaking = true;
                rtp.send(frame);
            }
        };
        start_speaking(speak_sent_cb);
    } else {
        rtp.send(frame);
    }
}

void discord::voice_gateway::stop()
{
    is_speaking = false;
    stop_speaking(ignore_transfer);
}
