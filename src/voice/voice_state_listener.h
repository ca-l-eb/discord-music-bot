#ifndef CMD_DISCORD_VOICE_STATE_LISTENER_H
#define CMD_DISCORD_VOICE_STATE_LISTENER_H

#include <events/event_listener.h>
#include <gateway.h>
#include <boost/asio.hpp>
#include <memory>

namespace cmd
{
namespace discord
{
class voice_gateway;

struct voice_gateway_entry {
    std::string channel_id;
    std::string guild_id;  // server_id in docs
    std::string session_id;
    std::string token;
    std::string endpoint;
    std::unique_ptr<cmd::discord::voice_gateway> gateway;
};

class voice_state_listener : public event_listener
{
public:
    voice_state_listener(boost::asio::io_service &service, cmd::discord::gateway &gateway, cmd::discord::gateway_store &store);
    ~voice_state_listener();
    void handle(cmd::discord::gateway &gateway, gtw_op_recv, const nlohmann::json &json,
                const std::string &type) override;

private:
    boost::asio::io_service &service;
    cmd::discord::gateway &gateway;
    cmd::discord::gateway_store &store;

    // Map guild_id to voice_gateway_entry (since 1 voice connection per guild)
    std::map<std::string, voice_gateway_entry> voice_gateways;

    void voice_state_update(const nlohmann::json &data);
    void voice_server_update(const nlohmann::json &data);
    void message_create(const nlohmann::json &data);
    
    void join_voice_server(const std::string &guild_id, const std::string &channel_id);
    void leave_voice_server(const std::string &guild_id);
    void check_command(const std::string &content, const nlohmann::json &data);
    void do_join(const std::string &params, const nlohmann::json &json);
    void do_leave(const nlohmann::json &json);
    void do_list(const nlohmann::json &json);
    void do_add(const std::string &params, const nlohmann::json &json);
    void do_skip(const nlohmann::json &json);
};
}
}

#endif
