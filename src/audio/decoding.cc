#include <algorithm>
#include <cassert>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "decoding.h"

// Some data has been requested, write the results into buf, return the amount of bytes written
static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    assert(opaque);
    auto bd = reinterpret_cast<buffer_data *>(opaque);
    buf_size = std::min<int>(buf_size, bd->data.size() - bd->loc);
    if (buf_size < 0)
        return 0;
    memcpy(buf, &bd->data[bd->loc], buf_size);
    bd->loc += buf_size;
    return buf_size;
}

#if 1
static int64_t seek(void *opaque, int64_t offset, int whence)
{
    assert(opaque);
    auto bd = reinterpret_cast<buffer_data *>(opaque);
    switch (whence) {
        case SEEK_SET:
            return bd->loc = offset;
        case SEEK_CUR:
            return bd->loc += offset;
        case SEEK_END:
            return bd->loc = bd->data.size() + offset;
        case AVSEEK_SIZE:
            return bd->data.size();
    }
    return -1;
}
#endif

avio_info::avio_info(std::vector<uint8_t> &audio_data) : audio_file_data{audio_data, 0}
{
    avio_buf_len = 8192;
    avio_buf = reinterpret_cast<uint8_t *>(av_malloc(avio_buf_len));
    if (!avio_buf)
        throw std::runtime_error{"Could not allocate avio context buffer"};

    // Instead of using avformat_open_input and passing path, we're going to use AVIO
    // which allows us to point to an already allocated area of memory that contains the media
    avio_context = avio_alloc_context(avio_buf, avio_buf_len, 0, &audio_file_data, &read_packet,
                                      nullptr, &seek);
    if (!avio_context)
        throw std::runtime_error{"Could not allocate AVIO context"};
}

avio_info::~avio_info()
{
    av_free(avio_context->buffer);
    av_free(avio_context);
}

audio_decoder::audio_decoder()
    : format_context{nullptr}
    , decoder_context{nullptr}
    , decoder{nullptr}
    , frame{av_frame_alloc()}
    , stream_index{-1}
    , do_read{true}
    , do_feed{true}
    , do_output{true}
    , flushed{false}
    , eof{false}
{
    if (!frame)
        throw std::runtime_error{"Unable to allocate audio frame"};
}

audio_decoder::~audio_decoder()
{
    if (format_context) {
        avformat_close_input(&format_context);
        avformat_free_context(format_context);
    }
    if (decoder_context) {
        avcodec_close(decoder_context);
        avcodec_free_context(&decoder_context);
    }
    if (frame)
        av_frame_free(&frame);
    av_packet_unref(&packet);
}

void audio_decoder::open_input(avio_info &av)
{
    format_context = avformat_alloc_context();
    if (!format_context)
        throw std::runtime_error{"Could not allocate libavformat context"};

    // Use the AVIO context
    format_context->pb = av.avio_context;

    // Open the file, read the header, export information into format_context
    // Frees format_context on failure
    if (avformat_open_input(&format_context, "audio-stream", nullptr, nullptr) != 0)
        throw std::runtime_error{"avformat failed to open input"};
}

void audio_decoder::find_stream_info()
{
    // Some format do not have header, or do not store enough information there so try to read
    // and decode a few frames if necessary to find missing information
    if (avformat_find_stream_info(format_context, nullptr) < 0)
        throw std::runtime_error{"avformat failed to find stream info"};
}

void audio_decoder::find_best_stream()
{
    // Get best audio stream from format_context, and possibly decoder for this stream
    stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream_index < 0)
        throw std::runtime_error{"failed to find best audio stream"};
}

void audio_decoder::open_decoder()
{
    if (stream_index < 0)
        throw std::runtime_error{"No stream found"};

    auto stream = format_context->streams[stream_index];

    // We weren't able to get decoder when opening stream, find decoder by stream's codec_id
    if (!decoder) {
        decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder)
            throw std::runtime_error{"Unable to find audio decoder"};
    }

    // Allocate codec context for the decoder
    decoder_context = avcodec_alloc_context3(decoder);
    if (!decoder_context)
        throw std::runtime_error{"Failed to allocate the audio codec context"};

    if (avcodec_parameters_to_context(decoder_context, stream->codecpar) < 0)
        throw std::runtime_error{"Failed to copy audio codec parameters to decoder context"};

    auto opts = static_cast<AVDictionary *>(nullptr);
    av_dict_set(&opts, "refcounted_frames", "1", 0);
    if (avcodec_open2(decoder_context, decoder, &opts) < 0)
        throw std::runtime_error{"Failed to open decoder for stream"};
}

