#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdint>
#include <string>

using message_received_callback =
    std::function<void(const boost::system::error_code &, const uint8_t *, size_t)>;

using message_sent_callback = std::function<void(const boost::system::error_code &, size_t)>;

#include <net/http_response.h>

class send_queue;

static const std::string websocket_guid{"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

enum class websocket_error {
    websocket_connection_closed = 1,
    upgrade_failed,
    bad_upgrade_key,
    no_upgrade_key,
    server_masked_data,
};

class websocket_category : public boost::system::error_category
{
public:
    virtual const char *name() const noexcept
    {
        return "WebSocket";
    }
    virtual std::string message(int ev) const noexcept
    {
        switch (websocket_error(ev)) {
            case websocket_error::websocket_connection_closed:
                return "The WebSocket connection has closed";
            case websocket_error::server_masked_data:
                return "The server sent masked data";
            case websocket_error::upgrade_failed:
                return "WebSocket upgrade failed";
            case websocket_error::bad_upgrade_key:
                return "The server sent a bad Sec-WebSocket-Accept";
            case websocket_error::no_upgrade_key:
                return "No Sec-WebSocket-Accept returned by server";
        }
        return "Unknown WebSocket error";
    }
    virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept
    {
        return &code.category() == this && static_cast<int>(code.value()) == condition;
    }
};

const boost::system::error_category &websocket_error_category();
boost::system::error_code make_error_code(websocket_error code) noexcept;

enum class websocket_opcode : int8_t {
    none = -1,
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA
};

enum class websocket_status_code : uint16_t {
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

class frame_parser
{
public:
    frame_parser();
    void parse(boost::asio::streambuf &source);
    void reset();
    bool is_frame_complete();
    bool is_fin();
    bool is_masked();
    websocket_opcode frame_type();
    uint64_t get_size();
    bool is_new_state();

private:
    enum class parse_state { begin, length, frame_done } state;
    bool fin_frame, masked;
    uint8_t payload_length;
    uint64_t size;

    websocket_opcode f_type;

    void parse_frame(boost::asio::streambuf &buf);
    void parse_payload_length(boost::asio::streambuf &buf);
};

class websocket
{
public:
    explicit websocket(boost::asio::io_context &ioc);
    websocket(const websocket &) = delete;
    websocket &operator=(const websocket &) = delete;
    ~websocket();
    void async_connect(const std::string &host, const std::string &service,
                       const std::string &resource, boost::asio::ip::tcp::resolver &resolver,
                       message_sent_callback c);
    void async_connect(const std::string &url, boost::asio::ip::tcp::resolver &resolver,
                       message_sent_callback c);
    void async_send(const std::string &str, message_sent_callback c);
    void async_send(const void *buffer, size_t len, message_sent_callback c);
    void async_next_message(message_received_callback c);

    void close(websocket_status_code = websocket_status_code::normal);
    uint16_t close_code();

private:
    boost::asio::io_context &io;
    boost::asio::ssl::context ctx;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream;
    boost::asio::streambuf buffer;
    std::string host, resource;
    bool secure_connection;

    std::unique_ptr<send_queue> queue;
    http_response response;
    std::string expected_accept;
    frame_parser parser;

    uint16_t close_status;
    message_received_callback sender_callback;
    message_sent_callback connect_callback;

    using endpoint_iterator = boost::asio::ip::tcp::resolver::iterator;

    void on_resolve(const boost::system::error_code &ec, endpoint_iterator it);
    void on_connect(const boost::system::error_code &ec, endpoint_iterator it);
    void on_websocket_upgrade_sent(const boost::system::error_code &ec);
    void on_websocket_upgrade_receive(const boost::system::error_code &ec, size_t transferred);
    void on_websocket_data_receive(const boost::system::error_code &ec, size_t transferred);
    void send_upgrade();
    void check_websocket_upgrade();

    void do_handshake();

    void build_frame_and_send_async(const void *data, size_t len, websocket_opcode op,
                                    message_sent_callback);
    void check_parser_state();

    void enqueue_read(size_t amount, message_sent_callback c);
    void handle_frame();

    void pong(const uint8_t *msg, size_t len);
};

template<>
struct boost::system::is_error_code_enum<websocket_error> : public boost::true_type {
};

#endif
