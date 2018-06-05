#include <boost/asio/post.hpp>
#include <fstream>
#include <iostream>

#include "audio/file_source.h"

file_source::file_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                         const std::string &file_path, error_cb c)
    : ctx{ctx}, encoder{encoder}, file_path{file_path}, callback{c}
{
    std::cout << "[file source] playing " << file_path << "\n";
}

opus_frame file_source::next()
{
    return next_frame(decoder, encoder, buffer.data(), buffer.size());
}

void file_source::prepare()
{
    auto read = 0;
    auto ifs = std::ifstream{file_path};
    auto error = boost::system::error_code{};
    if (!ifs) {
        error = make_error_code(boost::system::errc::io_error);
        boost::asio::post(ctx, [=]() { callback(error); });
        return;
    }
    auto buf = std::array<char, 4096>{};
    while (ifs.good()) {
        ifs.read(buf.data(), buf.size());
        read += ifs.gcount();
        decoder.feed(reinterpret_cast<uint8_t *>(buf.data()), ifs.gcount());
    }
    std::cout << "[file source] read " << read << " bytes\n";
    decoder.check_stream();
    if (!decoder.ready())
        error = make_error_code(boost::system::errc::io_error);

    boost::asio::post(ctx, [=]() { callback(error); });
}
