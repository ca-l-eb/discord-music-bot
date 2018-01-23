#include <audio_source/youtube_dl.h>
#include <iostream>

youtube_dl_source::youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                                     const std::string &url, error_cb c)
    : ctx{ctx}, encoder{encoder}, pipe{ctx}, callback{c}
{
    make_process(url);
}

audio_frame youtube_dl_source::next()
{
    if (!frames.empty()) {
        auto f = frames.front();
        frames.pop_front();
        return f;
    }
    return audio_frame{};
}

void youtube_dl_source::make_process(const std::string &url)
{
    namespace bp = boost::process;
    // Formats at https://github.com/rg3/youtube-dl/blob/master/youtube_dl/extractor/youtube.py
    // Prefer opus, vorbis, aac
    child =
        bp::child{"youtube-dl -f 251/250/249/172/171/141/140/139/256/258/325/328/13 -o - " + url,
                  bp::std_in<bp::null, bp::std_err> bp::null, bp::std_out > pipe};

    std::cout << "[youtube-dl source] created process for " << url << "\n";

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

        if (audio_file_data.empty()) {
            // TODO: add error codes
            ctx.post([=]() { callback(make_error_code(boost::system::errc::io_error)); });
            return;
        }
        // Create avio structure (holds audio data), decoder (demuxing and decoding), and
        // resampler
        avio = std::make_unique<avio_info>(audio_file_data);
        decoder = std::make_unique<audio_decoder>(*avio);
        resampler = std::make_unique<audio_resampler>(*decoder, 48000, 2, AV_SAMPLE_FMT_S16);

        // Close the pipe, allow the child to terminate
        pipe.close();
        child.wait();

        // We've read all the data from the pipe... Now to re-encode it all
        encode_audio();

        // Notify that we are done, no errors
        ctx.post([=]() { callback({}); });
    } else {
        std::cerr << "[youtube-dl source] pipe read error: " << e.message() << "\n";
        ctx.post([=]() { callback(e); });
    }
}

void youtube_dl_source::encode_audio()
{
    using namespace std::chrono;
    std::cout << "[youtube-dl source] decoding audio...\n";
    auto decode_start = high_resolution_clock::now();

    // decode all the data into the vector
    AVFrame *avf;
    std::vector<int16_t> data;
    data.reserve(1024*1024*10);
    avf = decoder->next_frame();
    while (avf) {
        auto start = high_resolution_clock::now();
        avf = decoder->next_frame();
        if (!avf) break;
        auto decoder_end = high_resolution_clock::now();
        int frame_count;
        auto *resampled_data = reinterpret_cast<int16_t*>(resampler->resample(avf, frame_count));
        if (frame_count > 0)
            data.insert(data.end(), resampled_data, resampled_data + frame_count * 2);
        auto end = high_resolution_clock::now();
        std::cout << "[youtube-dl source] decode "
                  << duration_cast<microseconds>(decoder_end - start).count() << "us resample "
                  << duration_cast<microseconds>(end - decoder_end).count() << "us. total "
                  << duration_cast<microseconds>(end - start).count() << "us\n";
    }
    audio_file_data.clear();

    std::cout << "[youtube-dl source] encoding audio...\n";

    const int num_channels = 2;
    const int frame_size = 960;
    const int size = num_channels * frame_size;
    int num_blocks = data.size() / size;

    auto encode_start = high_resolution_clock::now();
    int16_t *src = data.data();
    for (int i = 0 ; i < num_blocks; i++) {
        audio_frame f{};

        auto start = high_resolution_clock::now();
        auto encoded_len = encoder.encode(src, frame_size, buffer.data(), buffer.size());

        if (encoded_len > 0) {
            f.frame_count = frame_size;
            f.opus_encoded_data.insert(f.opus_encoded_data.end(), buffer.data(),
                                       buffer.data() + encoded_len);
            frames.push_back(std::move(f));
        }
        src += size;
        auto end = high_resolution_clock::now();
        std::cout << "[youtube-dl source] encode "
                  << duration_cast<microseconds>(end - start).count() << "us\n";
    }
    int remaining = data.size() - num_blocks * size;
    int remaining_frames = remaining / 2;
    if (remaining > 0) {
        assert(remaining % 2 == 0);
        audio_frame f{};
        auto encoded_len = encoder.encode(src, remaining_frames, buffer.data(), buffer.size());
        if (encoded_len > 0) {
            f.frame_count = frame_size;
            f.opus_encoded_data.insert(f.opus_encoded_data.end(), buffer.data(),
                                       buffer.data() + encoded_len);
            frames.push_back(std::move(f));
        }
    }
    auto total_end = high_resolution_clock::now();
    std::cout << "[youtube_dl_source] decode total "
              << duration_cast<microseconds>(encode_start - decode_start).count() << "\n";
    std::cout << "[youtube_dl_source] encode total "
              << duration_cast<microseconds>(total_end - encode_start).count() << "\n";
    std::cout << "[youtube-dl source] ready\n";
}
