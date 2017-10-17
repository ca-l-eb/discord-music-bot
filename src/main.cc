#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <cmd/http_pool.h>
#include <cmd/websocket.h>
#include <tls_socket.h>
#include <thread>
#include "api.h"
#include "gateway.h"

int main(int argc, char *argv[])
{
    using namespace cmd::discord::gateway;

    if (argc < 1) {
        std::cerr << "Usage: " << argv[0] << "<bot login file>\n";
        return EXIT_FAILURE;
    }
    std::string token;
    std::ifstream ifs{argv[1]};
    if (!ifs) {
        std::cerr << "Could not open file " << argv[1] << "\n";
        return EXIT_FAILURE;
    }
    std::getline(ifs, token);
    ifs.close();
    if (token.length() != 59) {
        std::cerr << "Invalid token. Token should be 59 characters long\n";
        return EXIT_FAILURE;
    }
    try {
        auto connection = std::make_shared<cmd::tls_socket>();
        connection->connect("gateway.discord.gg", 443);
        auto stream = cmd::stream{connection};

        cmd::websocket::socket sock{"/?v=6&encoding=json", stream};
        sock.connect();

        cmd::discord::api api{token};
        gateway gateway{sock, token};

        gateway.register_listener(op_recv::dispatch, event_listener::base::make<event_listener::echo_listener>());
        gateway.register_listener(op_recv::dispatch, event_listener::base::make<event_listener::hello_responder>(&api));


        // Get first n events each time calling any bound event listeners
        for (int i = 0; i < 20; i++)
            gateway.next_event();

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
