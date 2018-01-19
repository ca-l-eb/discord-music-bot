#include <openssl/sha.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

#include <net/base64.h>
#include <net/resource_parser.h>
#include <net/send_queue.h>
#include <net/websocket.h>

using random_byte_engine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;

thread_local random_byte_engine generator{
    static_cast<unsigned char>(std::chrono::system_clock::now().time_since_epoch().count())};

static size_t get_frame_size(size_t bytes);
static void write_frame_size(uint8_t *frame, size_t size);
static void write_masked_data(const uint8_t *in, uint8_t *out, size_t size, uint8_t mask[4]);
static std::vector<uint8_t> build_frame(const uint8_t *data, size_t len, websocket::opcode op);

websocket::websocket(boost::asio::io_context &ioc)
    : io{ioc}, ctx{boost::asio::ssl::context::tls_client}, stream{ioc, ctx}, close_status{0}
{
}

websocket::~websocket()
{
    close();
}

void websocket::async_connect(const std::string &host, const std::string &service,
                              const std::string &resource, boost::asio::ip::tcp::resolver &resolver,
                              error_cb c)
{
    this->host = host;
    this->resource = resource;
    this->connect_callback = c;
    secure_connection = (service == "443" || service == "https" || service == "wss");
    queue = std::make_unique<send_queue>(stream, io, secure_connection);

    auto callback = [this](auto &ec, auto it) { on_resolve(ec, it); };
    boost::asio::ip::tcp::resolver::query query{host, service};
    resolver.async_resolve(query, callback);
}

void websocket::async_connect(const std::string &url, boost::asio::ip::tcp::resolver &resolver,
                              error_cb c)
{
    auto parsed = resource_parser::parse(url);
    int port = parsed.port;
    host = parsed.host;
    resource = parsed.resource;

    if (host.empty())
        throw std::runtime_error("Could not find host url: " + url);

    // Use unsecure connection by default if resource parser couldn't determine a port
    if (port == -1 || parsed.protocol == "ws")
        parsed.protocol = "http";

    if (port == 443 || parsed.protocol == "wss")
        parsed.protocol = "https";

    async_connect(host, parsed.protocol, resource, resolver, c);
}

void websocket::on_resolve(const boost::system::error_code &ec,
                           boost::asio::ip::tcp::resolver::iterator it)
{
    if (ec) {
        throw std::runtime_error("Could not resolve host: " + host);
    }

    auto callback = [this](auto &ec, auto it) { on_connect(ec, it); };
    boost::asio::async_connect(stream.lowest_layer(), it, callback);
}

void websocket::on_connect(const boost::system::error_code &ec,
                           boost::asio::ip::tcp::resolver::iterator)
{
    if (ec) {
        throw std::runtime_error("Could not connect to " + host + ": " + ec.message());
    }

    // Successfully connected!
    if (secure_connection) {
        do_handshake();
    } else {
        send_upgrade();
    }
}

void websocket::do_handshake()
{
    // Load default certificate store
    ctx.set_default_verify_paths();
    auto callback = [&](auto &ec) {
        if (ec) {
            throw std::runtime_error("SSL handshake with " + host + " failed: " + ec.message());
        }
        send_upgrade();  // Handshake successful
    };

    // Make sure host is verified during handshake
    stream.set_verify_mode(boost::asio::ssl::verify_peer);
    stream.set_verify_callback(boost::asio::ssl::rfc2818_verification(host));
    stream.async_handshake(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>::client,
                           callback);
}

void websocket::send_upgrade()
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

    // clang-format off
    std::string request = 
        "GET " + resource + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Sec-WebSocket-Key: " + encoded_key + "\r\n"
        "Origin: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    // clang-format on

    // Copy the request to the data vector
    std::vector<uint8_t> data{request.begin(), request.end()};

    // Submit to the write queue the HTTP connection upgrade request, and the callback
    auto callback = [=](auto &ec, size_t) { on_upgrade_sent(ec); };
    queue->enqueue_message(data, callback);
}

void websocket::on_upgrade_sent(const boost::system::error_code &ec)
{
    if (ec) {
        std::cerr << "Error sending upgrade: " << ec.message() << "\n";
        io.post([=]() { connect_callback(ec); });
        return;
    }
    auto mutable_buffer = buffer.prepare(4096);
    auto callback = [this](auto &ec, auto transferred) { on_upgrade_receive(ec, transferred); };
    if (secure_connection) {
        stream.async_read_some(mutable_buffer, callback);
    } else {
        stream.next_layer().async_read_some(mutable_buffer, callback);
    }
}

