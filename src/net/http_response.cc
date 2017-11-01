#include <algorithm>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <regex>

#include <net/http_response.h>
#include <string_utils.h>

http_response::http_response()
    : status{0}
    , length{0}
    , b_type{body_type::none}
    , p_state{parse_state::begin}
    , c_state{chunk_state::read_length}
{
}

void http_response::parse(boost::asio::streambuf &buf)
{
    if (p_state == parse_state::begin) {
        // Read first line extracting HTTP response message a status code
        parse_status_line(buf);
    }
    if (p_state == parse_state::headers) {
        // Continuing reading until we get \r\n\r\n
        parse_headers(buf);
    }
    if (p_state == parse_state::body) {
        // Read until we need to stop (chunked, content-length, all, etc.)
        read_body(buf);
    }
}

void http_response::parse_status_line(boost::asio::streambuf &buf)
{
    std::string line;

    // Look for newline
    if (next_line(buf, line)) {
        check_response_status(line);
        p_state = parse_state::headers;
    }
}

void http_response::parse_headers(boost::asio::streambuf &buf)
{
    std::string line;
    while (next_line(buf, line)) {
        if (line.empty()) {
            // Got last header line
            p_state = parse_state::body;
            process_headers();
            break;
        }

        add_header_to_map(line);
    }
}

void http_response::read_body(boost::asio::streambuf &buf)
{
    if (status == 204) {
        // No content, we're done!
        p_state = parse_state::done;
        return;
    }

    switch (b_type) {
        case body_type::none:
            p_state = parse_state::done;
            break;
        case body_type::chunked:
            do_chunked(buf);
            break;
        case body_type::content_length:
            do_content_length(buf);
            break;
        case body_type::all:
            do_read_all(buf);
            break;
    }
}

int http_response::status_code()
{
    return status;
}

std::string http_response::status_message()
{
    return status_message_str;
}

std::string http_response::body()
{
    return body_str;
}

std::multimap<std::string, std::string> &http_response::headers()
{
    return headers_map;
}

std::string http_response::version()
{
    return http_version;
}

void http_response::process_headers()
{
    check_connection_close();  // Note: must be called before check_body_method
    check_body_method();
}

void http_response::check_response_status(const std::string &status_line)
{
    static std::regex re{R"(^(HTTP/\S+) (\d{3}) (.*)$)"};

    std::smatch matcher;
    std::regex_search(status_line, matcher, re);
    if (matcher.empty())
        throw std::runtime_error("Invalid HTTP response status line");

    http_version = matcher.str(1);
    status = std::stoi(matcher.str(2));
    status_message_str = matcher.str(3);
}

void http_response::add_header_to_map(const std::string &line)
{
    static std::regex re{R"(^(\S+):\s*(.*)$)"};
    std::smatch matcher;
    std::regex_search(line, matcher, re);
    if (!matcher.empty()) {
        std::string first = cmd::string_utils::to_lower(matcher.str(1));
        auto pair = std::pair<std::string, std::string>(first, matcher.str(2));
        headers_map.insert(pair);
    }
}
void http_response::check_body_method()
{
    check_content_length();
    // Check for Transfer-Encoding second because we always accept chunked data
    // in favor of Content-Length when provided
    check_transfer_encoding();
}

void http_response::check_content_length()
{
    auto range = headers_map.equal_range("content-length");
    for (auto it = range.first; it != range.second; ++it) {
        auto len = static_cast<size_t>(std::stoi(it->second));
        if (len != length) {
            if (b_type == body_type::content_length) {
                throw std::runtime_error("Got conflicting Content-Length headers");
            }
        }
        length = len;
        b_type = body_type::content_length;
    }
}

void http_response::check_transfer_encoding()
{
    auto range = headers_map.equal_range("transfer-encoding");
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.find("chunked") != std::string::npos)
            b_type = body_type::chunked;
        c_state = chunk_state::read_length;
    }
    if (b_type == body_type::chunked) {
        // TODO: check for Trailer header to add only listed trailing headers
    }
}

void http_response::do_chunked(boost::asio::streambuf &buf)
{
    std::string line;
    while (p_state == parse_state::body) {
        if (c_state == chunk_state::read_length) {
            if (next_line(buf, line)) {
                // Convert hex chunk length
                length = static_cast<size_t>(std::stoi(line, nullptr, 16));
                if (length == 0) {
                    // Done with chunked body, go on to read any trailing headers
                    p_state = parse_state::trailing_headers;
                    break;
                }
                c_state = chunk_state::read_body;
            } else {
                break;
            }
        }
        if (c_state == chunk_state::read_body && buf.size() > 0) {
            if (length > 0) {
                // More to read, read as much as needed (or as much as that is buffered)
                auto begin = boost::asio::buffers_begin(buf.data());
                auto end = begin + std::min(length, buf.size());

                std::string remaining{begin, end};

                length -= remaining.size();
                body_str += remaining;
                buf.consume(remaining.size());
            }
            if (length == 0 && next_line(buf, line)) {
                if (!line.empty()) {
                    throw std::runtime_error("Expected empty line after chunk");
                }
                // Ready to read next chunk length
                c_state = chunk_state::read_length;
            }
        } else {
            break;
        }
    }

    if (p_state == parse_state::trailing_headers) {
        // Check trailing headers
        while (next_line(buf, line)) {
            if (line.length() == 0) {
                // Empty line -> DONE, no more trailing headers
                p_state = parse_state::done;
                break;
            }
            // Add any trailing headers to the headers map
            add_header_to_map(line);
        }
    }
}

void http_response::do_content_length(boost::asio::streambuf &buf)
{
    if (length > 0) {
        // More to read, read as much as needed (or as much as that is buffered)
        auto begin = boost::asio::buffers_begin(buf.data());
        auto end = begin + std::min(length, buf.size());

        std::string remaining{begin, end};

        length -= remaining.size();
        body_str += remaining;
    }
    if (length == 0)
        p_state = parse_state::done;
}

// TODO: need a mechanism to tell the calling code that this http response
// is going to be reading all the data sent
void http_response::do_read_all(boost::asio::streambuf &buf)
{
    if (buf.size() > 0) {
        auto begin = boost::asio::buffers_begin(buf.data());
        auto end = begin + buf.size();

        body_str += std::string{begin, end};
    }
}

void http_response::check_connection_close()
{
    auto range = headers_map.equal_range("connection");
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.find("close") != std::string::npos) {
            // Read all by default if connection closed, will be overwritten
            // by check_body_method if it finds more specific details for body format
            b_type = body_type::all;
        }
    }
}

bool http_response::is_complete()
{
    return p_state == parse_state::done;
}

bool http_response::wants_all()
{
    return b_type == body_type::all;
}

// Extract the next line from the buffer and consume it
bool http_response::next_line(boost::asio::streambuf &buf, std::string &write_to)
{
    auto begin = boost::asio::buffers_begin(buf.data());
    auto end = begin + buf.size();

    // Look for newline
    auto find = std::find(begin, end, '\n');
    if (find != end) {
        // write_to now contains the new line (no \n)
        write_to = std::string{begin, find};

        // Remove carriage return if it exists
        if (write_to.back() == '\r') {
            write_to.pop_back();
            buf.consume(write_to.size() + 2);
        } else {
            buf.consume(write_to.size() + 1);
        }

        return true;
    }
    return false;
}
