#ifndef CMD_DISCORD_HEARTBEATER_H
#define CMD_DISCORD_HEARTBEATER_H

#include <json.hpp>

namespace cmd
{
namespace discord
{
struct beatable {
    virtual void heartbeat() {}
};

class gateway;

// On hello opcode, spawns a thread and periodically sends a heartbeat message through *this
// gateway. On destruction it stops the heartbeat thread and joins it
class heartbeater
{
public:
    explicit heartbeater();
    ~heartbeater();
    void heartbeat_loop(cmd::discord::beatable *b);
    void on_hello(cmd::discord::beatable &b, const nlohmann::json &data);
    void on_heartbeat_ack();
    void notify();
    void join();

private:
    int heartbeat_interval;
    bool first;
    bool acked;

    std::thread heartbeat_thread;
    std::mutex thread_mutex;
    std::condition_variable loop_variable;
};
}
}

#endif
