#ifndef CMD_DISCORD_HELLO_RESPONDER_H
#define CMD_DISCORD_HELLO_RESPONDER_H

#include <events/event_listener.h>

namespace discord
{
struct hello_responder : public event_listener {
    explicit hello_responder(discord::api &api);
    void handle(discord::gateway &, gateway_op, const nlohmann::json &json,
                const std::string &type) override;

private:
    discord::api &api;
};
}

#endif
