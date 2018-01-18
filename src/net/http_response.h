#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <boost/asio.hpp>

#include <map>
#include <string>

class http_response
{
public:
    http_response();
    void parse(boost::asio::streambuf &buf);
    int status_code();
    std::string status_message();
    std::string body();
    std::multimap<std::string, std::string> &headers();
    std::string version();
    bool is_complete();
    bool wants_all();

private:
    int status;
    std::string http_version;
    std::string status_message_str;
    std::string body_str;
    std::multimap<std::string, std::string> headers_map;
    size_t length;

    enum class body_type { none, chunked, content_length, all } b_type;
    enum class parse_state { begin, headers, body, trailing_headers, done } p_state;
    enum class chunk_state { read_length, read_body } c_state;

    void parse_status_line(boost::asio::streambuf &buf);
    void parse_headers(boost::asio::streambuf &buf);
    void read_body(boost::asio::streambuf &buf);

    bool next_line(boost::asio::streambuf &buf, std::string &write_to);

    void process_headers();
    void check_response_status(const std::string &status_line);
    void add_header_to_map(const std::string &line);
    void check_body_method();
    void check_content_length();
    void check_transfer_encoding();
    void do_chunked(boost::asio::streambuf &buf);
    void do_content_length(boost::asio::streambuf &buf);
    void do_read_all(boost::asio::streambuf &buf);
    void check_connection_close();
};

#endif
