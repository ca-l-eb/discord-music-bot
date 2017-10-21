#include <iostream>
#include <thread>
#include <csignal>

#include <cmd/http_pool.h>
#include <cmd/tls_socket.h>
#include <cmd/websocket.h>

#include <api.h>
#include <events/echo_listener.h>
#include <events/hello_responder.h>
#include <gateway.h>

int main(int argc, char *argv[])
{
    std::signal(SIGPIPE, SIG_IGN);
    if (argc < 1) {
        std::cerr << "Usage: " << argv[0] << "<bot token>\n";
        return EXIT_FAILURE;
    }
    std::string token{argv[1]};
    if (token.length() != 59) {
        std::cerr << "Invalid token. Token should be 59 characters long\n";
        return EXIT_FAILURE;
    }

    try {
        cmd::discord::api api{token};
        cmd::discord::gateway gateway{token};

//        gateway.add_listener("ALL", "echo", std::make_shared<cmd::discord::echo_listener>());
        gateway.add_listener("MESSAGE_CREATE", "hello_responder",
                             std::make_shared<cmd::discord::hello_responder>(api));

        gateway.identify();

//         Run a few events then join voice server
        for (int i = 0; i < 5; i++)
            gateway.next_event();

        gateway.join_voice_server("179378178601517056", "183719700826423298");

        // Get first n events each time calling corresponding bound event listeners
        for (int i = 0; i < 70; i++)
            gateway.next_event();

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
