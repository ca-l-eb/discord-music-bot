#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/process/io.hpp>
#include <iostream>

#include "audio_source/youtube_dl.h"

youtube_dl_source::youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                                     const std::string &url, error_cb c)
    : ctx{ctx}, encoder{encoder}, pipe{ctx}, callback{c}
{
    make_process(url);
}

audio_frame youtube_dl_source::next()
{
    AVFrame *avf = decoder->next_frame();
    audio_frame frame{};
    if (avf) {
        // buf 512 so we dont exceed UDP limit
        unsigned char buf[512];
        int frame_count;
        auto *resampled_data = reinterpret_cast<int16_t *>(resampler->resample(avf, frame_count));
        if (frame_count > 0) {
            // TODO: make sure frame_size is reasonable, 20 ms (960 samples) proabably
            int encoded_len = encoder.encode(resampled_data, frame_count, buf, sizeof(buf));
            if (encoded_len > 0) {
                frame.frame_count = frame_count;
                frame.opus_encoded_data.reserve(encoded_len);
                frame.opus_encoded_data.insert(frame.opus_encoded_data.end(), buf,
                                               buf + encoded_len);
            }
        }
    } else {
        // We read the last frame, clear any used memory
        audio_file_data.clear();
    }
    return frame;
}

void youtube_dl_source::make_process(const std::string &url)
{
    namespace bp = boost::process;
    // Formats at https://github.com/rg3/youtube-dl/blob/master/youtube_dl/extractor/youtube.py
    // Prefer opus, vorbis, aac
    child = bp::child{"youtube-dl -f 250/251/249/171/172 -o - " + url,
                      bp::std_in<bp::null, bp::std_err> bp::null, bp::std_out > pipe};

    std::cout << "[youtube-dl source] created process for " << url << "\n";

    audio_file_data.clear();
    avio = std::make_unique<avio_info>(audio_file_data);
    decoder = std::make_unique<audio_decoder>();
    state = decoder_state::start;
    notified = false;

    read_from_pipe({}, 0);
}

void youtube_dl_source::read_from_pipe(const boost::system::error_code &e, size_t transferred)
{
    if (transferred > 0) {
        // Commit any transferred data to the audio_file_data vector
        audio_file_data.insert(audio_file_data.end(), buffer.begin(), buffer.begin() + transferred);
        if (!notified && (audio_file_data.size() > 32768 || e == boost::asio::error::eof)) {
            if (state != decoder_state::ready)
                try_stream();

            if (state == decoder_state::ready) {
                notified = true;
                boost::asio::post(ctx, [=]() { callback({}); });
            }
        }
    }

    if (!e) {
        auto pipe_read_cb = [&](auto &ec, size_t transferred) { read_from_pipe(ec, transferred); };

        // Read from the pipe and fill up the audio_file_data vector
        boost::asio::async_read(pipe, boost::asio::buffer(buffer), pipe_read_cb);
    } else if (e == boost::asio::error::eof) {
        std::cout << "[youtube-dl source] got eof from async_pipe\n";

        if (audio_file_data.empty()) {
            // TODO: add error codes
            boost::asio::post(ctx,
                              [=]() { callback(make_error_code(boost::system::errc::io_error)); });
            return;
        }

        try {
            // Close the pipe, allow the child to terminate
            pipe.close();
            child.wait();
        } catch (...) {
        }

        if (!notified) {
            notified = true;
            boost::asio::post(ctx, [=]() { callback({}); });
        }
    } else {
        std::cerr << "[youtube-dl source] pipe read error: " << e.message() << "\n";
        boost::asio::post(ctx, [=]() { callback(e); });
    }
}

void youtube_dl_source::try_stream()
{
    try {
        if (state == decoder_state::start) {
            if (decoder->open_input(*avio) >= 0) {
                state = decoder_state::opened_input;
            }
        }
        if (state == decoder_state::opened_input) {
            if (decoder->find_stream_info() >= 0) {
                state = decoder_state::found_stream_info;
            }
        }
        if (state == decoder_state::found_stream_info) {
            if (decoder->find_best_stream() >= 0) {
                state = decoder_state::found_best_stream;
            }
        }
        if (state == decoder_state::found_best_stream) {
            decoder->open_decoder();
            state = decoder_state::opened_decoder;
        }
        if (state == decoder_state::opened_decoder) {
            resampler = std::make_unique<audio_resampler>(*decoder, 48000, 2, AV_SAMPLE_FMT_S16);
            state = decoder_state::ready;
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
    }
}
