#include <boost/asio/ssl.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <aliases.h>
#include <events/echo_listener.h>
#include <events/hello_responder.h>
#include <gateway.h>
#include <voice/decoding.h>

int main(int argc, char *argv[])
{
    try {
        std::signal(SIGPIPE, SIG_IGN);
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <bot token>\n";
            return EXIT_FAILURE;
        }
        std::string token{argv[1]};
        if (token.length() != 59) {
            std::cerr << "Invalid token. Token should be 59 characters long\n";
            return EXIT_FAILURE;
        }

        av_register_all();  // Initialize libavformat

        boost::asio::io_context ctx;
        ssl::context tls{ssl::context::tls_client};
        tls.set_default_verify_paths();
        tls.set_verify_mode(ssl::context::verify_peer);

        std::make_shared<discord::gateway>(ctx, tls, token)->run();

        ctx.run();
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
