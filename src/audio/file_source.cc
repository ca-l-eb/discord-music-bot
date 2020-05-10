#include <boost/asio/post.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

#include "audio/file_source.h"

file_source::file_source(discord::voice_context &voice_context, const std::string &file_path)
    : voice_context{voice_context}, file_path{file_path}
{
    SPDLOG_INFO("loading file {}", file_path);
}

opus_frame file_source::next()
{
    return next_frame(decoder, voice_context.get_encoder(), buffer.data(), buffer.size());
}

void file_source::prepare()
{
    auto read = 0;
    auto ifs = std::ifstream{file_path};
    auto error = boost::system::error_code{};
    if (!ifs) {
        error = make_error_code(boost::system::errc::io_error);
        voice_context.notify_audio_source_ready(error);
        return;
    }
    auto buf = std::array<char, 4096>{};
    while (ifs.good()) {
        ifs.read(buf.data(), buf.size());
        read += ifs.gcount();
        decoder.feed(reinterpret_cast<uint8_t *>(buf.data()), ifs.gcount());
    }
    SPDLOG_DEBUG("read {} bytes", read);
    decoder.check_stream();
    if (!decoder.ready())
        error = make_error_code(boost::system::errc::io_error);

    voice_context.notify_audio_source_ready(error);
}
