#ifndef ASIO_HTTP_REQUEST_H
#define ASIO_HTTP_REQUEST_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <map>
#include <string>

#include <callbacks.h>
#include <net/http_response.h>

class http_request
{
public:
    http_request(boost::asio::streambuf &buffer);
    void set_resource(const std::string &uri);
    void set_verb(const std::string &verb);
    std::string &operator[](const std::string &field);
    void set_body(const std::string &body);
    template<typename AsyncStream>
    void async_send(AsyncStream &stream, http_request_cb cb);

private:
    boost::asio::streambuf &buffer;

    std::string resource;
    std::string body;
    std::string verb;
    std::map<std::string, std::string> headers;

    http_response response;
    http_request_cb callback;

    template<typename AsyncStream>
    void read_response(AsyncStream &stream);
    void build_request();
    void set_defaults();
};

template<typename AsyncStream>
void http_request::async_send(AsyncStream &stream, http_request_cb cb)
{
    callback = cb;
    auto write_cb = [this, &stream](auto &ec, auto transferred) {
        if (transferred != body.size() || ec) {
            boost::asio::post(stream.get_executor(), [=]() { callback(ec, {}); });
        } else {
            read_response(stream);
        }
    };
    build_request();
    boost::asio::async_write(stream, boost::asio::buffer(body), write_cb);
}

template<typename AsyncStream>
void http_request::read_response(AsyncStream &stream)
{
    auto mutable_buffer = buffer.prepare(32768);
    auto read_cb = [this, &stream](auto &ec, auto transferred) {
        if (transferred > 0) {
            buffer.commit(transferred);
            response.parse(buffer);
        }
        if (response.is_complete() || (response.wants_all() && ec == boost::asio::error::eof)) {
            boost::asio::post(stream.get_executor(), [=]() { callback(ec, response); });
        } else if (!ec) {
            read_response(stream);
        } else {
            // Error reading, deliver error and whatever we have so far
            boost::asio::post(stream.get_executor(), [=]() { callback(ec, response); });
        }
    };
    stream.async_read_some(mutable_buffer, read_cb);
}

#endif
