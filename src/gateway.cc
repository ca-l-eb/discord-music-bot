#include <iostream>
#include <memory>

#include <gateway.h>
#include <voice/voice_state_listener.h>

discord::gateway::gateway(boost::asio::io_context &ctx, const std::string &token,
                          boost::asio::ip::tcp::resolver &resolver)
    : resolver{resolver}
    , ctx{ctx}
    , websock{ctx}
    , sender{ctx, websock, 500}
    , token{token}
    , state{connection_state::disconnected}
{
    gateway_event_map.emplace("READY", [&](nlohmann::json &data) { on_ready(data); });
    gateway_event_map.emplace("GUILD_CREATE",
                              [&](nlohmann::json &data) { store.parse_guild(data); });
    gateway_event_map.emplace("RESUME",
                              [&](nlohmann::json &) { state = connection_state::connected; });

    // Add listener for voice events
    auto handler = std::make_shared<voice_state_listener>(ctx, *this, store);
    add_listener("VOICE_STATE_UPDATE", "voice_gateway_listener", handler);
    add_listener("VOICE_SERVER_UPDATE", "voice_gateway_listener", handler);
    add_listener("MESSAGE_CREATE", "voice_gateway_listener", handler);

    // Establish the WebSocket connection
    websock.async_connect("wss://gateway.discord.gg/?v=6&encoding=json", resolver,
                          [&](auto &ec, auto transferred) { on_connect(ec, transferred); });
}

discord::gateway::~gateway() {}

void discord::gateway::on_connect(const boost::system::error_code &e, size_t)
{
    if (e) {
        throw std::runtime_error("Could not connect: " + e.message());
    }
    std::cout << "Connected! Sending identify\n";
    identify();
}

void discord::gateway::add_listener(const std::string &event_name, const std::string &handler_name,
                                    event_listener::ptr h)
{
    event_name_to_handler_name.emplace(event_name, handler_name);
    handler_name_to_handler_ptr.emplace(handler_name, h);
}

void discord::gateway::remove_listener(const std::string &event_name,
                                       const std::string &handler_name)
{
    auto range = event_name_to_handler_name.equal_range(event_name);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == handler_name) {
            event_name_to_handler_name.erase(it);
            handler_name_to_handler_ptr.erase(handler_name);
        }
    }
}

void discord::gateway::send(const std::string &s, message_sent_callback c)
{
    sender.safe_send(s, c);
}

void discord::gateway::heartbeat()
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::heartbeat)}, {"d", seq_num}};
    send(json.dump(), print_info_send);
}

void discord::gateway::identify()
{
    nlohmann::json identify_payload{
        {"op", static_cast<int>(gtw_op_send::identify)},
        {"d",
         {{"token", token},
          {"properties",
           {{"$os", "linux"}, {"$browser", "cmd-discord"}, {"$device", "cmd-discord"}}},
          {"compress", compress},
          {"large_threshold", large_threshold}}}};

    auto callback = [&](const boost::system::error_code &e, size_t) {
        if (e) {
            std::cerr << "Identify send error: " << e.message() << "\n";
        } else {
            std::cout << "Sucessfully sent identify payload... Beginning event loop\n";

            // Create heartbeater for this gateway
            beater = std::make_unique<discord::heartbeater>(ctx, *this);

            event_loop();
        }
    };
    send(identify_payload.dump(), callback);
}

void discord::gateway::resume()
{
    // If we are connected, ignore any resumes
    if (state == connection_state::connected)
        return;

    if (session_id.empty())
        throw std::runtime_error("Could not resume previous session: no such session");

    // Close the previous connection and create a new websocket
    websock.close(websocket::status_code::going_away);
    //    websock.connect("wss://gateway.discord.gg/?v=6&encoding=json");

    nlohmann::json resume_payload{
        {"op", static_cast<int>(gtw_op_send::resume)},
        {"d", {{"token", token}, {"session_id", session_id}, {"seq", seq_num}}}};
    send(resume_payload.dump(), print_info_send);
}

const std::string &discord::gateway::get_user_id() const
{
    return user_id;
}

const std::string &discord::gateway::get_session_id() const
{
    return session_id;
}

void discord::gateway::run_public_dispatch(gtw_op_recv op, nlohmann::json &data,
                                           const std::string &t)
{
    std::string events[] = {t, "ALL"};
    for (auto &event : events) {
        auto range = event_name_to_handler_name.equal_range(event);
        for (auto it = range.first; it != range.second; ++it) {
            auto handler = handler_name_to_handler_ptr.find(it->second);
            if (handler != handler_name_to_handler_ptr.end())
                handler->second->handle(*this, op, data, t);
        }
    }
}

void discord::gateway::run_gateway_dispatch(nlohmann::json &data, const std::string &event_name)
{
    auto event_it = gateway_event_map.find(event_name);
    if (event_it != gateway_event_map.end())
        event_it->second(data);
}

