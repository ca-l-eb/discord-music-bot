#include <delayed_message_sender.h>
#include <iostream>

cmd::discord::delayed_message_sender::delayed_message_sender(boost::asio::io_service &service,
                                                             cmd::websocket &websocket,
                                                             int delay_ms)
    : service{service}
    , send_timer{service}
    , timer_strand{service}
    , websocket{websocket}
    , delay_ms{delay_ms}
{
    // We want the first send to be immediate, after that, we reset upon getting notice of a
    // sucessfull send
    send_timer.expires_from_now(boost::posix_time::milliseconds(0));
}

void cmd::discord::delayed_message_sender::safe_send(const std::string s,
                                                     cmd::websocket::message_sent_callback c)
{
    service.post(timer_strand.wrap([=]() { queue_message(std::move(s), std::move(c)); }));
}

// This idea is from CppCon 2016 Talk by Michael Caisse "Asynchronous IO with Boost.Asio"
void cmd::discord::delayed_message_sender::queue_message(std::string s,
                                                         cmd::websocket::message_sent_callback c)
{
    bool write_in_progess = !write_queue.empty();
    write_queue.push_back(std::move(s));
    callback_queue.push_back(std::move(c));
    if (!write_in_progess) {
        async_wait_for_timer_then_send();
    }
}

void cmd::discord::delayed_message_sender::async_wait_for_timer_then_send()
{
    // Want to make sure delay_ms have passed since last message sent
    send_timer.async_wait([&](const boost::system::error_code &e) {
        if (e) {
            // TODO: handler error or cancelled timer
            std::cerr << "Timer error: " << e.message() << "\n";
        } else {
            // Wrap the handler with timer_strand to make sure it doesn't corrupt the queue if this
            // is multithreaded in the future.
            websocket.async_send(
                write_queue.front(),
                timer_strand.wrap([&](const boost::system::error_code &e, size_t transferred) {
                    send_timer.expires_from_now(boost::posix_time::milliseconds(delay_ms));
                    packet_send_done(e, transferred);
                }));
        }
    });
}

void cmd::discord::delayed_message_sender::packet_send_done(const boost::system::error_code &e,
                                                            size_t transferred)
{
    auto callback = callback_queue.front();
    service.post([=]() { callback(e, transferred); });
    write_queue.pop_front();
    callback_queue.pop_front();

    // If there wasn't an error, try to send the next packet if it exists
    if (e) {
        std::cerr << "Delayed_message_sender receive error: " << e.message() << "\n";
    } else {
        if (!write_queue.empty()) {
            async_wait_for_timer_then_send();
        }
    }
}
