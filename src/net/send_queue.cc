#include <net/send_queue.h>

send_queue::send_queue(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream, bool secure)
    : stream{stream}, secure{secure}, write_strand{stream.get_io_context()}
{
}

void send_queue::enqueue_message(std::vector<uint8_t> v, transfer_cb c)
{
    auto self = shared_from_this();
    auto callback = [self, v, c]() {
        bool write_in_progess = !self->write_queue.empty();
        self->write_queue.push_back(std::move(v));
        self->callback_queue.push_back(std::move(c));
        if (!write_in_progess) {
            self->start_packet_send();
        }
    };
    auto wrapped = boost::asio::bind_executor(write_strand, callback);
    boost::asio::post(wrapped);
}

void send_queue::start_packet_send()
{
    auto self = shared_from_this();
    auto callback = [self](auto &ec, auto transferred) { self->packet_send_done(ec, transferred); };
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
    auto callback = callback_queue.front();
    boost::asio::post(stream.get_executor(), [=]() { callback(ec, transferred); });
    write_queue.pop_front();
    callback_queue.pop_front();

    // If there wasn't an error, try to send the next packet if it exists
    if (!ec) {
        if (!write_queue.empty()) {
            start_packet_send();
        }
    }
}
