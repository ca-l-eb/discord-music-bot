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

namespace cmd
{
namespace discord
{
class gateway : public beatable
{
public:
    enum class error {
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

    class error_category : public boost::system::error_category
    {
    public:
        virtual const char *name() const noexcept
        {
            return "gateway";
        }

        virtual std::string message(int ev) const noexcept
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

        virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept
        {
            return &code.category() == this && static_cast<int>(code.value()) == condition;
        }
    };

    static const boost::system::error_category &category()
    {
        static gateway::error_category instance;
        return instance;
    }

    static boost::system::error_code make_error_code(gateway::error code) noexcept
    {
        return boost::system::error_code{(int) code, category()};
    }

    gateway(boost::asio::io_service &service, const std::string &token);
    ~gateway();
    void add_listener(const std::string &event_name, const std::string &handler_name,
                      event_listener::ptr h);
    void remove_listener(const std::string &event_name, const std::string &handler_name);
    void heartbeat() override;
    void send(const std::string &s, cmd::websocket::message_sent_callback c);
    const std::string &get_user_id() const;
    const std::string &get_session_id() const;

private:
    boost::asio::io_service &service;
    cmd::websocket websocket;
    cmd::discord::delayed_message_sender sender;
    cmd::discord::gateway_store store;

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

    cmd::websocket::message_sent_callback ignore_send = [](const boost::system::error_code &,
                                                           size_t) {};
    cmd::websocket::message_sent_callback print_info_send = [](const boost::system::error_code &e,
                                                               size_t transferred) {
        if (e) {
            std::cerr << "Message send error: " << e.message() << "\n";
        } else {
            std::cout << "Sent " << transferred << " bytes\n";
        }
    };
};
}
}

#endif