void audio_decoder::read_packet()
{
    auto error = 0;
    av_init_packet(&packet);
    while ((error = av_read_frame(format_context, &packet)) == 0) {
        if (packet.stream_index != stream_index)
            av_packet_unref(&packet);
        else
            break;
    }
    if (error)
        flush_decoder();
}

// Feed the decoder the next packet from the demuxer
void audio_decoder::feed_decoder()
{
    assert(!flushed);

    auto ret = avcodec_send_packet(decoder_context, &packet);
    switch (ret) {
        case 0:
            // successfully sent packet to decoder
            av_packet_unref(&packet);
            break;
        case AVERROR_EOF:
            // Decoder has been flushed. No new packets can be sent
            // fall through
        case AVERROR(EAGAIN):
            // Decoder denied input. Output must be read using next_frame()
            do_read = false;
            do_feed = false;
            do_output = true;
            break;
        case AVERROR(EINVAL):
            // decoder was unopened, or decoder needs to be flushed
            flush_decoder();
        case AVERROR(ENOMEM):
            throw std::runtime_error{"Decoder failed to add packet to internal queue: no memory"};
    }
}

void audio_decoder::flush_decoder()
{
    assert(!flushed);

    do_feed = false;
    do_read = false;
    flushed = true;
    do_output = true;

    avcodec_send_packet(decoder_context, nullptr);
}

void audio_decoder::decode_frame()
{
    // Retrieve (decoded) frame from decoder
    switch (avcodec_receive_frame(decoder_context, frame)) {
        case 0:
            assert(frame);
            do_output = true;
            do_read = false;
            do_feed = false;
            break;
        case AVERROR(EAGAIN):
            // Error receiving frame from decoder, needs input
            do_read = true;
            do_feed = true;
            break;
        case AVERROR_EOF:
            // Decoder is fully flushed. No more output frames
            eof = true;
            do_output = false;
            if (frame)
                av_frame_free(&frame);
            break;
        case AVERROR(EINVAL):
            throw std::runtime_error{"Attempted to use an unopened decoder"};
    }
}

