#ifndef CMD_SEND_QUEUE_H
#define CMD_SEND_QUEUE_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <deque>
#include <vector>

#include <net/websocket.h>

class send_queue
{
public:
    send_queue(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream,
               boost::asio::io_context &ioc, bool secure);
    void enqueue_message(std::vector<uint8_t> v, message_sent_callback c);

private:
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream;
    boost::asio::io_context &io;
    bool secure;
    boost::asio::io_context::strand write_strand;
    std::deque<std::vector<uint8_t>> write_queue;
    std::deque<message_sent_callback> callback_queue;

    void start_packet_send();
    void packet_send_done(const boost::system::error_code &ec, size_t transferred);
};

#endif
