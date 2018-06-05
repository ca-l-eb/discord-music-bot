#ifndef DECODING_H
#define DECODING_H

#include <boost/circular_buffer.hpp>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

class audio_decoder;

struct buffer_data {
    std::vector<uint8_t> &data;
    size_t loc;
};

struct audio_frame {
    AVFrame *data;
    bool eof;
};

template<typename T>
struct audio_samples {
    T *data;
    int frame_count;
};

class avio_context
{
public:
    avio_context(std::vector<uint8_t> &audio_data);
    ~avio_context();

private:
    AVIOContext *avio_ctx;
    uint8_t *avio_buf;
    buffer_data audio_file_data;
    size_t avio_buf_len;

    friend class audio_decoder;
};

template<typename T, AVSampleFormat format, int sample_rate, int channels>
class audio_resampler
{
private:
    SwrContext *swr;
    uint8_t *frame_buf;
    int current_alloc;

    void grow(int bytes_wanted);

public:
    audio_resampler(audio_decoder &decoder);
    ~audio_resampler();
    void feed(audio_frame *frame);
    audio_samples<T> read(int samples);
    int delayed_samples();
};

using float_resampler = audio_resampler<float, AV_SAMPLE_FMT_FLT, 48000, 2>;
using s16_resampler = audio_resampler<int16_t, AV_SAMPLE_FMT_S16, 48000, 2>;

class audio_decoder
{
public:
    audio_decoder();
    ~audio_decoder();
    void open_input(avio_context &av);
    void find_stream_info();
    void find_best_stream();
    void open_decoder();
    audio_frame next_frame();  // Get next frame from the audio stream

private:
    friend float_resampler;
    friend s16_resampler;

    AVFormatContext *format_context;  // Demuxer interface
    AVCodecContext *decoder_context;  // Decoder interface
    AVCodec *decoder;
    AVFrame *frame;
    AVPacket packet;
    int stream_index;
    bool do_read;
    bool do_feed;
    bool do_output;
    bool flushed;
    bool eof;

    void read_packet();
    void feed_decoder();
    void flush_decoder();
    void decode_frame();
};

template<typename T, AVSampleFormat format, int sample_rate, int channels>
class simple_audio_decoder
{
public:
    simple_audio_decoder();
    ~simple_audio_decoder() = default;
    void feed(const uint8_t *data, size_t bytes);
    int read(T *data, int samples);
    int available();
    bool ready();
    bool done();
    void check_stream();

private:
    using resampler_type = audio_resampler<T, format, sample_rate, channels>;

    std::vector<uint8_t> input_buffer;

    avio_context avio;
    audio_decoder decoder;
    std::unique_ptr<resampler_type> resampler;

    enum class decoder_state {
        start,
        opened_input,
        found_stream_info,
        found_best_stream,
        opened_decoder,
        ready,
        eof
    } state;
};

#endif

using float_audio_decoder = simple_audio_decoder<float, AV_SAMPLE_FMT_FLT, 48000, 2>;
using s16_audio_decoder = simple_audio_decoder<int16_t, AV_SAMPLE_FMT_S16, 48000, 2>;
