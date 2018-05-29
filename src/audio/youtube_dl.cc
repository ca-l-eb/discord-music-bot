#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/process/io.hpp>
#include <iostream>

#include "audio/youtube_dl.h"

youtube_dl_source::youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                                     const std::string &url, error_cb c)
    : ctx{ctx}, encoder{encoder}, pipe{ctx}, callback{c}, url{url}
{
}

opus_frame youtube_dl_source::next()
{
    auto avf = decoder->next_frame();
    auto frame = opus_frame{};
    if (avf.data && !avf.eof) {
        auto buf = std::array<unsigned char, 512>{};
        auto resampled = static_cast<float *>(nullptr);
        auto frame_count = resampler->resample(avf.data, reinterpret_cast<void **>(&resampled));
        if (frame_count > 0) {
            // TODO: make sure frame_size is reasonable, 20 ms (960 samples) probably
            auto encoded_len =
                encoder.encode_float(resampled, frame_count, buf.data(), sizeof(buf));
            if (encoded_len > 0) {
                frame.frame_count = frame_count;
                frame.data.reserve(encoded_len);
                frame.data.insert(frame.data.end(), buf.data(), buf.data() + encoded_len);
                frame.end_of_source = false;
            }
        }
        // std::cout << "[youtube-dl source] source frame count: " << avf.data->nb_samples
        //          << " resampled frame count:" << frame.frame_count << "\n";
    }
    if (avf.eof) {
        // We read the last frame, clear any used memory
        std::cout << "[youtube-dl source] got end of source\n";
        audio_file_data.clear();
        frame.end_of_source = true;
    }
    return frame;
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
        auto pipe_read_cb = [weak = weak_from_this()](auto &ec, size_t transferred) {
            if (auto self = weak.lock())
                self->read_from_pipe(ec, transferred);
        };

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
            resampler = std::make_unique<audio_resampler>(*decoder, 48000, 2, AV_SAMPLE_FMT_FLT);
            state = decoder_state::ready;
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
    }
}
