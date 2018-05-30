#ifndef CMD_DECODING_H
#define CMD_DECODING_H

#include <boost/circular_buffer.hpp>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

struct buffer_data {
    std::vector<uint8_t> &data;
    size_t loc;
};

class avio_info
{
public:
    avio_info(std::vector<uint8_t> &audio_data);
    ~avio_info();

private:
    AVIOContext *avio_context;
    uint8_t *avio_buf;
    buffer_data audio_file_data;
    size_t avio_buf_len;

    friend class audio_decoder;
};

struct av_frame {
    AVFrame *data;
    bool eof;
};

class audio_decoder
{
public:
    audio_decoder();
    ~audio_decoder();
    int read();
    int feed(bool flush);
    int decode();
    av_frame next_frame();  // Get next frame from the audio stream
    int open_input(avio_info &av);
    int find_stream_info();
    int find_best_stream();
    void open_decoder();

private:
    AVPacket packet;
    AVFormatContext *format_context;  // Demuxer interface
    AVCodecContext *decoder_context;  // Decoder interface
    AVCodec *decoder;
    AVFrame *frame;
    int stream_index;  // Audio stream in format_context
    bool do_read;
    bool do_feed;
    bool eof;

    friend class audio_resampler;
};

class audio_resampler
{
private:
    SwrContext *swr;
    uint8_t *frame_buf;
    int current_alloc;
    AVSampleFormat format;

    void grow();

public:
    audio_resampler(audio_decoder &decoder, int sample_rate, int channels, AVSampleFormat format);
    ~audio_resampler();
    int resample(av_frame frame, void **data);
};

template<typename T, int sample_rate, int channels, AVSampleFormat format>
class simple_audio_decoder
{
public:
    simple_audio_decoder();
    ~simple_audio_decoder() = default;
    void feed(const void *data, size_t bytes);
    int read(T *data, int samples);
    int available();
    bool ready();
    bool done();

private:
    boost::circular_buffer<T> output_buffer;
    std::unique_ptr<avio_info> avio;
    std::unique_ptr<audio_decoder> decoder;
    std::unique_ptr<audio_resampler> resampler;
    std::vector<uint8_t> input_buffer;

    enum class decoder_state {
        start,
        opened_input,
        found_stream_info,
        found_best_stream,
        opened_decoder,
        ready,
        eof
    } state;

    void try_stream();
};

#endif

using float_audio_decoder = simple_audio_decoder<float, 48000, 2, AV_SAMPLE_FMT_FLT>;
