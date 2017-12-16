#ifndef ASIO_HTTP_REQUEST_H
#define ASIO_HTTP_REQUEST_H

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include <net/http_response.h>

class http_request
{
public:
    http_request(boost::asio::io_service &io, const std::string &host, int port,
                 const std::string &resource);

private:
    boost::asio::ip::tcp::socket sock;
    std::string resource;
    std::string host;

    size_t bytes_to_write;

    std::unique_ptr<http_response> response;

    void on_connect(const boost::system::error_code &e);
    void on_write(const boost::system::error_code &e, size_t wrote);
};

#endif  // ASIO_HTTP_REQUEST_H
