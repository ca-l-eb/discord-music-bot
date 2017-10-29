#include <openssl/sha.h>
#include <boost/bind.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

#include "base64.h"
#include "resource_parser.h"
#include "websocket.h"

using random_byte_engine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;
thread_local random_byte_engine generator{
    static_cast<unsigned char>(std::chrono::system_clock::now().time_since_epoch().count())};

cmd::websocket::websocket(boost::asio::io_service &service)
    : io{service}
    , ctx{boost::asio::ssl::context::tls_client}
    , socket{service, ctx}
    , write_strand{service}
    , close_status{0}
    , resolver{service}
{
}

cmd::websocket::~websocket()
{
    close();
}

void cmd::websocket::async_connect(const std::string &host, const std::string &service,
                                   const std::string &resource, message_sent_callback c)
{
    this->host = host;
    this->resource = resource;
    this->connect_callback = c;
    secure_connection = (service == "443" || service == "https" || service == "wss");

    boost::asio::ip::tcp::resolver::query query{host, service};
    resolver.async_resolve(query, [&](const boost::system::error_code &e, endpoint_iterator it) {
        on_resolve(e, it);
    });
}

void cmd::websocket::async_connect(const std::string &url, cmd::websocket::message_sent_callback c)
{
    std::string proto;
    int port;
    std::tie(proto, host, port, resource) = cmd::resource_parser::parse(url);

    // Use unsecure connection by default if resource parser couldn't determine a port
    if (port == -1)
        proto = "http";

    if (port == 443)
        proto = "https";

    async_connect(host, proto, resource, c);
}

void cmd::websocket::on_resolve(const boost::system::error_code &e, endpoint_iterator it)
{
    if (!e) {
        auto endpoint = *it;
        auto next = ++it;
        socket.lowest_layer().async_connect(
            endpoint, [=](const boost::system::error_code &e) { on_connect(e, next); });
    } else {
        throw std::runtime_error("Could not resolve host: " + host);
    }
}

void cmd::websocket::on_connect(const boost::system::error_code &e, endpoint_iterator it)
{
    if (!e) {
        // Successfully connected!
        if (secure_connection) {
            do_handshake();
        } else {
            send_upgrade();
        }
    } else if (it != endpoint_iterator()) {
        // Try next
        auto next = ++it;
        socket.lowest_layer().async_connect(
            *it, [=](const boost::system::error_code &e) { on_connect(e, next); });
    } else {
        throw std::runtime_error("Could not connect to host: " + host);
    }
}

void cmd::websocket::do_handshake()
{
    // Load default certificate store
    ctx.set_default_verify_paths();

    // Make sure host is verified during handshake
    socket.set_verify_mode(boost::asio::ssl::verify_peer);
    socket.set_verify_callback(boost::asio::ssl::rfc2818_verification(host));
    socket.async_handshake(
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::client,
        [&](const boost::system::error_code &e) {
            if (e) {
                throw std::runtime_error("SSL handshake with " + host + " failed");
            }
            send_upgrade();  // Handshake successful
        });
}

