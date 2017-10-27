#ifndef CMD_DISCORD_HEARTBEATER_H
#define CMD_DISCORD_HEARTBEATER_H

#include <json.hpp>
#include <boost/asio.hpp>

namespace cmd
{
namespace discord
{
struct beatable {
    virtual void heartbeat() {}
};

// On hello opcode, spawns a thread and periodically sends a heartbeat message through *this
// gateway. On destruction it stops the heartbeat thread and joins it
class heartbeater
{
public:
    heartbeater(boost::asio::io_service &service, cmd::discord::beatable &b);
    ~heartbeater();
    void on_hello(const nlohmann::json &data);
    void on_heartbeat_ack();
    void cancel();

private:
    cmd::discord::beatable &b;
    boost::asio::deadline_timer timer;
    int heartbeat_interval;
    bool acked;
    
    void start_heartbeat_timer();
    void on_timer_fire(const boost::system::error_code &e);
};
}
}

#endif
