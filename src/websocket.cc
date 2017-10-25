#include <openssl/sha.h>
#include <boost/bind.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

#include "base64.h"
#include "websocket.h"

using random_byte_engine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;
thread_local random_byte_engine generator{
    static_cast<unsigned char>(std::chrono::system_clock::now().time_since_epoch().count())};

websocket::websocket(boost::asio::io_service &service)
    : io{service}, socket{service}, close_status{0}
{
}

websocket::~websocket()
{
    close();
}

void websocket::async_connect(const std::string &host, const std::string &service,
                              const std::string &resource, bool secure, connect_callback c)
{
    this->host = host;
    this->resource = resource;
    boost::asio::ip::tcp::resolver resolver{io};
    boost::asio::ip::tcp::resolver::query query{host, service};
    auto endpoints = resolver.resolve(query);
    socket.connect(*endpoints);
    on_connect({}, endpoints, c);
    //    resolver.async_resolve(
    //        query, boost::bind(&websocket::on_resolve, this, boost::asio::placeholders::error,
    //                           boost::asio::placeholders::iterator, c));
}

void websocket::on_resolve(const ::boost::system::error_code &e, endpoint_iterator it,
                           connect_callback c)
{
    if (e) {
        c(e);
    } else {
        // Try to connect to the first endpoint
        auto endpoint = *it;
        socket.async_connect(endpoint, boost::bind(&websocket::on_connect, this,
                                                   boost::asio::placeholders::error, ++it, c));
    }
}

void websocket::on_connect(const boost::system::error_code &e, endpoint_iterator it,
                           connect_callback c)
{
    if (!e) {
        // Successfully connected!

        // Want 16 byte random key for WebSocket-Key
        std::vector<uint8_t> nonce(16);
        std::generate(nonce.begin(), nonce.end(), std::ref(generator));
        std::string encoded_key = base64::encode(nonce.data(), nonce.size());

        // Figure out the expected response (Sec-WebSocket-Accept)
        std::string concat = encoded_key + websocket_guid;
        uint8_t shasum[20];
        SHA1(reinterpret_cast<const unsigned char *>(concat.c_str()), concat.size(), shasum);
        expected_accept = base64::encode(shasum, sizeof(shasum));

        std::ostream request_stream{&write_buffer};
        request_stream << "GET " << resource << " HTTP/1.1\r\n";
        request_stream << "Host: " << host << "\r\n";
        request_stream << "Upgrade: websocket\r\n";
        request_stream << "Connection: Upgrade\r\n";
        request_stream << "Sec-WebSocket-Key: " << encoded_key << "\r\n";
        request_stream << "Origin: " << host << "\r\n";
        request_stream << "Sec-WebSocket-Version: 13\r\n";
        request_stream << "\r\n";

        auto begin = boost::asio::buffers_begin(write_buffer.data());
        auto end = begin + write_buffer.size();
        std::cout << std::string{begin, end} << "\n";

        // Send the HTTP connection upgrade request
        boost::asio::async_write(socket, write_buffer,
                                 boost::bind(&websocket::on_websocket_upgrade_sent, this,
                                             boost::asio::placeholders::error, c));
    } else if (it != endpoint_iterator()) {
        // The connection failed. Try the next endpoint in the list.
        socket.close();
        auto next_endpoint = *it;
        socket.async_connect(next_endpoint, boost::bind(&websocket::on_connect, this,
                                                        boost::asio::placeholders::error, ++it, c));
    } else {
        c(e);
    }
}

void websocket::on_websocket_upgrade_sent(const boost::system::error_code &e, connect_callback c)
{
    if (e) {
        c(e);
        return;
    }
    auto mutable_buffer = write_buffer.prepare(4096);
    socket.async_read_some(boost::asio::buffer(mutable_buffer),
                           boost::bind(&websocket::on_websocket_upgrade_receive, this,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred, c));
}

