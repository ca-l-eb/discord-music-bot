#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include <callbacks.h>
#include <error.h>
#include <net/http_response.h>

class send_queue;

static const std::string websocket_guid{"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

class websocket : public std::enable_shared_from_this<websocket>
{
public:
    enum class opcode : int8_t {
        none = -1,
        continuation = 0x0,
        text = 0x1,
        binary = 0x2,
        close = 0x8,
        ping = 0x9,
        pong = 0xA
    };

    class frame_parser
    {
    public:
        frame_parser();
        void parse(boost::asio::streambuf &source);
        void reset();
        bool is_frame_complete();
        bool is_fin();
        bool is_masked();
        opcode frame_type();
        uint64_t get_size();
        bool is_new_state();

    private:
        enum class parse_state { begin, length, frame_done } state;
        bool fin_frame, masked;
        uint8_t payload_length;
        uint64_t size;

        opcode f_type;

        void parse_frame(boost::asio::streambuf &buf);
        void parse_payload_length(boost::asio::streambuf &buf);
    };

    enum class status_code : uint16_t {
        normal = 1000,
        going_away = 1001,
        protocol_error = 1002,
        data_error = 1003,  // e.g. got binary when expected text
        reserved = 1004,
        no_status_code_present = 1005,  // don't send
        closed_abnormally = 1006,       // don't send
        inconsistent_data = 1007,
        policy_violation = 1008,  // generic code return
        message_too_big = 1009,
        extension_negotiation_failure = 1010,
        unexpected_error = 1011,
        tls_handshake_error = 1015  // don't send
    };

    explicit websocket(boost::asio::io_context &ioc);
    websocket(const websocket &) = delete;
    websocket &operator=(const websocket &) = delete;
    ~websocket();
    void async_connect(const std::string &host, const std::string &service,
                       const std::string &resource, boost::asio::ip::tcp::resolver &resolver,
                       error_cb c);
    void async_connect(const std::string &url, boost::asio::ip::tcp::resolver &resolver,
                       error_cb c);
    void async_send(const std::string &str, transfer_cb c);
    void async_send(const void *buffer, size_t len, transfer_cb c);
    void async_next_message(data_cb c);

    void close(websocket::status_code = websocket::status_code::normal);
    uint16_t close_code();
    boost::asio::io_context &get_io_context();

private:
    boost::asio::ssl::context ctx;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream;
    boost::asio::streambuf buffer;
    std::string host, resource;
    bool secure_connection;

    std::shared_ptr<send_queue> queue;
    http_response response;
    frame_parser parser;
    std::string expected_accept;

    uint16_t close_status;
    data_cb sender_callback;
    error_cb connect_callback;

    void on_resolve(const boost::system::error_code &ec,
                    boost::asio::ip::tcp::resolver::iterator it);
    void on_connect(const boost::system::error_code &ec,
                    boost::asio::ip::tcp::resolver::iterator it);
    void on_upgrade_sent(const boost::system::error_code &ec);
    void on_upgrade_receive(const boost::system::error_code &ec, size_t transferred);
    void on_data_receive(const boost::system::error_code &ec, size_t transferred);
    void send_upgrade();
    void check_upgrade();

    void do_handshake();

    void build_frame_and_send_async(const void *data, size_t len, websocket::opcode op,
                                    transfer_cb);
    void check_parser_state();

    void enqueue_read(size_t amount, transfer_cb c);
    void handle_frame();

    void pong(const uint8_t *msg, size_t len);
};

#endif
