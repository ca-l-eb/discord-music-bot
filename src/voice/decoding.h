#ifndef CMD_DECODING_H
#define CMD_DECODING_H

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

struct buffer_data {
    uint8_t *data;
    size_t size;
    size_t loc;
};

struct avio_info {
    AVIOContext *avio_context;
    uint8_t *avio_buf;
    buffer_data audio_file_data;
    size_t avio_buf_len;

    avio_info(std::vector<uint8_t> &audio_data);
    ~avio_info();
};

struct audio_decoder {
    AVPacket packet;
    AVFormatContext *format_context;  // Demuxer interface
    AVCodecContext *decoder_context;  // Decoder interface
    AVFrame *frame;
    int stream_index;  // Audio stream in format_context
    bool do_read;
    bool do_feed;

    audio_decoder(avio_info &av);
    ~audio_decoder();
    int read();
    int feed(bool flush);
    int decode();
    AVFrame *next_frame();  // Get next frame from the audio stream
};

struct audio_resampler {
    SwrContext *swr;
    uint8_t *frame_buf;
    int current_alloc;
    AVSampleFormat format;

    audio_resampler(audio_decoder &decoder, int sample_rate, int channels, AVSampleFormat format);
    ~audio_resampler();
    void *resample(AVFrame *frame, int &frame_count);
};

#endif
