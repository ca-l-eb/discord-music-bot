#ifndef CMD_DISCORD_BOT_H
#define CMD_DISCORD_BOT_H

#include <condition_variable>
#include <iostream>
#include <json.hpp>
#include <mutex>
#include <thread>

#include <api.h>
#include <delayed_message_sender.h>
#include <events/event_listener.h>
#include <gateway_store.h>
#include <heartbeater.h>
#include <net/websocket.h>
#include <voice/voice_gateway.h>

namespace discord
{
enum class gateway_error {
    unknown_error = 4000,
    unknown_opcode = 4001,
    decode_error = 4002,
    not_authenticated = 4003,
    authentication_failed = 4004,
    already_authenticated = 4005,
    invalid_seq = 4007,
    rate_limited = 4008,
    session_timeout = 4009,
    invalid_shard = 4010,
    sharding_required = 4011
};

class gateway_error_category : public boost::system::error_category
{
public:
    virtual const char *name() const noexcept
    {
        return "gateway";
    }

    virtual std::string message(int ev) const noexcept
    {
        switch (gateway_error(ev)) {
            case gateway_error::unknown_error:
                return "unknown error";
            case gateway_error::unknown_opcode:
                return "invalid opcode";
            case gateway_error::decode_error:
                return "decode error";
            case gateway_error::not_authenticated:
                return "sent payload before identified";
            case gateway_error::authentication_failed:
                return "incorrect token in identify payload";
            case gateway_error::already_authenticated:
                return "sent more than one identify payload";
            case gateway_error::invalid_seq:
                return "invalid sequence number";
            case gateway_error::rate_limited:
                return "rate limited";
            case gateway_error::session_timeout:
                return "session has timed out";
            case gateway_error::invalid_shard:
                return "invalid shard";
            case gateway_error::sharding_required:
                return "sharding required";
        }
        return "Unknown gateway error";
    }

    virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept
    {
        return &code.category() == this && static_cast<int>(code.value()) == condition;
    }
};

const boost::system::error_category &gateway_category();

class gateway : public beatable
{
public:
    gateway(boost::asio::io_context &ctx, const std::string &token,
            boost::asio::ip::tcp::resolver &resolver);
    ~gateway();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void heartbeat() override;
    void send(const std::string &s, message_sent_callback c);
    const std::string &get_user_id() const;
    const std::string &get_session_id() const;

    boost::asio::ip::tcp::resolver &resolver;

private:
    boost::asio::io_context &ctx;
    websocket websock;
    discord::delayed_message_sender sender;
    discord::gateway_store store;

    // Map an event name (e.g. READY, RESUMED, etc.) to a handler name
    std::multimap<std::string, std::string> event_name_to_handler_name;

    // Map a handler name to a event_lister
    std::map<std::string, event_listener::ptr> handler_name_to_handler_ptr;
    std::map<std::string, std::function<void(nlohmann::json &)>> gateway_event_map;

    std::unique_ptr<heartbeater> beater;
    std::string token;
    std::string user_id, session_id;
    int seq_num;
    const bool compress = false;
    const int large_threshold = 250;
    enum class connection_state { disconnected, connected } state;

    void run_public_dispatch(gtw_op_recv op, nlohmann::json &data, const std::string &t);
    void run_gateway_dispatch(nlohmann::json &data, const std::string &event_name);
    void identify();
    void resume();
    void on_connect(const boost::system::error_code &e, size_t);
    void on_ready(nlohmann::json &data);
    void event_loop();

    message_sent_callback ignore_send = [](const boost::system::error_code &, size_t) {};
    message_sent_callback print_info_send = [](const boost::system::error_code &e,
                                               size_t transferred) {
        if (e) {
            std::cerr << "Message send error: " << e.message() << "\n";
        } else {
            std::cout << "Sent " << transferred << " bytes\n";
        }
    };
};
}

boost::system::error_code make_error_code(discord::gateway_error code) noexcept;

#endif
