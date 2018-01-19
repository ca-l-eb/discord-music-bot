#ifndef CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H
#define CMD_DISCORD_GATEWAY_EVENT_LISTENERS_H

#include <condition_variable>
#include <memory>
#include <thread>

#include <api.h>
#include <opcodes.h>
#include <json.hpp>

namespace discord
{
class gateway;

struct event_listener {
    using ptr = std::shared_ptr<event_listener>;
    virtual void handle(discord::gateway &gateway, gtw_op_recv, const nlohmann::json &,
                        const std::string &) = 0;
};
}

#endif
