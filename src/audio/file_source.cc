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
    const auto channels = 2;
    const auto frames_wanted = 960;
    auto frame = opus_frame{};
    auto buffer = reinterpret_cast<float *>(this->buffer.data());
    auto read = decoder.read(buffer, frames_wanted);
    if (read < frames_wanted) {
        if (!decoder.done())
            return {};

        frame.end_of_source = true;

        // Want to clear the remaining frames to 0
        auto start = buffer + read * channels;
        auto end = buffer + frames_wanted * channels;
        std::fill(start, end, 0.0f);
    }
    if (read > 0) {
        auto buf = std::array<uint8_t, 512>{};
        auto encoded_len = encoder.encode(buffer, frames_wanted, buf.data(), buf.size());
        if (encoded_len > 0) {
            frame.data.reserve(encoded_len);
            frame.data.insert(std::begin(frame.data), buf.data(), buf.data() + encoded_len);
        }
    }
    frame.frame_count = frames_wanted;
    return frame;
}

void file_source::prepare()
{
    auto read = 0;
    auto ifs = std::ifstream{file_path};
    if (!ifs) {
        boost::asio::post(ctx,
                          [=]() { callback({make_error_code(boost::system::errc::io_error)}); });
    }
    auto buf = std::array<char, 4096>{};
    while (ifs.good()) {
        ifs.read(buf.data(), buf.size());
        read += ifs.gcount();
        decoder.feed(reinterpret_cast<uint8_t *>(buf.data()), ifs.gcount());
    }
    // no error otherwise
    boost::asio::post(ctx, [=]() { callback({}); });
    std::cout << "[file source] read " << read << " bytes\n";
}
