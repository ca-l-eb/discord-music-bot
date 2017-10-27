#ifndef CMD_DISCORD_DELAYED_MESSAGE_SENDER_H
#define CMD_DISCORD_DELAYED_MESSAGE_SENDER_H

#include <net/websocket.h>
#include <boost/asio.hpp>
#include <deque>
#include <string>

namespace cmd
{
namespace discord
{
class delayed_message_sender
{
public:
    delayed_message_sender(boost::asio::io_service &service, cmd::websocket &websocket,
                           int delay_ms);
    void safe_send(const std::string s, cmd::websocket::message_sent_callback c);

private:
    boost::asio::io_service &service;
    boost::asio::deadline_timer send_timer;
    boost::asio::strand timer_strand;
    cmd::websocket &websocket;
    std::deque<std::string> write_queue;
    std::deque<cmd::websocket::message_sent_callback> callback_queue;
    int delay_ms;

    void queue_message(std::string s, cmd::websocket::message_sent_callback c);
    void async_wait_for_timer_then_send();
    void packet_send_done(const boost::system::error_code &e, size_t transferred);
};
}
}

#endif
