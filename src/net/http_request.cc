#include <iostream>

#include <net/http_request.h>

http_request::http_request(boost::asio::io_context &io, const std::string &host, int,
                           const std::string &resource)
    : sock{io}, resource{resource}, host{host}, bytes_to_write{0}
{
    boost::asio::ip::tcp::resolver resolver{io};
    boost::asio::ip::tcp::resolver::query query{host, "http"};
    auto loc = resolver.resolve(query);
    auto callback = [=](auto &ec) { on_connect(ec); };
    sock.async_connect(*loc, callback);
}

void http_request::on_connect(const boost::system::error_code &e)
{
    if (e)
        throw std::runtime_error("Could not connect to " + host);

    // We are now connected!
    std::string req = "GET " + resource + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";
    bytes_to_write = req.size();
    auto callback = [=](auto &ec, auto transferred) { on_write(ec, transferred); };
    sock.async_write_some(boost::asio::buffer(req.data(), req.size()), callback);
}

void http_request::on_write(const boost::system::error_code &e, size_t wrote)
{
    if (e) {
        throw std::runtime_error("Write: unexpected error " + e.message());
    }
    if (wrote <= bytes_to_write)
        bytes_to_write -= wrote;
    else
        throw std::runtime_error("Write: wrote more bytes than expected: " + std::to_string(wrote));

    // Write completed... Time to forward to http_response to parse the incoming message
    if (bytes_to_write == 0) {
    }
}
