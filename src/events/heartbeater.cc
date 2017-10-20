#include <iostream>

#include "heartbeater.h"

cmd::discord::heartbeater::heartbeater(cmd::discord::gateway *gateway)
    : gateway{gateway}, heartbeat_interval{0}, first{true}
{
}

cmd::discord::heartbeater::~heartbeater()
{
    // Notify thread we are closing, then join it
    notify();
    join();
}

void cmd::discord::heartbeater::heartbeat_loop()
{
    while (true) {
        std::unique_lock<std::mutex> lock{thread_mutex};
        loop_variable.wait_for(lock, std::chrono::milliseconds{heartbeat_interval});
        if (heartbeat_interval < 0)
            break;
        gateway->heartbeat();
    }
}

void cmd::discord::heartbeater::handle(gtw_op_recv op, const nlohmann::json &data,
                                       const std::string &)
{
    if (op == gtw_op_recv ::hello && !data.is_null()) {
        if (data["heartbeat_interval"].is_number())
            heartbeat_interval = data["heartbeat_interval"].get<int>();
        std::cout << "Heartbeating every " << heartbeat_interval << " ms\n";
        if (first)
            heartbeat_thread = std::thread{&heartbeater::heartbeat_loop, this};
        first = false;
    } else if (op == gtw_op_recv ::heartbeat_ack) {
    }
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