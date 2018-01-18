#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <api.h>
#include <events/echo_listener.h>
#include <events/hello_responder.h>
#include <gateway.h>

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

        boost::asio::io_context ctx;
        boost::asio::ip::tcp::resolver resolver{ctx};
        //        discord::api api{token};
        discord::gateway gateway{ctx, token, resolver};

        //        gateway.add_listener("ALL", "echo",
        //        std::make_shared<discord::echo_listener>());
        //        gateway.add_listener("MESSAGE_CREATE", "hello_responder",
        //                             std::make_shared<discord::hello_responder>(api));

        ctx.run();
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
