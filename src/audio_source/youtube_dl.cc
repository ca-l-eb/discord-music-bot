#include <audio_source/youtube_dl.h>
#include <iostream>

youtube_dl_source::youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                                     const std::string &url, error_cb c)
    : pipe{ctx}, encoder{encoder}, callback{c}
{
    make_process(ctx, url);
}

audio_frame youtube_dl_source::next()
{
    assert(avio);
    assert(decoder);
    assert(resampler);
    audio_frame af{};

    return af;
}

void youtube_dl_source::make_process(boost::asio::io_context &ctx, const std::string url)
{
    namespace bp = boost::process;
    // Formats at https://github.com/rg3/youtube-dl/blob/master/youtube_dl/extractor/youtube.py
    // Prefer opus, vorbis, aac
    child = bp::child{
        "youtube-dl -r 100M -f 251/250/249/172/171/141/140/139/256/258/325/328/13 -o - " + url,
        bp::std_in<bp::null, bp::std_err> bp::null, bp::std_out > pipe};

    std::cout << "[youtube-dl source] created processes for " << url << "\n";

    read_from_pipe({}, 0);
}

void youtube_dl_source::read_from_pipe(const boost::system::error_code &e, size_t transferred)
{
    if (transferred > 0) {
        // Commit any transferred data to the audio_file_data vector
        audio_file_data.insert(audio_file_data.end(), buffer.begin(), buffer.begin() + transferred);
    }
    if (!e) {
        auto pipe_read_cb = [&](auto &ec, size_t transferred) { read_from_pipe(ec, transferred); };

        // Read from the pipe and fill up the audio_file_data vector
        boost::asio::async_read(pipe, boost::asio::buffer(buffer), pipe_read_cb);

    } else if (e == boost::asio::error::eof) {
        std::cout << "[youtube-dl source] got eof from async_pipe\n";
        // Create avio structure (holds audio data), decoder (demuxing and decoding), and
        // resampler
        avio = std::make_unique<avio_info>(audio_file_data);
        decoder = std::make_unique<audio_decoder>(*avio);
        resampler = std::make_unique<audio_resampler>(*decoder, 48000, 2, AV_SAMPLE_FMT_S16);

        // Close the pipe, allow the child to terminate
        pipe.close();
        child.wait();

        // We've read all the data from the pipe... Now to re-encode it all

        // Notify that we are done, no errors
        callback({});
    } else {
        std::cerr << "[youtube-dl source] pipe read error: " << e.message() << "\n";
    }
}
