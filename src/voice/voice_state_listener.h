#ifndef CMD_DISCORD_VOICE_STATE_LISTENER_H
#define CMD_DISCORD_VOICE_STATE_LISTENER_H

#include <gateway.h>
#include <voice_gateway.h>
#include "event_listener.h"

namespace cmd
{
namespace discord
{
class voice_state_listener : public event_listener
{
public:
    voice_state_listener(const std::string &user_id, const std::string &channel_id,
                         const std::string &guild_id);
    ~voice_state_listener();
    void handle(cmd::discord::gateway &gateway, gtw_op_recv, const nlohmann::json &json,
                const std::string &type) override;

private:
    std::string user_id, channel_id, guild_id, session_id;
    std::unique_ptr<cmd::discord::voice_gateway> voice_gateway_ptr;

    std::thread voice_gateway_thread;
    bool alive;

    void voice_event_loop();
};
}
}

#endif
