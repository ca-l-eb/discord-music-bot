#include <iostream>

#include <delayed_message_sender.h>

discord::delayed_message_sender::delayed_message_sender(std::shared_ptr<websocket> websocket,
                                                        int delay_ms)
    : send_timer{websocket->get_io_context()}
    , timer_strand{websocket->get_io_context()}
    , websock{websocket}
    , delay_ms{delay_ms}
{
    // We want the first send to be immediate, after that, we reset upon getting notice of a
    // sucessfull send
    send_timer.expires_from_now(boost::posix_time::milliseconds(0));
}

void discord::delayed_message_sender::safe_send(const std::string &s, transfer_cb c)
{
    auto callback = [=]() { enqueue_message(s, c); };
    boost::asio::post(boost::asio::bind_executor(timer_strand, callback));
}

// This idea is from CppCon 2016 Talk by Michael Caisse "Asynchronous IO with Boost.Asio"
void discord::delayed_message_sender::enqueue_message(std::string s, transfer_cb c)
{
    bool write_in_progess = !write_queue.empty();
    write_queue.push_back(std::move(s));
    callback_queue.push_back(std::move(c));
    if (!write_in_progess) {
        async_wait_for_timer_then_send();
    }
}

void discord::delayed_message_sender::async_wait_for_timer_then_send()
{
    // Want to make sure delay_ms have passed since last message sent
    send_timer.async_wait([&](const boost::system::error_code &e) {
        if (e) {
            // TODO: handler error or cancelled timer
            std::cerr << "timer error: " << e.message() << "\n";
        } else {
            // Wrap the handler with timer_strand to make sure it doesn't corrupt the queue if this
            // is multithreaded in the future.
            auto callback = [=](auto &ec, auto transferred) {
                send_timer.expires_from_now(boost::posix_time::milliseconds(delay_ms));
                packet_send_done(ec, transferred);
            };
            websock->async_send(write_queue.front(),
                                boost::asio::bind_executor(timer_strand, callback));
        }
    });
}

void discord::delayed_message_sender::packet_send_done(const boost::system::error_code &e,
                                                       size_t transferred)
{
    auto callback = callback_queue.front();
    boost::asio::post(websock->get_io_context(), [=]() { callback(e, transferred); });
    write_queue.pop_front();
    callback_queue.pop_front();

    // If there wasn't an error, try to send the next packet if it exists
    if (e) {
        std::cerr << "delayed_message_sender receive error: " << e.message() << "\n";
    } else {
        if (!write_queue.empty()) {
            async_wait_for_timer_then_send();
        }
    }
}
