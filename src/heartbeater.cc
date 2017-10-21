#include <iostream>

#include <gateway.h>
#include "heartbeater.h"

cmd::discord::heartbeater::heartbeater() : heartbeat_interval{0}, first{true}, acked{true} {}

cmd::discord::heartbeater::~heartbeater()
{
    // Notify thread we are closing, then join it
    notify();
    join();
}

void cmd::discord::heartbeater::heartbeat_loop(cmd::discord::beatable *b)
{
    while (true) {
        std::unique_lock<std::mutex> lock{thread_mutex};
        loop_variable.wait_for(lock, std::chrono::milliseconds{heartbeat_interval});
        if (heartbeat_interval < 0)
            break;
        if (acked) {
            b->heartbeat();
            acked = false;
        } else {
            // Last heartbeat was not ACKed... Error
            // TODO: close the WebSocket connection with non 1000 code, as Discord reference says.
            // then reconnect and try to resume connection
        }
    }
}

void cmd::discord::heartbeater::on_hello(cmd::discord::beatable &b, const nlohmann::json &data)
{
    if (!data.is_null()) {
        if (data["heartbeat_interval"].is_number()) {
            heartbeat_interval = data["heartbeat_interval"].get<int>();
            std::cout << "Heartbeating every " << heartbeat_interval << " ms\n";

            // Only spawn a single heartbeat thread, but allow the interval to change
            if (first) {
                heartbeat_thread = std::thread{&heartbeater::heartbeat_loop, this, &b};
                first = false;
            }
        }
    }
}

void cmd::discord::heartbeater::on_heartbeat_ack()
{
    acked = true;
}

void cmd::discord::heartbeater::notify()
{
    heartbeat_interval = -1;
    loop_variable.notify_all();
}

void cmd::discord::heartbeater::join()
{
    if (heartbeat_thread.joinable())
        heartbeat_thread.join();
}