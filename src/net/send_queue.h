#ifndef CMD_SEND_QUEUE_H
#define CMD_SEND_QUEUE_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <deque>
#include <memory>
#include <vector>

#include <callbacks.h>
#include <net/websocket.h>

class send_queue : public std::enable_shared_from_this<send_queue>
{
public:
    send_queue(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream, bool secure);
    void enqueue_message(std::vector<uint8_t> v, transfer_cb c);

private:
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> &stream;
    bool secure;
    boost::asio::io_context::strand write_strand;
    std::deque<std::vector<uint8_t>> write_queue;
    std::deque<transfer_cb> callback_queue;

    void start_packet_send();
    void packet_send_done(const boost::system::error_code &ec, size_t transferred);
};

#endif
