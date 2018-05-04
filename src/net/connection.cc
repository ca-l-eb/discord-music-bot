#include <boost/asio/connect.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include "connection.h"
#include "errors.h"
#include "net/resource_parser.h"

discord::connection::connection(boost::asio::io_context &io, ssl::context &tls)
    : ctx{io}, resolver{ctx}, websock{ctx, tls}
{
}

void discord::connection::connect(const std::string &url, error_cb c)
{
    connect_cb = c;

    info = resource_parser::parse(url);

    auto query = tcp::resolver::query{info.host, std::to_string(info.port)};
    resolver.async_resolve(query, [this](auto &ec, auto it) { on_resolve(ec, it); });
}

void discord::connection::disconnect()
{
    auto ec = boost::system::error_code{};
    websock.close(boost::beast::websocket::close_code::normal, ec);
    websock.lowest_layer().close(ec);
}

void discord::connection::read(json_cb c)
{
    websock.async_read(buffer, [c, this](auto &ec, auto transferred) {
        auto json = nlohmann::json{};
        if (!ec) {
            auto data = boost::beast::buffers_to_string(buffer.data());
            json = nlohmann::json::parse(data);
        }
        c(ec, json);
        buffer.consume(transferred);
    });
}

void discord::connection::send(const std::string &s, transfer_cb c)
{
    // TODO: use strand + message queue + timer for delay
    auto ec = boost::system::error_code{};
    auto wrote = websock.write(boost::asio::buffer(s), ec);
    c(ec, wrote);
}

void discord::connection::on_resolve(const boost::system::error_code &ec,
                                     tcp::resolver::iterator it)
{
    if (ec) {
        connect_cb(ec);
    } else {
        boost::asio::async_connect(websock.next_layer().next_layer(), it,
                                   [this](auto &ec, auto it) { on_connect(ec, it); });
    }
}

void discord::connection::on_connect(const boost::system::error_code &ec, tcp::resolver::iterator)
{
    if (ec) {
        connect_cb(ec);
    } else {
        websock.next_layer().set_verify_mode(ssl::verify_peer);
        websock.next_layer().set_verify_callback(ssl::rfc2818_verification(info.host));
        websock.next_layer().async_handshake(ssl::stream_base::client,
                                             [this](auto &ec) { on_tls_handshake(ec); });
    }
}

void discord::connection::on_tls_handshake(const boost::system::error_code &ec)
{
    if (ec) {
        connect_cb(ec);
    } else {
        websock.async_handshake(info.host, info.resource,
                                [this](auto &ec) { on_websocket_handshake(ec); });
    }
}

void discord::connection::on_websocket_handshake(const boost::system::error_code &ec)
{
    connect_cb(ec);
}

int discord::connection::close_code()
{
    if (websock.is_open())
        return -1;
    return websock.reason().code;
}
