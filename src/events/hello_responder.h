#ifndef CMD_DISCORD_HELLO_RESPONDER_H
#define CMD_DISCORD_HELLO_RESPONDER_H

#include <events/event_listener.h>

namespace cmd
{
namespace discord
{
struct hello_responder : public event_listener {
    explicit hello_responder(cmd::discord::api *api);
    void handle(gtw_op_recv, const nlohmann::json &json, const std::string &type) override;

private:
    cmd::discord::api *api;
};
}
}

#endif