void websocket::on_websocket_upgrade_receive(const boost::system::error_code &e, size_t transferred,
                                             connect_callback c)
{
    if (e) {
        c(e);
        return;
    }

    // Commit the read bytes to the streambuf and parse the results
    write_buffer.commit(transferred);
    response.parse(write_buffer);

    if (response.is_complete()) {
        check_websocket_upgrade(c);
    } else {
        // Http response is incomplete, read more
        auto mutable_buffer = write_buffer.prepare(4096);
        socket.async_read_some(boost::asio::buffer(mutable_buffer),
                               boost::bind(&websocket::on_websocket_upgrade_receive, this,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred, c));
    }
}

void websocket::check_websocket_upgrade(connect_callback c)
{
    if (response.status_code() != 101) {
        // No version renegotiation. We only support WebSocket v13
        c(websocket::boost_make_error_code(websocket::error::upgrade_failed));
        return;
    }

    auto &headers_map = response.headers();
    auto it = headers_map.find("sec-websocket-accept");
    if (it == headers_map.end()) {
        c(websocket::boost_make_error_code(websocket::error::no_upgrade_key));
        return;
    }
    if (it->second != expected_accept) {
        c(websocket::boost_make_error_code(websocket::error::bad_upgrade_key));
    } else {
        c({});
    }
}

void websocket::close(websocket_status_code code)
{
    // TODO: check if we are sending in response, or initiating close
    if (close_status)
        return;

    close_status = static_cast<uint16_t>(code);

    // Make sure the code is in network byte order
    uint8_t buf[2];
    buf[0] = static_cast<unsigned char>((close_status & 0xFF00) >> 8);
    buf[1] = static_cast<unsigned char>(close_status & 0x00FF);

    build_frame_and_send_async(buf, sizeof(buf), websocket_opcode::close,
                               [=](const boost::system::error_code &e, size_t) {
                                    std::cout << "Close frame send with value: " << close_status << "\n";
                               });
}

void websocket::async_send(const std::string &str, websocket::message_sent_callback c)
{
    build_frame_and_send_async(str.c_str(), str.size(), websocket_opcode::text, c);
}

void websocket::async_send(const void *buffer, size_t size, websocket::message_sent_callback c)
{
    build_frame_and_send_async(buffer, size, websocket_opcode::text, c);
}

void websocket::build_frame_and_send_async(const void *data, size_t len, websocket_opcode op,
                                           message_sent_callback c)
{
    // out_size is the size of the buffer data + WebSocket frame
    size_t out_size = get_frame_size(len);
    auto frame = boost::asio::buffer_cast<uint8_t *>(write_buffer.prepare(out_size));

    frame[0] = 0x80;  // FIN bit set (this is a complete frame)
    frame[0] |= static_cast<uint8_t>(op);

    const auto *read_from = reinterpret_cast<const uint8_t *>(data);
    auto *write_to = &frame[out_size - len - 4];  // -4 to get to start of where mask goes

    write_frame_size(frame, len);
    write_masked_data(read_from, write_to, len);

    write_buffer.commit(out_size);
    boost::asio::async_write(socket, write_buffer, c);
}

// Return the number bytes needed to encoded WebSocket frame + bytes
size_t websocket::get_frame_size(size_t bytes)
{
    if (bytes < 126)
        return bytes + 6;
    if (bytes < std::numeric_limits<uint16_t>::max())
        return bytes + 8;
    return bytes + 14;
}

