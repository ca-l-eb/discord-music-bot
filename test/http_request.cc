#include <iostream>
#include <net/http_response.h>
#include <net/http_request.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace ba = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using secure_stream = ssl::stream<tcp::socket>;

void http_request_callback(const boost::system::error_code &ec, http_response response)
{
    std::cout << response.status_code() << " " << response.status_message() << "\n";
    std::cout << "=========================HEADERS=============================\n";
    auto headers = response.headers();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        std::cout << it->first << ": " << it->second << "\n"; 
    }
    std::cout << "===========================BODY==============================\n";
    std::cout << response.body() << "\n";
    std::cout << "body size: " << response.body().size() << "\n";
    if (ec) {
        std::cerr << "http response error: " << ec.message() << "\n";
    }
    if (!response.is_complete())
        std::cout << "incomplete http response\n";
}

int main(int argc, char *argv[]) 
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <service> <uri>\n";
        return 1;
    }
    std::string host = argv[1];
    std::string service = argv[2];
    std::string search_query = argv[3];

    ba::io_context context;
    ba::streambuf buffer;
    tcp::resolver resolver{context};
    tcp::resolver::query query{host, service};

    auto endpoints = resolver.resolve(query);
    std::cout << endpoints->host_name() << " resolves to:\n";
    for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
        std::cout << it->endpoint().address().to_string() << "\n";
    }

    ssl::context ssl_context{ssl::context::tls_client};
    secure_stream stream{context, ssl_context};
    boost::system::error_code ec;
    ba::connect(stream.lowest_layer(), endpoints, ec);
    if (ec) {
        std::cerr << "connect error: " << ec.message() << "\n";
        return 1;
    }

    http_request request{buffer};
    request.set_resource(search_query);
    request["Host"] = host;
    if (service == "https" || service == "443") {
        ssl_context.set_default_verify_paths();
        stream.set_verify_mode(ssl::verify_peer);
        stream.set_verify_callback(ssl::rfc2818_verification(host));
        stream.handshake(secure_stream::client, ec);
        if (ec) {
            std::cerr << "handshake error: " << ec.message() << "\n";
            return 1;
        }
        request.async_send(stream, http_request_callback);
    } else {
        request.async_send(stream.next_layer(), http_request_callback);
    }
    context.run();
}