void websocket::on_upgrade_receive(const boost::system::error_code &ec, size_t transferred)
{
    if (ec) {
        std::cerr << "Error reading websocket upgrade reponse: " << ec.message() << "\n";
        io.post([=]() { connect_callback(ec); });
        return;
    }

    // Commit the read bytes to the streambuf and parse the results
    buffer.commit(transferred);
    response.parse(buffer);

    if (response.is_complete()) {
        check_upgrade();
    } else {
        auto callback = [this](auto &ec, auto transferred) { on_upgrade_receive(ec, transferred); };
        // Http response is incomplete, read more
        enqueue_read(4096, callback);
    }
}

void websocket::check_upgrade()
{
    if (response.status_code() != 101) {
        // No version renegotiation. We only support WebSocket v13
        auto callback = [=]() { connect_callback(error::upgrade_failed); };
        io.post(callback);
        return;
    }

    auto &headers_map = response.headers();
    auto it = headers_map.find("sec-websocket-accept");
    if (it == headers_map.end()) {
        io.post([=]() { connect_callback(error::no_upgrade_key); });
    } else if (it->second != expected_accept) {
        io.post([=]() { connect_callback(error::bad_upgrade_key); });
    } else {
        // Successfully connected and upgraded connection to websocket
        io.post([=]() { connect_callback({}); });
    }
}

void websocket::close(websocket::status_code code)
{
    // TODO: check if we are sending in response, or initiating close
    if (close_status)
        return;

    close_status = static_cast<uint16_t>(code);

    // Make sure the code is in network byte order
    uint8_t buf[2];
    buf[0] = static_cast<unsigned char>((close_status & 0xFF00) >> 8);
    buf[1] = static_cast<unsigned char>(close_status & 0x00FF);

    auto callback = [=](auto &, size_t) {
        std::cout << "Close frame sent with value: " << close_status << "\n";
    };
    build_frame_and_send_async(buf, sizeof(buf), opcode::close, callback);
}

void websocket::async_send(const std::string &str, transfer_cb c)
{
    build_frame_and_send_async(str.c_str(), str.size(), opcode::text, c);
}

void websocket::async_send(const void *buffer, size_t size, transfer_cb c)
{
    build_frame_and_send_async(buffer, size, opcode::text, c);
}

void websocket::build_frame_and_send_async(const void *data, size_t len, websocket::opcode op,
                                           transfer_cb c)
{
    auto frame = build_frame(reinterpret_cast<const uint8_t *>(data), len, op);
    queue->enqueue_message(frame, c);
}

void websocket::async_next_message(data_cb c)
{
    // If the connection has been closed, notify the caller immediately
    if (close_status) {
        c(error::websocket_connection_closed, nullptr, 0);
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

    if (!parser.is_frame_complete() && buffer.size() > 0)
        parser.parse(buffer);

    auto amount_to_read = std::max((size_t) 4096, (size_t)(parser.get_size() - buffer.size()));
    if (!parser.is_frame_complete() || parser.get_size() > buffer.size()) {
        auto callback = [=](auto &ec, auto transferred) { on_data_receive(ec, transferred); };
        enqueue_read(amount_to_read, callback);
    } else {
        // We have enough data at this point
        handle_frame();
    }
}

void websocket::enqueue_read(size_t amount, transfer_cb c)
{
    auto mutable_buf = buffer.prepare(amount);
    if (secure_connection) {
        stream.async_read_some(mutable_buf, c);
    } else {
        stream.next_layer().async_read_some(mutable_buf, c);
    }
}

void websocket::on_data_receive(const boost::system::error_code &ec, size_t transferred)
{
    if (ec == boost::asio::error::eof) {
        // Connection has been closed, if we didn't get a close value, set it to 1 (not a valid
        // WebSocket close code, but it will indicate closed connection on next read)
        if (!close_status)
            close_status = 1;
    } else if (ec) {
        // Some other error
        throw std::runtime_error("Error receiving data from websocket: " + ec.message());
    }

    // Commit the read data to the stream
    buffer.commit(transferred);

    check_parser_state();
}

void websocket::handle_frame()
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buffer.data());
    auto size = parser.get_size();

    bool is_control_frame = false;

    switch (parser.frame_type()) {
        case opcode::continuation:
            // TODO: keep track of frame state, e.g. if we got text no FIN, expect continuation
            // until FIN (with control frames allowed in between)
            break;
        case opcode::text:
            break;
        case opcode::binary:
            break;
        case opcode::close:
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
        case opcode::ping:
            pong(data, size);
            is_control_frame = true;
            break;
        case opcode::pong:
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

void websocket::pong(const uint8_t *msg, size_t len)
{
    // At most, send 125 bytes in control frame
    auto callback = [](auto &, size_t) { std::cout << "Sent pong\n"; };
    build_frame_and_send_async(msg, std::min(len, (size_t) 125), opcode::pong, callback);
}

uint16_t websocket::close_code()
{
    return close_status;
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
    f_type = opcode::none;
}

void websocket::frame_parser::parse_frame(boost::asio::streambuf &buf)
{
    const auto *data = boost::asio::buffer_cast<const uint8_t *>(buf.data());
    if (buf.size() > 0) {
        fin_frame = (data[0] & 0x80) != 0;
        f_type = static_cast<opcode>(data[0] & 0x0F);
        buf.consume(1);
        state = parse_state::length;
    }
}

void websocket::frame_parser::parse_payload_length(boost::asio::streambuf &buf)
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

bool websocket::frame_parser::is_frame_complete()
{
    return state == parse_state::frame_done;
}

websocket::opcode websocket::frame_parser::frame_type()
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
    return size == 0 && f_type == opcode::none;
}

