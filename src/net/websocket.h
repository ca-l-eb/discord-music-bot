#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <string>

#include <net/http_response.h>
#include <deque>
#include <cstdint>

namespace cmd
{
static const std::string websocket_guid{"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

class websocket
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

    enum class  error {
        websocket_connection_closed = 1,
        upgrade_failed,
        bad_upgrade_key,
        no_upgrade_key,
        server_masked_data,
    };

    class websocket_category : public std::error_category, public boost::system::error_category
    {
    public:
        virtual const char *name() const noexcept
        {
            return "WebSocket";
        }
        virtual std::string message(int ev) const noexcept
        {
            switch (error(ev)) {
                case error::websocket_connection_closed:
                    return "The WebSocket connection has closed";
                case error::server_masked_data:
                    return "The server sent masked data";
                case error::upgrade_failed:
                    return "WebSocket upgrade failed";
                case error::bad_upgrade_key:
                    return "The server sent a bad Sec-WebSocket-Accept";
                case error::no_upgrade_key:
                    return "No Sec-WebSocket-Accept returned by server";
            }
            return "Unknown WebSocket error";
        }
        virtual bool equivalent(const std::error_code &code, int condition) const noexcept
        {
            return &code.category() == this && static_cast<int>(code.value()) == condition;
        }
    };

    static const std::error_category &websocket_error_category()
    {
        static websocket::websocket_category instance;
        return instance;
    }

    static const boost::system::error_category &boost_websocket_error_category()
    {
        static websocket::websocket_category instance;
        return instance;
    }

    static std::error_code make_error_code(websocket::error code) noexcept
    {
        return std::error_code{(int) code, websocket_error_category()};
    }

    static boost::system::error_code boost_make_error_code(websocket::error code) noexcept
    {
        return boost::system::error_code{(int) code, boost_websocket_error_category()};
    }

    using message_received_callback =
    std::function<void(const boost::system::error_code &, const uint8_t *, size_t)>;
    using message_sent_callback = std::function<void(const boost::system::error_code &, size_t)>;

    explicit websocket(boost::asio::io_service &service);
    websocket(const websocket &) = delete;
    websocket &operator=(const websocket &) = delete;
    ~websocket();
    void async_connect(const std::string &host, const std::string &service,
                       const std::string &resource, message_sent_callback c);
    void async_connect(const std::string &url, message_sent_callback c);
    void async_send(const std::string &str, message_sent_callback c);
    void async_send(const void *buffer, size_t len, message_sent_callback c);
    void async_next_message(message_received_callback c);

    void close(websocket::status_code = websocket::status_code::normal);
    uint16_t close_code();

private:
    boost::asio::io_service &io;
    boost::asio::ssl::context ctx;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket;
    boost::asio::streambuf buffer;
    std::string host, resource;
    bool secure_connection;

    // Messages get submitted to the write_queue, wrapped in the write strand
    boost::asio::strand write_strand;
    std::deque<std::vector<uint8_t>> write_queue;
    std::deque<message_sent_callback> callback_queue;

    http_response response;
    std::string expected_accept;

    void queue_message(std::vector<uint8_t> v, message_sent_callback c);
    void start_packet_send();
    void packet_send_done(const boost::system::error_code &e, size_t transferred);

    class frame_parser
    {
    public:
        frame_parser();
        void parse(boost::asio::streambuf &source);
        void reset();
        bool is_frame_complete();
        bool is_fin();
        bool is_masked();
        websocket::opcode frame_type();
        uint64_t get_size();
        bool is_new_state();

    private:
        enum class parse_state { begin, length, frame_done } state;
        bool fin_frame, masked;
        uint8_t payload_length;
        uint64_t size;

        websocket::opcode f_type;

        void parse_frame(boost::asio::streambuf &buf);
        void parse_payload_length(boost::asio::streambuf &buf);
    };
    frame_parser parser;

    uint16_t close_status;
    message_received_callback sender_callback;
    message_sent_callback connect_callback;

    using endpoint_iterator = boost::asio::ip::tcp::resolver::iterator;

    void on_websocket_upgrade_sent(const boost::system::error_code &e);
    void on_websocket_upgrade_receive(const boost::system::error_code &e, size_t transferred);
    void on_websocket_data_receive(const boost::system::error_code &e, size_t transferred);
    void send_upgrade();
    void check_websocket_upgrade();

    void do_handshake();

    void build_frame_and_send_async(const void *data, size_t len, websocket::opcode op,
                                    message_sent_callback);
    size_t get_frame_size(size_t bytes);
    void write_frame_size(uint8_t *frame, size_t size);
    void write_masked_data(const uint8_t *in, uint8_t *out, size_t size);

    void check_parser_state();

    void enqueue_read(size_t amount, message_sent_callback c);
    void handle_frame();

    void pong(const uint8_t *msg, size_t len);
};
}

template<>
struct std::is_error_code_enum<cmd::websocket::error> : public std::true_type {
};

template<>
struct boost::system::is_error_code_enum<cmd::websocket::error> : public boost::true_type {
};

#endif
