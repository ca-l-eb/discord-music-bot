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
//    std::time_t now_t = std::time(nullptr);
//
//    std::cout << now_t << "\n";
//
//    auto now = std::chrono::system_clock::now();
//    auto second_from_now = (now + std::chrono::seconds{5});
//    std::cout << now.time_since_epoch().count() << " " << second_from_now.time_since_epoch().count() << "\n";
//    std::this_thread::sleep_until(second_from_now);
//    auto now2 = std::chrono::system_clock::now();
//    std::cout << now.time_since_epoch().count() << "\n";
//
//    auto n = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now).count();
//    std::cout << n << "\n";
//
//    std::cout << "Waiting for now2\n";
//    std::this_thread::sleep_until(now2);
//    std::cout << "Done\n";
//
//    now_t = std::time(nullptr);
//    std::cout << now_t << "\n";
//
//    exit(0);
    std::string token;
    std::ifstream ifs{argv[1]};
    if (!ifs) {
        std::cerr << "Usage: " << argv[0] << "<bot login file>\n";
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
        cmd::discord::gateway gateway{sock, token};
        auto echo_event_listener = [](nlohmann::json &json, const std::string &type) -> void {
            std::cout << "Type: " << type << " ";
            if (!json.is_null())
                std::cout << "json: " << json;
            std::cout << "\n";
        };
        auto hello_responder = [&](nlohmann::json &json, const std::string &type) -> void {
            if (json.is_null())
                return;
            if (type == "MESSAGE_CREATE") {
                std::string channel = json["channel_id"];
                std::string content = json["content"];
                std::string user = json["author"]["username"];
                if (content.find("hi") != std::string::npos ) {
                    api.send_message(channel, "hey " + user);
                }
            }
        };
        gateway.register_listener(cmd::discord::gateway_op_recv::dispatch, echo_event_listener);
        gateway.register_listener(cmd::discord::gateway_op_recv::dispatch, hello_responder);


        // Get first n events each time calling any bound event listeners
        for (int i = 0; i < 100; i++)
            gateway.next_event();

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
