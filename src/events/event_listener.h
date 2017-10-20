#ifndef CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H
#define CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H

#include <condition_variable>
#include <memory>
#include <thread>

#include <api.h>
#include <json.hpp>

namespace cmd
{
namespace discord
{
class gateway;

enum class gtw_op_recv {
    dispatch = 0,
    heartbeat = 1,
    reconnect = 7,
    invalid_session = 9,
    hello = 10,
    heartbeat_ack = 11
};

enum class gtw_op_send {
    heartbeat = 1,
    identify = 2,
    status_update = 3,
    voice_state_update = 4,
    voice_server_ping = 5,
    resume = 6,
    request_guild_members = 8
};

struct event_listener {
    using ptr = std::shared_ptr<event_listener>;
    virtual void handle(gtw_op_recv, const nlohmann::json &, const std::string &) = 0;
};
}
}

#endif
