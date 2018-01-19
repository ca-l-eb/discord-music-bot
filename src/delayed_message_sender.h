#ifndef CMD_DISCORD_DELAYED_MESSAGE_SENDER_H
#define CMD_DISCORD_DELAYED_MESSAGE_SENDER_H

#include <boost/asio.hpp>
#include <deque>
#include <string>

#include <callbacks.h>
#include <net/websocket.h>

namespace discord
{
class delayed_message_sender
{
public:
    delayed_message_sender(boost::asio::io_context &service, websocket &websocket, int delay_ms);
    void safe_send(const std::string &s, transfer_cb c);

private:
    boost::asio::io_context &ctx;
    boost::asio::deadline_timer send_timer;
    boost::asio::io_context::strand timer_strand;
    websocket &websock;
    std::deque<std::string> write_queue;
    std::deque<transfer_cb> callback_queue;
    int delay_ms;

    void enqueue_message(std::string s, transfer_cb c);
    void async_wait_for_timer_then_send();
    void packet_send_done(const boost::system::error_code &e, size_t transferred);
};
}

#endif