void cmd::websocket::on_websocket_upgrade_sent(const boost::system::error_code &e)
{
    if (e) {
        std::cerr << "Error sending upgrade: " << e.message() << "\n";
        io.post([&]() { connect_callback(e, 0); });
        return;
    }
    auto mutable_buffer = buffer.prepare(4096);
    auto callback =
        boost::bind(&websocket::on_websocket_upgrade_receive, this,
                    boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
    if (secure_connection) {
        socket.async_read_some(mutable_buffer, callback);
    } else {
        socket.next_layer().async_read_some(mutable_buffer, callback);
    }
}

void cmd::websocket::on_websocket_upgrade_receive(const boost::system::error_code &e,
                                                  size_t transferred)
{
    if (e) {
        std::cerr << "Error reading websocket upgrade reponse: " << e.message() << "\n";
        io.post([=]() { connect_callback(e, 0); });
        return;
    }

    // Commit the read bytes to the streambuf and parse the results
    buffer.commit(transferred);
    response.parse(buffer);

    if (response.is_complete()) {
        check_websocket_upgrade();
    } else {
        // Http response is incomplete, read more
        enqueue_read(4096, boost::bind(&websocket::on_websocket_upgrade_receive, this,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
}

void cmd::websocket::send_upgrade()
{
    // Want 16 byte random key for WebSocket-Key
    std::vector<uint8_t> nonce(16);
    std::generate(nonce.begin(), nonce.end(), std::ref(generator));
    std::string encoded_key = base64::encode(nonce.data(), nonce.size());

    // Figure out the expected response (Sec-WebSocket-Accept)
    std::string concat = encoded_key + websocket_guid;
    uint8_t shasum[20];
    SHA1(reinterpret_cast<const unsigned char *>(concat.c_str()), concat.size(), shasum);
    expected_accept = base64::encode(shasum, sizeof(shasum));

    std::string request;
    request += "GET " + resource + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + encoded_key + "\r\n";
    request += "Origin: " + host + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";

    // Copy the request to the data vector
    std::vector<uint8_t> data{request.begin(), request.end()};

    // Submit to the write queue the HTTP connection upgrade request, and the callback
    io.post(write_strand.wrap([=]() {
        queue_message(data, [=](const boost::system::error_code &e, size_t transferred) {
            on_websocket_upgrade_sent(e);
        });
    }));
}

void cmd::websocket::check_websocket_upgrade()
{
    if (response.status_code() != 101) {
        // No version renegotiation. We only support WebSocket v13
        io.post([=]() {
            connect_callback(websocket::boost_make_error_code(websocket::error::upgrade_failed), 0);
        });
        return;
    }

    auto &headers_map = response.headers();
    auto it = headers_map.find("sec-websocket-accept");
    if (it == headers_map.end()) {
        io.post([=]() {
            connect_callback(websocket::boost_make_error_code(websocket::error::no_upgrade_key), 0);
        });
        return;
    }
    if (it->second != expected_accept) {
        io.post([=]() {
            connect_callback(websocket::boost_make_error_code(websocket::error::bad_upgrade_key),
                             0);
        });
    } else {
        io.post([=]() { connect_callback({}, 0); });
    }
}

void cmd::websocket::close(websocket::status_code code)
{
    // TODO: check if we are sending in response, or initiating close
    if (close_status)
        return;

    close_status = static_cast<uint16_t>(code);

    // Make sure the code is in network byte order
    uint8_t buf[2];
    buf[0] = static_cast<unsigned char>((close_status & 0xFF00) >> 8);
    buf[1] = static_cast<unsigned char>(close_status & 0x00FF);

    build_frame_and_send_async(
        buf, sizeof(buf), websocket::opcode::close, [=](const boost::system::error_code &, size_t) {
            std::cout << "Close frame send with value: " << close_status << "\n";
        });
}

void cmd::websocket::async_send(const std::string &str, websocket::message_sent_callback c)
{
    build_frame_and_send_async(str.c_str(), str.size(), websocket::opcode::text, c);
}

void cmd::websocket::async_send(const void *buffer, size_t size, websocket::message_sent_callback c)
{
    build_frame_and_send_async(buffer, size, websocket::opcode::text, c);
}

void cmd::websocket::build_frame_and_send_async(const void *data, size_t len, websocket::opcode op,
                                                message_sent_callback c)
{
    // out_size is the size of the buffer data + WebSocket frame
    size_t out_size = get_frame_size(len);
    auto frame = std::vector<uint8_t>(out_size);

    frame[0] = 0x80;  // FIN bit set (this is a complete frame)
    frame[0] |= static_cast<uint8_t>(op);

    const auto *read_from = reinterpret_cast<const uint8_t *>(data);
    auto *write_to = &frame[out_size - len - 4];  // -4 to get to start of where mask goes

    write_frame_size(frame.data(), len);
    write_masked_data(read_from, write_to, len);

    io.post(write_strand.wrap([=]() { queue_message(frame, c); }));
}

// Return the number bytes needed to encoded WebSocket frame + bytes
size_t cmd::websocket::get_frame_size(size_t bytes)
{
    if (bytes < 126)
        return bytes + 6;
    if (bytes < std::numeric_limits<uint16_t>::max())
        return bytes + 8;
    return bytes + 14;
}

void cmd::websocket::write_frame_size(uint8_t *frame, size_t size)
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
void cmd::websocket::write_masked_data(const uint8_t *in, uint8_t *out, size_t size)
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

void cmd::websocket::async_next_message(message_received_callback c)
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

void cmd::websocket::check_parser_state()
{
    // If we're closed, don't bother trying to read more
    //    if (close_status)
    //        return;

    if (!parser.is_frame_complete() && buffer.size() > 0)
        parser.parse(buffer);

    auto amount_to_read = std::max((size_t) 4096, (size_t)(parser.get_size() - buffer.size()));
    if (!parser.is_frame_complete() || parser.get_size() > buffer.size()) {
        enqueue_read(amount_to_read, boost::bind(&websocket::on_websocket_data_receive, this,
                                                 boost::asio::placeholders::error,
                                                 boost::asio::placeholders::bytes_transferred));
    } else {
        // We have enough data at this point
        handle_frame();
    }
}

void cmd::websocket::enqueue_read(size_t amount, message_sent_callback c)
{
    auto mutable_buf = buffer.prepare(amount);
    if (secure_connection) {
        socket.async_read_some(mutable_buf, c);
    } else {
        socket.next_layer().async_read_some(mutable_buf, c);
    }
}

void cmd::websocket::on_websocket_data_receive(const boost::system::error_code &e,
                                               size_t transferred)
{
    if (e) {
        // TODO: handle the error
        throw std::runtime_error("Websocket message data receive error");
    }
    if (e == boost::asio::error::eof) {
        // Connection has been closed, if we didn't get a close value, set it to 1 (not a valid
        // WebSocket close code, but it will indicate closed connection on next read)
        if (!close_status)
            close_status = 1;
    }
    // Commit the read data to the stream
    buffer.commit(transferred);

    check_parser_state();
}

void cmd::websocket::handle_frame()
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buffer.data());
    auto size = parser.get_size();

    bool is_control_frame = false;

    switch (parser.frame_type()) {
        case websocket::opcode::continuation:
            // TODO: keep track of frame state, e.g. if we got text no FIN, expect continuation
            // until FIN (with control frames allowed in between)
            break;
        case websocket::opcode::text:
            break;
        case websocket::opcode::binary:
            break;
        case websocket::opcode::close:
            if (close_status)
                break;
            // Response with the same close code if available
            if (parser.get_size() >= 2) {
                // Respond with the same code sent
                uint16_t code = (data[0] << 8) | data[1];
                close(static_cast<websocket::status_code>(code));
            } else {
                // Invalid WebSocket close frame... respond with normal close response
                close(websocket::status_code::normal);
            }
            is_control_frame = true;
            break;
        case websocket::opcode::ping:
            pong(data, size);
            is_control_frame = true;
            break;
        case websocket::opcode::pong:
            is_control_frame = true;
            break;
        default:
            is_control_frame = true;
            std::cerr << "Invalid WebSocket opcode: " << (int) parser.frame_type() << "\n";
    }
    if (parser.is_fin() && !is_control_frame) {
        // Make a copy of the data because the buffer is being release before the callback (which
        // might do some IO on the websocket which can invalidate the data)
        std::vector<uint8_t> vec_copy(data, data + size);
        buffer.consume(size);
        parser.reset();
        io.post([=]() { sender_callback({}, vec_copy.data(), vec_copy.size()); });
    } else if (is_control_frame) {
        // Consume the frame
        buffer.consume(size);
    }
}

void cmd::websocket::pong(const uint8_t *msg, size_t len)
{
    // At most, send 125 bytes in control frame
    build_frame_and_send_async(
        msg, std::min(len, (size_t) 125), websocket::opcode::pong,
        [](const boost::system::error_code &, size_t) { std::cout << "Sent pong\n"; });
}

uint16_t cmd::websocket::close_code()
{
    return close_status;
}

// This idea is from CppCon 2016 Talk by Michael Caisse "Asynchronous IO with Boost.Asio"
//
// This is wrapped in write_strand... will only be called by a single thread at a time since all
// work dealing with sending it wrapped in the same strand
void cmd::websocket::queue_message(std::vector<uint8_t> v, message_sent_callback c)
{
    bool write_in_progess = !write_queue.empty();
    write_queue.push_back(std::move(v));
    callback_queue.push_back(std::move(c));
    if (!write_in_progess) {
        start_packet_send();
    }
}

void cmd::websocket::start_packet_send()
{
    auto send_complete_callback = write_strand.wrap(
        boost::bind(&websocket::packet_send_done, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
    if (secure_connection) {
        boost::asio::async_write(socket, boost::asio::buffer(write_queue.front()),
                                 send_complete_callback);
    } else {
        boost::asio::async_write(socket.next_layer(), boost::asio::buffer(write_queue.front()),
                                 send_complete_callback);
    }
}

void cmd::websocket::packet_send_done(const boost::system::error_code &e, size_t transferred)
{
    auto &callback = callback_queue.front();
    io.post([=]() { callback(e, transferred); });
    write_queue.pop_front();
    callback_queue.pop_front();

    // If there wasn't an error, try to send the next packet if it exists
    if (!e) {
        if (!write_queue.empty()) {
            start_packet_send();
        }
    }
}

cmd::websocket::frame_parser::frame_parser()
{
    reset();
}

void cmd::websocket::frame_parser::parse(boost::asio::streambuf &source)
{
    if (state == parse_state::begin) {
        parse_frame(source);
    }
    if (state == parse_state::length) {
        parse_payload_length(source);
    }
}

void cmd::websocket::frame_parser::reset()
{
    fin_frame = false;
    masked = false;
    state = parse_state::begin;
    payload_length = 0;
    size = 0;
    f_type = websocket::opcode::none;
}

void cmd::websocket::frame_parser::parse_frame(boost::asio::streambuf &buf)
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buf.data());
    if (buf.size() > 0) {
        fin_frame = (data[0] & 0x80) != 0;
        f_type = static_cast<websocket::opcode>(data[0] & 0x0F);
        buf.consume(1);
        state = parse_state::length;
    }
}

void cmd::websocket::frame_parser::parse_payload_length(boost::asio::streambuf &buf)
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buf.data());
    if (buf.size() == 0)
        return;
    if (size == 0) {
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

bool cmd::websocket::frame_parser::is_frame_complete()
{
    return state == parse_state::frame_done;
}

cmd::websocket::opcode cmd::websocket::frame_parser::frame_type()
{
    return f_type;
}

uint64_t cmd::websocket::frame_parser::get_size()
{
    return size;
}

bool cmd::websocket::frame_parser::is_fin()
{
    return fin_frame;
}

bool cmd::websocket::frame_parser::is_masked()
{
    return masked;
}

bool cmd::websocket::frame_parser::is_new_state()
{
    return size == 0 && f_type == websocket::opcode::none;
}