void websocket::write_frame_size(uint8_t *frame, size_t size)
{
    const uint8_t mask_bit = 0x80;
    if (size < 126) {
        frame[1] = static_cast<uint8_t>(mask_bit | (uint8_t) size);
    } else if (size <= std::numeric_limits<uint16_t>::max()) {  // 16 bit unsigned max
        frame[1] = mask_bit | 126;
        frame[2] = static_cast<uint8_t>((size & 0xFF00) >> 8);
        frame[3] = static_cast<uint8_t>(size & 0x00FF);
    } else {
        frame[1] = mask_bit | 127;
        frame[2] = static_cast<uint8_t>((size & 0x7F00000000000000) >> 56);  // MSB always 0
        frame[3] = static_cast<uint8_t>((size & 0x00FF000000000000) >> 48);
        frame[4] = static_cast<uint8_t>((size & 0x0000FF0000000000) >> 40);
        frame[5] = static_cast<uint8_t>((size & 0x000000FF00000000) >> 32);
        frame[6] = static_cast<uint8_t>((size & 0x00000000FF000000) >> 24);
        frame[7] = static_cast<uint8_t>((size & 0x0000000000FF0000) >> 16);
        frame[8] = static_cast<uint8_t>((size & 0x000000000000FF00) >> 8);
        frame[9] = static_cast<uint8_t>(size & 0x00000000000000FF);
    }
}

// Assumes there is room in 'out' for size + 4 bytes
void websocket::write_masked_data(const uint8_t *in, uint8_t *out, size_t size)
{
    auto mask0 = generator();
    auto mask1 = generator();
    auto mask2 = generator();
    auto mask3 = generator();

    *out++ = mask0;
    *out++ = mask1;
    *out++ = mask2;
    *out++ = mask3;

    while (size > 3) {
        *out++ = *in++ ^ mask0;
        *out++ = *in++ ^ mask1;
        *out++ = *in++ ^ mask2;
        *out++ = *in++ ^ mask3;
        size -= 4;
    }
    if (size > 0)
        *out++ = *in++ ^ mask0;
    if (size > 1)
        *out++ = *in++ ^ mask1;
    if (size > 2)
        *out++ = *in++ ^ mask2;
}

void websocket::async_next_message(message_received_callback c)
{
    // If the connection has been closed, notify the caller immediately
    if (close_status) {
        c(websocket::boost_make_error_code(websocket::error::websocket_connection_closed), nullptr,
          0);
    }
    // Otherwise continue on, trying to parse any available buffered data, and enqueueing reads as
    // neccessary to read an entire WebSocket frame

    // If a close frame occurs in the middle of a message receive, we send the message (or all that
    // is available). Next call to async_next_message will set error code to notify caller, the
    // close code value can be retrived by websocket::close_code();

    if (parser.is_new_state()) {
        sender_callback = c;
    } else {
        throw std::runtime_error("Called async_next_message before previous completed");
    }

    check_parser_state();
}

void websocket::check_parser_state()
{
    // If we're closed, don't bother trying to read more
    //    if (close_status)
    //        return;

    if (!parser.is_frame_complete() && message_response.size() > 0)
        parser.parse(message_response);

    if (!parser.is_frame_complete()) {
        enqueue_read_some(4096);
    } else if (parser.get_size() > message_response.size()) {
        // Frame incomplete... read the remaining amount
        enqueue_read(parser.get_size() - message_response.size());
    } else {
        // We have enough data at this point
        handle_frame();
    }
}

void websocket::enqueue_read(size_t amount)
{
    auto mutable_buf = message_response.prepare(amount);
    socket.async_receive(mutable_buf, boost::bind(&websocket::on_websocket_data_receive, this,
                                                  boost::asio::placeholders::error,
                                                  boost::asio::placeholders::bytes_transferred));
}

void websocket::enqueue_read_some(size_t amount)
{
    auto mutable_buf = message_response.prepare(amount);
    socket.async_read_some(mutable_buf, boost::bind(&websocket::on_websocket_data_receive, this,
                                                    boost::asio::placeholders::error,
                                                    boost::asio::placeholders::bytes_transferred));
}

void websocket::on_websocket_data_receive(const boost::system::error_code &e, size_t transferred)
{
    if (e) {
        std::cerr << "Websocket message data receive error: " << e.message() << "\n";
    }
    if (e == boost::asio::error::eof) {
        // Connection has been closed, if we didn't get a close value, set it to 1 (not a valid
        // WebSocket close code, but it will indicate closed connection on next read)
        if (!close_status)
            close_status = 1;
    }
    // Commit the read data to the stream
    message_response.commit(transferred);

    check_parser_state();
}

