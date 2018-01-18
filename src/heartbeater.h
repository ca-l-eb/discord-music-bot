#ifndef CMD_DISCORD_HEARTBEATER_H
#define CMD_DISCORD_HEARTBEATER_H

#include <boost/asio.hpp>
#include <json.hpp>

namespace discord
{
struct beatable {
    virtual void heartbeat() {}
};

class heartbeater
{
public:
    heartbeater(boost::asio::io_context &ctx, discord::beatable &b);
    ~heartbeater();
    void on_hello(const nlohmann::json &data);
    void on_heartbeat_ack();
    void cancel();

private:
    discord::beatable &b;
    boost::asio::deadline_timer timer;
    int heartbeat_interval;
    bool acked;

    void start_heartbeat_timer();
    void on_timer_fire(const boost::system::error_code &e);
};
}

#endif