void discord::gateway::on_ready(nlohmann::json &data)
{
    auto version = data["v"];
    if (version.is_number()) {
        if (version.get<int>() != 6)
            throw std::runtime_error(
                "Unsupported gateway protocol version: " + std::to_string(version.get<int>()) +
                ". Support is limited to v6");
    } else {
        throw std::runtime_error("Expected version number in READY event");
    }

    // We got the READY event, we are now connected
    state = connection_state::connected;

    auto user_obj = data["user"];
    if (user_obj.is_object()) {
        auto id = user_obj["id"];
        if (id.is_string()) {
            user_id = id.get<std::string>();
        }
    }
    auto session = data["session_id"];
    if (session.is_string())
        session_id = session.get<std::string>();
}

void discord::gateway::event_loop()
{
    // Asynchronously read next message, on message received send it to listeners
    websock.async_next_message([&](const boost::system::error_code &e, const uint8_t *data,
                                   size_t len) {
        if (e) {
            if (e == websocket::error::websocket_connection_closed) {
                // Convert the close code to a gateway error message
                auto ec = make_error_code(static_cast<gateway::error>(websock.close_code()));
                throw std::runtime_error(ec.message());
            }
            throw std::runtime_error("There was an error: " + e.message());
        }
        const char *start = reinterpret_cast<const char *>(data);
        const char *end = start + len;
        std::cout << "Gateway received: ";
        std::cout.write(start, len);
        std::cout << "\n";
        try {
            auto json = nlohmann::json::parse(start, end);
            auto op = json["op"];
            auto event_data = json["d"];

            if (op.is_number()) {
                int op_val = op.get<int>();
                if (op_val == 0) {
                    if (json["s"].is_number()) {
                        seq_num = json["s"].get<int>();
                    }
                }
                std::string event_name = json["t"].is_string() ? json["t"].get<std::string>() : "";

                if (event_name == "MESSAGE_CREATE") {
                    // TODO: Ignore messages that are sent by this bot (user_id)
                }

                auto gateway_op = static_cast<gtw_op_recv>(op_val);
                switch (gateway_op) {
                    case gtw_op_recv::dispatch:
                        run_public_dispatch(gateway_op, event_data, event_name);
                        run_gateway_dispatch(event_data, event_name);
                        break;
                    case gtw_op_recv::heartbeat:
                        heartbeat();  // Respond to heartbeats with a heartbeat
                        break;
                    case gtw_op_recv::reconnect:
                        // We are asked to reconnect, i.e. we are disconnected
                        state = connection_state::disconnected;
                        resume();
                        break;
                    case gtw_op_recv::invalid_session:
                        if (state == connection_state::disconnected) {
                            throw std::runtime_error("Could not connect to Discord Gateway");
                        } else {
                            state = connection_state::disconnected;
                            // Already connected, if we can reconnect, try to resume the
                            // connection
                            if (event_data.is_boolean() && event_data.get<bool>())
                                resume();
                            else
                                throw std::runtime_error("disconnected");
                        }
                        break;
                    case gtw_op_recv::hello:
                        beater->on_hello(event_data);
                        break;
                    case gtw_op_recv::heartbeat_ack:
                        beater->on_heartbeat_ack();
                        break;
                    default:
                        throw std::runtime_error("Unknown opcode: " + std::to_string(op_val));
                }
            }

            // There was no error, run the event loop again
            event_loop();
        } catch (nlohmann::json::exception &e) {
            std::cerr << e.what() << "\n";
        }
    });
}

const char *discord::gateway::error_category::name() const noexcept
{
    return "gateway";
}

std::string discord::gateway::error_category::message(int ev) const noexcept
{
    switch (error(ev)) {
        case error::unknown_error:
            return "unknown error";
        case error::unknown_opcode:
            return "invalid opcode";
        case error::decode_error:
            return "decode error";
        case error::not_authenticated:
            return "sent payload before identified";
        case error::authentication_failed:
            return "incorrect token in identify payload";
        case error::already_authenticated:
            return "sent more than one identify payload";
        case error::invalid_seq:
            return "invalid sequence number";
        case error::rate_limited:
            return "rate limited";
        case error::session_timeout:
            return "session has timed out";
        case error::invalid_shard:
            return "invalid shard";
        case error::sharding_required:
            return "sharding required";
    }
    return "Unknown gateway error";
}

bool discord::gateway::error_category::equivalent(const boost::system::error_code &code,
                                                  int condition) const noexcept
{
    return &code.category() == this && static_cast<int>(code.value()) == condition;
}

const boost::system::error_category &discord::gateway::error_category::instance()
{
    static discord::gateway::error_category instance;
    return instance;
}

boost::system::error_code make_error_code(discord::gateway::error code) noexcept
{
    return boost::system::error_code{(int) code, discord::gateway::error_category::instance()};
}
