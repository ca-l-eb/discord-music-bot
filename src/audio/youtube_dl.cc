#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/process/io.hpp>
#include <spdlog/spdlog.h>

#include "audio/youtube_dl.h"

static const auto channels = 2;

youtube_dl_source::youtube_dl_source(discord::voice_context &voice_context, const std::string &url)
    : voice_context{voice_context}, pipe{voice_context.get_io_context()}, url{url}
{
}

opus_frame youtube_dl_source::next()
{
    return next_frame(decoder, voice_context.get_encoder(), buffer.data(), buffer.size());
}

void youtube_dl_source::prepare()
{
    make_process(url);
}

void youtube_dl_source::make_process(const std::string &url)
{
    namespace bp = boost::process;
    // Formats at https://github.com/rg3/youtube-dl/blob/master/youtube_dl/extractor/youtube.py
    // Prefer opus, vorbis, aac
    child = bp::child{"youtube-dl -f 250/251/249/171/172 -o - " + url,
                      bp::std_in<bp::null, bp::std_err> bp::null, bp::std_out > pipe};
    notified = false;
    bytes_sent_to_decoder = 0;

    SPDLOG_INFO("created process for {}", url);
    read_from_pipe({}, 0);
}

void youtube_dl_source::read_from_pipe(const boost::system::error_code &e, size_t transferred)
{
    if (transferred > 0) {
        // Commit any transferred data to the audio_file_data vector
        decoder.feed(buffer.data(), transferred);
        bytes_sent_to_decoder += transferred;
    }
    if (!e) {
        auto pipe_read_cb = [weak = weak_from_this()](const auto &ec, size_t transferred) {
            if (auto self = weak.lock())
                self->read_from_pipe(ec, transferred);
        };
        // Read from the pipe and fill up the audio_file_data vector
        boost::asio::async_read(pipe, boost::asio::buffer(buffer), pipe_read_cb);
    } else if (e == boost::asio::error::eof || (bytes_sent_to_decoder > 0)) {
        SPDLOG_DEBUG("got eof from async_pipe");

        auto be = boost::system::error_code{};
        auto se = std::error_code{};

        // Close the pipe, allow the child to terminate
        pipe.close(be);
        child.wait(se);

        if (be)
            SPDLOG_ERROR("error closing pipe: {}", be.message());
        if (se)
            SPDLOG_ERROR("error waiting for process: {}", se.message());
        if (!notified) {
            SPDLOG_INFO("notifying voice context audio is ready");
            decoder.check_stream();
            notified = true;
            auto error = decoder.ready() ? boost::system::error_code{}
                                         : make_error_code(boost::system::errc::io_error);
            voice_context.notify_audio_source_ready(error);
        }
    } else {
        SPDLOG_ERROR("pipe read error: {}", e.message());
        if (!notified) {
            voice_context.notify_audio_source_ready(e);
            notified = true;
        }
    }
}
