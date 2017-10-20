#ifndef CMD_DISCORD_VOICE_GATEWAY_H
#define CMD_DISCORD_VOICE_GATEWAY_H

#include <cmd/websocket.h>
#include <events/event_listener.h>

namespace cmd
{
namespace discord
{
class voice_gateway
{
public:
    voice_gateway(cmd::websocket::socket &connection, const std::string &user_id,
                  const std::string &session_id, const std::string &token);

private:
    cmd::websocket::socket websock;
    std::string user_id, session_id, token;
};
}
}

#endif
