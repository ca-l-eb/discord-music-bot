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
                              [&](nlohmann::json &data) { store.guild_create(data); });
    gateway_event_map.emplace("CHANNEL_CREATE",
                              [&](nlohmann::json &data) { store.channel_create(data); });
    gateway_event_map.emplace("CHANNEL_UPDATE",
                              [&](nlohmann::json &data) { store.channel_update(data); });
    gateway_event_map.emplace("CHANNEL_DELETE",
                              [&](nlohmann::json &data) { store.channel_delete(data); });
    gateway_event_map.emplace("RESUME",
                              [&](nlohmann::json &) { state = connection_state::connected; });

    // Add listener for voice events
    auto handler = std::make_shared<voice_state_listener>(ctx, *this, store);
    add_listener("VOICE_STATE_UPDATE", "voice_gateway_listener", handler);
    add_listener("VOICE_SERVER_UPDATE", "voice_gateway_listener", handler);
    add_listener("MESSAGE_CREATE", "voice_gateway_listener", handler);

    // Establish the WebSocket connection
    websock.async_connect("wss://gateway.discord.gg/?v=6&encoding=json", resolver,
                          [&](auto &ec) { on_connect(ec); });
}

discord::gateway::~gateway() {}

void discord::gateway::on_connect(const boost::system::error_code &e)
{
    if (e) {
        throw std::runtime_error("Could not connect: " + e.message());
    }
    std::cout << "[gateway] connected! sending identify\n";
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

void discord::gateway::send(const std::string &s, transfer_cb c)
{
    sender.safe_send(s, c);
}

void discord::gateway::heartbeat()
{
    nlohmann::json json{{"op", static_cast<int>(gateway_op::heartbeat)}, {"d", seq_num}};
    send(json.dump(), ignore_transfer);
}

void discord::gateway::identify()
{
    nlohmann::json identify_payload{
        {"op", static_cast<int>(gateway_op::identify)},
        {"d",
         {{"token", token},
          {"properties",
           {{"$os", "linux"}, {"$browser", "cmd-discord"}, {"$device", "cmd-discord"}}},
          {"compress", compress},
          {"large_threshold", large_threshold}}}};

    auto callback = [&](const boost::system::error_code &e, size_t) {
        if (e) {
            std::cerr << "[gateway] identify send error: " << e.message() << "\n";
        } else {
            std::cout << "[gateway] sucessfully sent identify payload... beginning event loop\n";

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
        {"op", static_cast<int>(gateway_op::resume)},
        {"d", {{"token", token}, {"session_id", session_id}, {"seq", seq_num}}}};
    send(resume_payload.dump(), ignore_transfer);
}

uint64_t discord::gateway::get_user_id() const
{
    return user_id;
}

const std::string &discord::gateway::get_session_id() const
{
    return session_id;
}

void discord::gateway::run_public_dispatch(gateway_op op, nlohmann::json &data,
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
    auto ready = data.get<discord::event::ready>();
    if (ready.version != 6) {
        throw std::runtime_error("Unsupported gateway protocol version: " +
                                 std::to_string(ready.version) + ". Support is limited to v6");
    }

    // We got the READY event, we are now connected
    state = connection_state::connected;

    user_id = ready.user.id;
    session_id = std::move(ready.session_id);
}

void discord::gateway::event_loop()
{
    auto callback = [&](auto &e, auto *data, auto len) {
        if (e) {
            if (e == websocket::error::websocket_connection_closed) {
                // Convert the close code to a gateway error message
                auto ec = make_error_code(static_cast<gateway::error>(websock.close_code()));
                throw std::runtime_error(ec.message());
            }
            throw std::runtime_error("There was an error: " + e.message());
        } else {
            handle_event(data, len);
        }
    };
    // Asynchronously read next message, on message received send it to listeners
    websock.async_next_message(callback);
}

void discord::gateway::handle_event(const uint8_t *data, size_t len)
{
    const char *start = reinterpret_cast<const char *>(data);
    const char *end = start + len;
    std::cout << "[gateway] ";
    std::cout.write(start, len);
    std::cout << "\n";
    try {
        auto json = nlohmann::json::parse(start, end);
        auto payload = json.get<discord::payload>();
        seq_num = payload.sequence_num;

        if (payload.event_name == "MESSAGE_CREATE") {
            // TODO: Ignore messages that are sent by this bot (user_id)
        }

        switch (payload.op) {
            case gateway_op::dispatch:
                run_public_dispatch(payload.op, payload.data, payload.event_name);
                run_gateway_dispatch(payload.data, payload.event_name);
                break;
            case gateway_op::heartbeat:
                heartbeat();  // Respond to heartbeats with a heartbeat
                break;
            case gateway_op::reconnect:
                // We are asked to reconnect, i.e. we are disconnected
                state = connection_state::disconnected;
                resume();
                break;
            case gateway_op::invalid_session:
                if (state == connection_state::disconnected) {
                    throw std::runtime_error("Could not connect to Discord Gateway");
                } else {
                    state = connection_state::disconnected;
                    // Already connected, if we can reconnect, try to resume the
                    // connection
                    if (payload.data.is_boolean() && payload.data.get<bool>())
                        resume();
                    else
                        throw std::runtime_error("disconnected");
                }
                break;
            case gateway_op::hello:
                beater->on_hello(payload.data);
                break;
            case gateway_op::heartbeat_ack:
                beater->on_heartbeat_ack();
                break;
            default:
                throw std::runtime_error("Unknown opcode: " +
                                         std::to_string(static_cast<int>(payload.op)));
        }
    } catch (nlohmann::json::exception &e) {
        std::cerr << "[gateway] " << e.what() << "\n";
    }
    event_loop();
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

boost::system::error_code discord::make_error_code(discord::gateway::error code) noexcept
{
    return {(int) code, discord::gateway::error_category::instance()};
}
