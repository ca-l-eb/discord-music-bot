#include <iostream>

#include <gateway.h>
#include <heartbeater.h>

discord::heartbeater::heartbeater(boost::asio::io_context &ctx, discord::beatable &b)
    : b{b}, timer{ctx}, heartbeat_interval{0}, acked{true}
{
}

discord::heartbeater::~heartbeater()
{
    timer.cancel();
}

void discord::heartbeater::on_hello(const nlohmann::json &data)
{
    if (!data.is_null()) {
        if (data["heartbeat_interval"].is_number()) {
            heartbeat_interval = data["heartbeat_interval"].get<int>();
            std::cout << "heartbeating every " << heartbeat_interval << " ms\n";
            // Cancel any previous timer waiting, before resuming with new heartbeat_interval
            timer.cancel();
            start_heartbeat_timer();
        }
    }
}

void discord::heartbeater::on_heartbeat_ack()
{
    acked = true;
}

void discord::heartbeater::cancel()
{
    timer.cancel();
}

void discord::heartbeater::start_heartbeat_timer()
{
    timer.expires_from_now(boost::posix_time::milliseconds(heartbeat_interval));
    auto callback = [=](auto &ec) { on_timer_fire(ec); };
    timer.async_wait(callback);
}

void discord::heartbeater::on_timer_fire(const boost::system::error_code &e)
{
    if (e || heartbeat_interval < 0) {
        // Timer was cancelled, dont fire the heartbeat
    } else {
        if (acked) {
            b.heartbeat();
            acked = false;
            start_heartbeat_timer();
        } else {
            // Last heartbeat was not ACKed... Error
            // TODO: close the WebSocket connection with non 1000 code, as Discord reference says.
            // then reconnect and try to resume connection
        }
    }
}
