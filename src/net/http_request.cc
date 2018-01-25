#ifndef NET_HTTP_REQUEST_CC
#define NET_HTTP_REQUEST_CC

#include <net/http_request.h>

http_request::http_request(boost::asio::streambuf &buffer) : buffer{buffer}
{
    headers["Connection"] = "keep-alive";
}

void http_request::set_resource(const std::string &uri)
{
    resource = uri;
}

void http_request::set_verb(const std::string &verb)
{
    this->verb = verb;
}

std::string &http_request::operator[](const std::string &field)
{
    return headers[field];
}

void http_request::set_body(const std::string &body)
{
    this->body = body;
}

void http_request::build_request()
{
    set_defaults();

    std::string request = verb + " " + resource + " HTTP/1.1\r\n";
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request += it->first + ": " + it->second + "\r\n";
    }
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n";

    body = request + body;
}

void http_request::set_defaults()
{
    if (verb.empty())
        verb = "GET";
    if (resource.empty())
        resource = "/";
    else if (resource[0] != '/')
        resource = '/' + resource;
}

#endif
