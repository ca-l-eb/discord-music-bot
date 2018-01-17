#include <boost/asio.hpp>
#include <experimental/coroutine>
#include <iostream>

#include "/home/dechant/code/cc/coroutines/future_adapter.h"

namespace ba = boost::asio;
namespace stdx = std::experimental;
namespace ip = boost::asio::ip;

using endpoint_iterator = ip::tcp::resolver::iterator;

auto resume = [](stdx::coroutine_handle<> h) { h.resume(); };

struct transfer_result {
    boost::system::error_code ec = {};
    size_t transferred = 0;
};

struct resolve_result {
    boost::system::error_code ec = {};
    endpoint_iterator it;
};

template<typename AsyncReadStream, typename MutableBufferSequence>
auto co_async_read(AsyncReadStream &s, MutableBufferSequence b)
{
    struct Awaitable {
        AsyncReadStream &s;
        MutableBufferSequence b;
        transfer_result res;

        bool await_ready() { return false; }

        void await_suspend(stdx::coroutine_handle<> h)
        {
            auto callback = [=](auto &ec, auto transferred) {
                this->res.ec = ec;
                this->res.transferred = transferred;
                resume(h);
            };
            ba::async_read(s, b, callback);
        }

        auto await_resume() { return res; }
    };
    return Awaitable{s, b, {}};
}

template<typename AsyncWriteStream, typename BufferSequence>
auto co_async_write(AsyncWriteStream &s, BufferSequence b)
{
    struct Awaitable {
        AsyncWriteStream &s;
        BufferSequence b;
        transfer_result res;

        bool await_ready() { return false; }

        void await_suspend(stdx::coroutine_handle<> h)
        {
            auto callback = [=](auto &ec, auto transferred) {
                this->res.ec = ec;
                this->res.transferred = transferred;
                resume(h);
            };
            ba::async_write(s, b, callback);
        }

        auto await_resume() { return res; }
    };
    return Awaitable{s, b, {}};
}

auto co_async_resolve(const std::string &host, const std::string &service, ba::io_context &ioc)
{
    struct Awaitable {
        ip::tcp::resolver::query query;
        ip::tcp::resolver resolver;

        resolve_result res;

        bool await_ready() { return false; }

        void await_suspend(stdx::coroutine_handle<> h)
        {
            auto callback = [=](auto &ec, endpoint_iterator it) {
                res.ec = ec;
                res.it = it;
                resume(h);
            };
            resolver.async_resolve(query, callback);
        }

        auto await_resume() { return res; }
    };
    return Awaitable{ip::tcp::resolver::query{host, service}, ip::tcp::resolver{ioc}, {}};
}

template<typename Socket>
auto co_async_connect(Socket &sock, endpoint_iterator it)
{
    struct Awaitable {
        Socket &sock;
        ip::tcp::endpoint endpoint;

        boost::system::error_code ec;

        bool await_ready() { return false; }

        void await_suspend(stdx::coroutine_handle<> h)
        {
            auto callback = [this, h](auto &ec) { this->ec = ec; resume(h); };
            sock.async_connect(endpoint, callback);
        }

        auto await_resume() { return ec; }
    };
    return Awaitable{sock, *it, {}};
}

std::future<size_t> connect(const std::string &host, const std::string &service, const std::string &query, ba::io_context &ioc)
{
    auto ret = co_await co_async_resolve(host, service, ioc);
    if (ret.ec) {
        std::cerr << "host resolve error: " << ret.ec.message() << "\n";
        co_return 0;
    }
    std::cout << "resolved host\n";
    ip::tcp::socket sock{ioc};
    for (;;) {
        if (ret.it == ip::tcp::resolver::iterator()) {
            std::cerr << "no suitable connections\n";
            co_return 0; 
        }
        std::cout << "connecting to " << ret.it->endpoint().address().to_string() << " ... ";
        auto connect_error = co_await co_async_connect(sock, ret.it++);
        if (!connect_error)
            break;
        std::cout << "fail\n";
    }
    std::cout << "success\n";

    std::string msg;
    msg += "GET " + query + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "Connection: close\r\n"
           "\r\n";

    char buf[4096];
    std::memcpy(buf, msg.c_str(), msg.size());

    transfer_result res;

    res = co_await co_async_write(sock, ba::buffer(buf, msg.size()));
    std::cout << "wrote " << res.transferred << " bytes\n";
    if (res.ec) {
        std::cerr << "write error: " << res.ec.message() << "\n";
        co_return 0;
    }

    size_t total = 0;
    for (;;) {
        res = co_await co_async_read(sock, ba::buffer(buf));
        std::cout << "read " << res.transferred << " bytes\n";
        if (res.transferred > 0) {
            std::cout.write(buf, res.transferred);
            std::cout << "\n";
            total += res.transferred;
        }
        if (res.ec) {
            std::cerr << "read error: " << res.ec.message() << "\n";
            break;
        }
    }
    co_return total;
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <query>\n";
        exit(1);
    }
    ba::io_context ioc;

    const std::string host = argv[1];
    const std::string service = argv[2];
    const std::string query = argv[3];

    std::future<size_t> fut = connect(host, service, query, ioc);

    std::cout << "io_context::run() starting\n";
    ioc.run();
    std::cout << "io_context::run() finished\n";
    std::cout << "fut.get() returned " << fut.get() << "\n";
}
