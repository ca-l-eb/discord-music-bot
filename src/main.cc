#include <signal.h>
#include <cstdlib>
#include <string>
#include <spdlog/spdlog.h>

#include "aliases.h"
#include "audio/decoding.h"
#include "gateway.h"
#include "net/connection.h"

static discord::gateway *gateway_ptr{nullptr};
static boost::asio::io_context *ctx_ptr{nullptr};

void signal_handler(int)
{
    if (gateway_ptr)
        gateway_ptr->disconnect();
    if (ctx_ptr) {
        ctx_ptr->restart();
        ctx_ptr->stop();
    }
}

int main(int argc, char *argv[])
{ 
    try {
        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%^%L%$] %v [%s:%#]");

        if (argc < 2) {
            SPDLOG_INFO("Usage: {} <bot token>", argv[0]);
            return EXIT_FAILURE;
        }
        auto token = std::string{argv[1]};
        if (token.length() != 59) {
            SPDLOG_ERROR("Invalid token. Token should be 59 characters long\n");
            return EXIT_FAILURE;
        }

        signal(SIGINT, signal_handler);

#ifndef FF_API_NEXT
        av_register_all();
#endif
        auto ctx = boost::asio::io_context{};
        auto tls = ssl::context{ssl::context::tls_client};
        tls.set_default_verify_paths();
        tls.set_verify_mode(ssl::context::verify_peer);

        auto gateway_connection = discord::connection{ctx, tls};
        auto gateway = std::make_shared<discord::gateway>(ctx, tls, token, gateway_connection);
        gateway->run();

        gateway_ptr = gateway.get();
        ctx_ptr = &ctx;

        ctx.run();
    } catch (std::exception &e) {
        SPDLOG_CRITICAL("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
