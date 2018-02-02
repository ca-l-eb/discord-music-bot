#ifndef CMD_DISCORD_ECHO_LISTENER_H
#define CMD_DISCORD_ECHO_LISTENER_H

#include "events/event_listener.h"

namespace discord
{
struct echo_listener : public event_listener {
    void handle(discord::gateway &, gateway_op, const nlohmann::json &json,
                const std::string &type) override;
};
}

#endif
