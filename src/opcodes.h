#ifndef CMD_DISCORD_OPCODES_H
#define CMD_DISCORD_OPCODES_H

namespace cmd
{
namespace discord
{
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

enum class gtw_voice_op_send {
    identify = 0,
    select_proto = 1,
    heartbeat = 3,
    speaking = 5,
    resume = 7
};

enum class gtw_voice_op_recv {
    ready = 2,
    session_description = 4,
    speaking = 5,
    heartbeat_ack = 6,
    hello = 8,
    resumed = 9,
    client_disconnect = 13
};
}
}
#endif
