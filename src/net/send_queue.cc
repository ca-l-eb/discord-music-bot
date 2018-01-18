#include <net/send_queue.h>

send_queue::send_queue(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream,
                            boost::asio::io_context &ioc, bool secure)
    : stream{stream}, io{ioc}, secure{secure}, write_strand{ioc}
{
}

void send_queue::enqueue_message(std::vector<uint8_t> v, message_sent_callback c)
{
    auto callback = [=]() {
        bool write_in_progess = !write_queue.empty();
        write_queue.push_back(std::move(v));
        callback_queue.push_back(std::move(c));
        if (!write_in_progess) {
            start_packet_send();
        }
    };
    auto wrapped = boost::asio::bind_executor(write_strand, callback);
    io.post(wrapped);
}

void send_queue::start_packet_send()
{
    auto callback = [=](auto &ec, auto transferred) { packet_send_done(ec, transferred); };
    auto send_complete_callback = boost::asio::bind_executor(write_strand, callback);
    if (secure)
        boost::asio::async_write(stream, boost::asio::buffer(write_queue.front()),
                                 send_complete_callback);
    else
        boost::asio::async_write(stream.next_layer(), boost::asio::buffer(write_queue.front()),
                                 send_complete_callback);
}

void send_queue::packet_send_done(const boost::system::error_code &ec, size_t transferred)
{
    auto &callback = callback_queue.front();
    io.post([=]() { callback(ec, transferred); });
    write_queue.pop_front();
    callback_queue.pop_front();

    // If there wasn't an error, try to send the next packet if it exists
    if (!ec) {
        if (!write_queue.empty()) {
            start_packet_send();
        }
    }
}