// Return the number bytes needed to encoded WebSocket frame + bytes
static size_t get_frame_size(size_t bytes)
{
    if (bytes < 126)
        return bytes + 6;
    if (bytes < std::numeric_limits<uint16_t>::max())
        return bytes + 8;
    return bytes + 14;
}

static void write_frame_size(uint8_t *frame, size_t size)
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
static void write_masked_data(const uint8_t *in, uint8_t *out, size_t size, uint8_t mask[4])
{
    *out++ = mask[0];
    *out++ = mask[1];
    *out++ = mask[2];
    *out++ = mask[3];

    while (size > 3) {
        *out++ = *in++ ^ mask[0];
        *out++ = *in++ ^ mask[1];
        *out++ = *in++ ^ mask[2];
        *out++ = *in++ ^ mask[3];
        size -= 4;
    }
    if (size > 0)
        *out++ = *in++ ^ mask[0];
    if (size > 1)
        *out++ = *in++ ^ mask[1];
    if (size > 2)
        *out++ = *in++ ^ mask[2];
}

static std::vector<uint8_t> build_frame(const uint8_t *data, size_t len, websocket::opcode op)
{
    // out_size is the size of the buffer data + WebSocket frame
    size_t out_size = get_frame_size(len);
    auto frame = std::vector<uint8_t>(out_size);
    uint8_t mask[4];
    std::generate(mask, mask + 4, std::ref(generator));

    frame[0] = 0x80;  // FIN bit set (this is a complete frame)
    frame[0] |= static_cast<uint8_t>(op);

    const auto *read_from = reinterpret_cast<const uint8_t *>(data);
    auto *write_to = &frame[out_size - len - 4];  // -4 to get to start of where mask goes

    write_frame_size(frame.data(), len);
    write_masked_data(read_from, write_to, len, mask);
    return frame;
}

const char *websocket::error_category::name() const noexcept
{
    return "WebSocket";
}

std::string websocket::error_category::message(int ev) const noexcept
{
    switch (websocket::error(ev)) {
        case websocket::error::websocket_connection_closed:
            return "The WebSocket connection has closed";
        case websocket::error::server_masked_data:
            return "The server sent masked data";
        case websocket::error::upgrade_failed:
            return "WebSocket upgrade failed";
        case websocket::error::bad_upgrade_key:
            return "The server sent a bad Sec-WebSocket-Accept";
        case websocket::error::no_upgrade_key:
            return "No Sec-WebSocket-Accept returned by server";
    }
    return "Unknown WebSocket error";
}

bool websocket::error_category::equivalent(const boost::system::error_code &code,
                                           int condition) const noexcept
{
    return &code.category() == this && static_cast<int>(code.value()) == condition;
}

const boost::system::error_category &websocket::error_category::instance()
{
    static websocket::error_category instance;
    return instance;
}

boost::system::error_code make_error_code(websocket::error code) noexcept
{
    return {(int) code, websocket::error_category::instance()};
}
