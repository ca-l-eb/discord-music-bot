#ifndef CMD_DISCORD_HEARTBEATER_H
#define CMD_DISCORD_HEARTBEATER_H

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <json.hpp>

namespace discord
{
class heartbeater
{
public:
    heartbeater(boost::asio::io_context &ctx) : timer{ctx}, heartbeat_interval{0}, acked{true} {}

    ~heartbeater()
    {
        timer.cancel();
    }

    template<typename Beatable>
    void on_hello(const nlohmann::json &data, Beatable &b);

    void on_heartbeat_ack()
    {
        acked = true;
    }

    void cancel()
    {
        timer.cancel();
    }

private:
    boost::asio::deadline_timer timer;
    int heartbeat_interval;
    bool acked;

    template<typename Beatable>
    void start_heartbeat_timer(Beatable &b);

    template<typename Beatable>
    void on_timer_fire(const boost::system::error_code &e, Beatable &b);
};
}

template<typename Beatable>
void discord::heartbeater::on_hello(const nlohmann::json &data, Beatable &b)
{
    if (!data.is_null()) {
        if (data["heartbeat_interval"].is_number()) {
            heartbeat_interval = data["heartbeat_interval"].get<int>();
            // Cancel any previous timer waiting, before resuming with new heartbeat_interval
            timer.cancel();
            start_heartbeat_timer(b);
        }
    }
}

template<typename Beatable>
void discord::heartbeater::start_heartbeat_timer(Beatable &b)
{
    timer.expires_from_now(boost::posix_time::milliseconds(heartbeat_interval));
    auto callback = [this, &b](auto &ec) { on_timer_fire(ec, b); };
    timer.async_wait(callback);
}

template<typename Beatable>
void discord::heartbeater::on_timer_fire(const boost::system::error_code &e, Beatable &b)
{
    if (e || heartbeat_interval < 0) {
        // Timer was cancelled, dont fire the heartbeat
    } else {
        if (acked) {
            b.heartbeat();
            acked = false;
            start_heartbeat_timer(b);
        } else {
            // Last heartbeat was not ACKed... Error
            // TODO: close the WebSocket connection with non 1000 code, as Discord reference says.
            // then reconnect and try to resume connection
        }
    }
}

#endif
