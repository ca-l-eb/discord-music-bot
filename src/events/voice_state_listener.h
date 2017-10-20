#ifndef CMD_DISCORD_VOICE_STATE_LISTENER_H
#define CMD_DISCORD_VOICE_STATE_LISTENER_H

#include "event_listener.h"

namespace cmd
{
namespace discord
{
class voice_state_listener : public event_listener
{
public:
    voice_state_listener(cmd::discord::gateway *gateway, const std::string &user_id,
                         const std::string &channel_id, const std::string &guild_id);
    void handle(gtw_op_recv, const nlohmann::json &json, const std::string &type) override;

private:
    cmd::discord::gateway *gateway;
    std::string user_id, channel_id, guild_id, session_id;
};
}
}

#endif
