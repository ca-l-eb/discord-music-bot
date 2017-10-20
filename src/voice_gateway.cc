#include "voice_gateway.h"

cmd::discord::voice_gateway::voice_gateway(cmd::websocket::socket &connection,
                                           const std::string &user_id,
                                           const std::string &session_id, const std::string &token)
    : websock{connection}, user_id{user_id}, session_id{session_id}, token{token}
{
}