audio_frame audio_decoder::next_frame()
{
    if (do_read)
        read_packet();

    if (do_feed)
        feed_decoder();

    if (do_output) {
        decode_frame();
        if (do_read && do_feed) {
            return next_frame();
        }
    }
    return {frame, eof};
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
audio_resampler<T, format, sample_rate, channels>::audio_resampler(audio_decoder &decoder)
    : swr{swr_alloc()}, frame_buf{nullptr}, current_alloc{960}
{
    static_assert(sample_rate > 0);
    static_assert(channels > 0);

    if (!swr)
        throw std::runtime_error{"Could not allocate resampling context"};

    av_opt_set_int(swr, "in_channel_count", decoder.decoder_context->channels, 0);
    av_opt_set_int(swr, "out_channel_count", channels, 0);
    av_opt_set_int(swr, "in_channel_layout", decoder.decoder_context->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr, "in_sample_rate", decoder.decoder_context->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", decoder.decoder_context->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", format, 0);
    swr_init(swr);
    if (!swr_is_initialized(swr)) {
        swr_free(&swr);
        throw std::runtime_error{"Could not initialize audio resampler"};
    }

    grow(current_alloc);
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
audio_resampler<T, format, sample_rate, channels>::~audio_resampler()
{
    if (swr)
        swr_free(&swr);
    if (frame_buf)
        av_free(frame_buf);
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
void audio_resampler<T, format, sample_rate, channels>::grow(int bytes_wanted)
{
    while (bytes_wanted > current_alloc)
        current_alloc *= 2;

    if (frame_buf)
        av_free(frame_buf);

    av_samples_alloc(&frame_buf, nullptr, channels, current_alloc, format, 0);

    if (!frame_buf)
        throw std::runtime_error{"Could not allocate resample buffer"};
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
void audio_resampler<T, format, sample_rate, channels>::feed(audio_frame *frame)
{
    assert(swr);

    auto ret = 0;
    if (frame && frame->data) {
        ret = swr_convert(swr, nullptr, 0, const_cast<const uint8_t **>(frame->data->data),
                          frame->data->nb_samples);
    } else {
        ret = swr_convert(swr, nullptr, 0, nullptr, 0);
    }
    if (ret)
        std::cerr << "[audio resampler] error feeding input\n";
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
audio_samples<T> audio_resampler<T, format, sample_rate, channels>::read(int samples)
{
    assert(swr);
    assert(frame_buf);

    if (samples > current_alloc)
        grow(samples);

    // we're not actually reading from frame_buf and writing to frame_buf, if we give nullptr for
    // input with 0 in_count, the resampler will flush
    auto frame_count =
        swr_convert(swr, &frame_buf, samples, const_cast<const uint8_t **>(&frame_buf), 0);
    if (frame_count < 0)
        std::cerr << "[audio resampler] error reading output\n";

    return {reinterpret_cast<T *>(frame_buf), frame_count};
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
int audio_resampler<T, format, sample_rate, channels>::delayed_samples()
{
    assert(swr);
    return swr_get_delay(swr, sample_rate);
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
simple_audio_decoder<T, format, sample_rate, channels>::simple_audio_decoder()
    : avio{input_buffer}, state{decoder_state::start}
{
    static_assert(sample_rate > 0);
    static_assert(channels > 0);
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
void simple_audio_decoder<T, format, sample_rate, channels>::feed(const uint8_t *data, size_t bytes)
{
    if (data && bytes > 0) {
        input_buffer.insert(input_buffer.end(), data, data + bytes);
    }
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
int simple_audio_decoder<T, format, sample_rate, channels>::read(T *data, int samples)
{
    assert(data);
    assert(samples > 0);

    if (state != decoder_state::eof) {
        auto avf = decoder.next_frame();
        if (avf.data) {
            resampler->feed(&avf);
        }
        if (avf.eof) {
            state = decoder_state::eof;
            resampler->feed(nullptr);
        }
    }

    auto audio = resampler->read(samples);
    if (audio.frame_count > 0) {
        auto start = audio.data;
        auto end = audio.data + audio.frame_count * channels;
        std::copy(start, end, data);

        if (state != decoder_state::eof && audio.frame_count < samples) {
            // try to read the remaining samples
            return audio.frame_count +
                   read(data + audio.frame_count * channels, samples - audio.frame_count);
        }
    }

    if (audio.frame_count <= 0 && state == decoder_state::eof) {
        // decoder gave eof and resampler isn't giving more data... completely done
        input_buffer.clear();
    }
    return audio.frame_count;
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
int simple_audio_decoder<T, format, sample_rate, channels>::available()
{
    if (ready())
        return resampler->delayed_samples();
    return 0;
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
bool simple_audio_decoder<T, format, sample_rate, channels>::ready()
{
    return state == decoder_state::ready;
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
bool simple_audio_decoder<T, format, sample_rate, channels>::done()
{
    return state == decoder_state::eof;
}

template<typename T, AVSampleFormat format, int sample_rate, int channels>
void simple_audio_decoder<T, format, sample_rate, channels>::check_stream()
{
    try {
        switch (state) {
            // yes, fall through
            case decoder_state::start:
                decoder.open_input(avio);
                state = decoder_state::opened_input;
            case decoder_state::opened_input:
                decoder.find_stream_info();
                state = decoder_state::found_stream_info;
            case decoder_state::found_stream_info:
                decoder.find_best_stream();
                state = decoder_state::found_best_stream;
            case decoder_state::found_best_stream:
                decoder.open_decoder();
                state = decoder_state::opened_decoder;
            case decoder_state::opened_decoder:
                resampler = std::make_unique<resampler_type>(decoder);
                state = decoder_state::ready;
            default:
                break;
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
    }
}

// explicit instantiation
template class simple_audio_decoder<float, AV_SAMPLE_FMT_FLT, 48000, 2>;
template class simple_audio_decoder<int16_t, AV_SAMPLE_FMT_S16, 48000, 2>;
template class audio_resampler<float, AV_SAMPLE_FMT_FLT, 48000, 2>;
template class audio_resampler<int16_t, AV_SAMPLE_FMT_S16, 48000, 2>;
