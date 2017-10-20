#ifndef CMD_DISCORD_HEARTBEATER_H
#define CMD_DISCORD_HEARTBEATER_H

#include <events/event_listener.h>
#include <gateway.h>

namespace cmd
{
namespace discord
{
// On HELLO event, extract heartbeat_interval and spawn thread to heartbeat
struct heartbeater : public event_listener {
    explicit heartbeater(cmd::discord::gateway *gateway);
    ~heartbeater();
    void heartbeat_loop();
    void handle(gtw_op_recv op, const nlohmann::json &data, const std::string &) override;
    void notify();
    void join();

private:
    cmd::discord::gateway *gateway;
    int heartbeat_interval;
    bool first;

    std::thread heartbeat_thread;
    std::mutex thread_mutex;
    std::condition_variable loop_variable;
};
}
}

#endif
