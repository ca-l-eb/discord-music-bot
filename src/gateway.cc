#include <functional>
#include <iostream>
#include <memory>

#include <errors.h>
#include <gateway.h>
#include <voice/voice_state_listener.h>

discord::gateway::gateway(boost::asio::io_context &ctx, ssl::context &tls, const std::string &token)
    : ctx{ctx}
    , tls{tls}
    , resolver{ctx}
    , websock{ctx, tls}
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

    tcp::resolver::query query{"gateway.discord.gg", "443"};
    resolver.async_resolve(query, std::bind(&gateway::on_resolve, shared_from_this(),
                                            std::placeholders::_1, std::placeholders::_2));
}

discord::gateway::~gateway() {}

void discord::gateway::on_resolve(const boost::system::error_code &ec, tcp::resolver::iterator it)
{
    if (ec) {
        throw std::runtime_error("Could not resolve host: " + ec.message());
    }

    boost::asio::async_connect(
        websock.next_layer().lowest_layer(), it,
        std::bind(&gateway::on_connect, shared_from_this(), std::placeholders::_1));
}

void discord::gateway::on_connect(const boost::system::error_code &ec)
{
    if (ec) {
        throw std::runtime_error("Could not connect: " + ec.message());
    }

    websock.next_layer().async_handshake(
        ssl::stream_base::client,
        std::bind(&gateway::on_tls_handshake, shared_from_this(), std::placeholders::_1));
}

void discord::gateway::on_tls_handshake(const boost::system::error_code &ec)
{
    if (ec) {
        throw std::runtime_error{"TLS handshake error: " + ec.message()};
    }

    websock.async_handshake(
        "gateway.discord.gg", "/?v=6&encoding=json",
        std::bind(&gateway::on_websocket_handshake, shared_from_this(), std::placeholders::_1));
}

void discord::gateway::on_websocket_handshake(const boost::system::error_code &ec)
{
    if (ec) {
        throw std::runtime_error("Could not complete websocket handshake: " + ec.message());
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
    // TODO: use strand + message queue + timer for delay
    boost::system::error_code ec;
    auto wrote = websock.write(boost::asio::buffer(s), ec);
    c(ec, wrote);
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

    // TODO: Close the previous connection and create a new websocket

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

void discord::gateway::on_read(const boost::system::error_code &ec, size_t transferred)
{
    if (ec) {
        // TODO: if close code, convert to gateway error code
        std::cerr << "[gateway] error reading message: " << ec.message() << "\n";
    } else {
        auto data = boost::beast::buffers_to_string(buffer);
        handle_event(data);
        buffer.consume(transferred);
    }
}

void discord::gateway::event_loop()
{
    // Asynchronously read next message, on message received send it to listeners
    websock.async_read(buffer, std::bind(&gateway::on_read, shared_from_this(),
                                         std::placeholders::_1, std::placeholders::_2));
}

void discord::gateway::handle_event(const std::string &data)
{
    std::cout << "[gateway] ";
    std::cout.write(data.c_str(), data.size());
    std::cout << "\n";
    try {
        auto json = nlohmann::json::parse(data);
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
