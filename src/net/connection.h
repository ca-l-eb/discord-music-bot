#ifndef DISCORD_CONNECTION_H
#define DISCORD_CONNECTION_H

#include "aliases.h"
#include "callbacks.h"
#include "net/resource_parser.h"

namespace discord
{
class connection
{
public:
    connection(boost::asio::io_context &io, ssl::context &tls);
    void connect(const std::string &url, error_cb c);
    void disconnect();
    void read(json_cb c);
    void send(const std::string &s, transfer_cb c);
    int close_code();

private:
    boost::asio::io_context &ctx;
    tcp::resolver resolver;
    secure_websocket websock;
    boost::beast::multi_buffer buffer;
    error_cb connect_cb;
    resource_parser::parsed_url info;

    void on_resolve(const boost::system::error_code &ec, tcp::resolver::iterator it);
    void on_connect(const boost::system::error_code &ec, tcp::resolver::iterator);
    void on_tls_handshake(const boost::system::error_code &ec);
    void on_websocket_handshake(const boost::system::error_code &ec);
};
}  // namespace discord

#endif