void websocket::handle_frame()
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(message_response.data());
    auto size = parser.get_size();

    bool is_control_frame = false;

    switch (parser.frame_type()) {
        case websocket_opcode::continuation:
            // TODO: keep track of frame state, e.g. if we got text no FIN, expect continuation
            // until FIN
            break;
        case websocket_opcode::text:
            break;
        case websocket_opcode::binary:
            break;
        case websocket_opcode::close:
            if (close_status)
                break;
            // Response with the same close code if available
            if (parser.get_size() >= 2) {
                // Respond with the same code sent
                uint16_t code = (data[0] << 8) | data[1];
                close(static_cast<websocket_status_code>(code));
            } else {
                // Invalid WebSocket close frame... respond with normal close response
                close(websocket_status_code::normal);
            }
            is_control_frame = true;
            break;
        case websocket_opcode::ping:
            pong(data, size);
            is_control_frame = true;
            break;
        case websocket_opcode::pong:
            is_control_frame = true;
            break;
        default:
            is_control_frame = true;
            std::cerr << "Invalid WebSocket opcode: " << (int) parser.frame_type() << "\n";
    }
    if (parser.is_fin() && !is_control_frame) {
        parser.reset();
        sender_callback({}, data, size);
        message_response.consume(size);
    }

    if (parser.is_fin() || is_control_frame) {
        // Consume the frame
        message_response.consume(size);
    }
}

void websocket::pong(const uint8_t *msg, size_t len)
{
    // At most, send 125 bytes in control frame
    build_frame_and_send_async(msg, std::min(len, (size_t) 125), websocket_opcode::pong,
                               [](const boost::system::error_code &e, size_t) {
                                    std::cout << "Sent pong\n";
                               });
}

websocket::frame_parser::frame_parser()
{
    reset();
}

void websocket::frame_parser::parse(boost::asio::streambuf &source)
{
    if (state == parse_state::begin) {
        parse_frame(source);
    }
    if (state == parse_state::length) {
        parse_payload_length(source);
    }
}

void websocket::frame_parser::reset()
{
    fin_frame = false;
    masked = false;
    state = parse_state::begin;
    payload_length = 0;
    size = 0;
    f_type = websocket_opcode::none;
}

void websocket::frame_parser::parse_frame(boost::asio::streambuf &buf)
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buf.data());
    if (buf.size() > 0) {
        fin_frame = (data[0] & 0x80) != 0;
        f_type = static_cast<websocket_opcode>(data[0] & 0x0F);
        buf.consume(1);
        state = parse_state::length;
    }
}

void websocket::frame_parser::parse_payload_length(boost::asio::streambuf &buf)
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buf.data());
    if (size == 0 && buf.size() > 0) {
        payload_length = static_cast<uint8_t>(data[0] & 0x7F);
        masked = (data[0] & 0x80) != 0;
        buf.consume(1);
        data++;
        if (payload_length < 126) {
            size = payload_length;
            payload_length = 0;
        } else if (payload_length == 126) {
            payload_length = 2;
        } else {
            payload_length = 6;
        }
    }
    while (payload_length > 0 && buf.size() > 0) {
        size = (size << 8) | data[0];
        data++;
        buf.consume(1);
        payload_length--;
    }
    if (payload_length == 0) {
        state = parse_state::frame_done;
    }
}

bool websocket::frame_parser::is_frame_complete()
{
    return state == parse_state::frame_done;
}

websocket_opcode websocket::frame_parser::frame_type()
{
    return f_type;
}

uint64_t websocket::frame_parser::get_size()
{
    return size;
}

bool websocket::frame_parser::is_fin()
{
    return fin_frame;
}

bool websocket::frame_parser::is_masked()
{
    return masked;
}

bool websocket::frame_parser::is_new_state()
{
    return size == 0 && f_type == websocket_opcode::none;
}
