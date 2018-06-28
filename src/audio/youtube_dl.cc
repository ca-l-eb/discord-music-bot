#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/process/io.hpp>
#include <iostream>

#include "audio/youtube_dl.h"

static const auto channels = 2;

youtube_dl_source::youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                                     const std::string &url, error_cb c)
    : ctx{ctx}, encoder{encoder}, pipe{ctx}, callback{c}, url{url}
{
}

opus_frame youtube_dl_source::next()
{
    return next_frame(decoder, encoder, buffer.data(), buffer.size());
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

    std::cout << "[youtube-dl source] created process for " << url << "\n";
    read_from_pipe({}, 0);
}

void youtube_dl_source::read_from_pipe(const boost::system::error_code &e, size_t transferred)
{
    if (transferred > 0) {
        // Commit any transferred data to the audio_file_data vector
        decoder.feed(buffer.data(), transferred);
        bytes_sent_to_decoder += transferred;
    }
#if 0
    if (!notified) {
        if (bytes_sent_to_decoder >= 256 * 1024) {
            decoder.check_stream();
        }
        if (decoder.ready()) {
            notified = true;
            boost::asio::post(ctx, [=]() { callback({}); });
        }
    }
#endif
    if (!e) {
        auto pipe_read_cb = [weak = weak_from_this()](const auto &ec, size_t transferred) {
            if (auto self = weak.lock())
                self->read_from_pipe(ec, transferred);
        };
        // Read from the pipe and fill up the audio_file_data vector
        boost::asio::async_read(pipe, boost::asio::buffer(buffer), pipe_read_cb);
    } else if (e == boost::asio::error::eof) {
        std::cout << "[youtube-dl source] got eof from async_pipe\n";

        auto be = boost::system::error_code{};
        auto se = std::error_code{};

        // Close the pipe, allow the child to terminate
        pipe.close(be);
        child.wait(se);

        if (be)
            std::cerr << "[youtube-dl source] error closing pipe: " << be.message() << "\n";
        if (se)
            std::cerr << "[youtube-dl source] error waiting for process: " << se.message() << "\n";
        if (!notified) {
            decoder.check_stream();
            notified = true;
            auto error = decoder.ready() ? boost::system::error_code{}
                                         : make_error_code(boost::system::errc::io_error);
            boost::asio::post(ctx, [=]() { callback(error); });
        }
    } else {
        std::cerr << "[youtube-dl source] pipe read error: " << e.message() << "\n";
        if (!notified) {
            boost::asio::post(ctx, [=]() { callback(e); });
            notified = true;
        }
    }
}
