#include <cassert>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "audio/decoding.h"

// Some data has been requested, write the results into buf, return the amount of bytes written
static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    assert(opaque);
    auto bd = reinterpret_cast<buffer_data *>(opaque);

    buf_size = FFMIN((size_t) buf_size, bd->data.size() - bd->loc);
    if (buf_size < 0)
        return 0;
    memcpy(buf, &bd->data[bd->loc], buf_size);
    bd->loc += buf_size;
    return buf_size;
}

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
    , frame{nullptr}
    , stream_index{-1}
    , do_read{true}
    , do_feed{true}
{
    frame = av_frame_alloc();
    if (!frame)
        throw std::runtime_error{"Unable to allocate audio frame"};
}

int audio_decoder::open_input(avio_info &av)
{
    format_context = avformat_alloc_context();
    if (!format_context)
        throw std::runtime_error{"Could not allocate libavformat context"};

    // Use the AVIO context
    format_context->pb = av.avio_context;

    // Open the file, read the header, export information into format_context
    // Frees format_context on failure
    return avformat_open_input(&format_context, nullptr, nullptr, nullptr);
}

int audio_decoder::find_stream_info()
{
    // Some format do not have header, or do not store enough information there so try to read
    // and decode a few frames if necessary to find missing information
    return avformat_find_stream_info(format_context, nullptr);
}

int audio_decoder::find_best_stream()
{
    // Get best audio stream from format_context, and possibly decoder for this stream
    return stream_index =
               av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
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

audio_decoder::~audio_decoder()
{
    av_frame_free(&frame);
    if (decoder_context) {
        avcodec_close(decoder_context);
        avcodec_free_context(&decoder_context);
    }
    if (format_context) {
        avformat_close_input(&format_context);
        avformat_free_context(format_context);
    }
}

int audio_decoder::read()
{
    auto ret = 0;
    av_init_packet(&packet);
    // Grab the next packet for the audio stream we're interested in
    while ((ret = av_read_frame(format_context, &packet)) >= 0) {
        if (packet.stream_index != stream_index) {
            av_packet_unref(&packet);
        } else {
            break;
        }
    }
    return ret;
}

// Feed the decoder the next packet from the demuxer (format_context)
int audio_decoder::feed(bool flush)
{
    auto ret = 0;
    if (flush) {
        ret = avcodec_send_packet(decoder_context, nullptr);
        do_read = false;
    } else {
        // Send the packet to the decoder
        ret = avcodec_send_packet(decoder_context, &packet);
        eof = false;
    }

    if (ret == AVERROR(EAGAIN)) {
        // Decoder denied input. Output must be read
        do_read = false;
        do_feed = false;
    } else if (ret == AVERROR_EOF) {
        // Decoder has been flushed. No new packets can be sent
        do_feed = false;
    } else if (ret == AVERROR(EINVAL)) {
        // Decoder is not opened
        throw std::runtime_error{"Attempted to use an unopened decoder"};
    } else if (ret == AVERROR(ENOMEM)) {
        throw std::runtime_error{"Decoder failed to add packet to internal queue: no memory"};
    } else {
        av_packet_unref(&packet);
        do_feed = true;
    }
    return ret;
}

int audio_decoder::decode()
{
    if (!frame)
        return AVERROR_EOF;

    // Retrieve (decoded) frame from decoder
    auto ret = avcodec_receive_frame(decoder_context, frame);

    if (ret == AVERROR(EAGAIN)) {
        // Error receiving frame from decoder
    } else if (ret == AVERROR_EOF) {
        // Decoder is fully flushed. No more output frames
        eof = true;
    } else if (ret == AVERROR(EINVAL)) {
        throw std::runtime_error{"Attempted to use an unopened decoder"};
    }
    return ret;
}

av_frame audio_decoder::next_frame()
{
    auto ret = 0;
    auto av = av_frame{};
    if (do_read)
        ret = read();
    if (ret || do_feed)
        feed(ret != 0);

    ret = decode();
    if (ret == AVERROR(EAGAIN))
        return next_frame();
    if (ret == AVERROR_EOF && !do_feed && !do_read) {
        if (frame)
            av_frame_free(&frame);
        frame = nullptr;
    }
    av.data = frame;
    av.eof = eof;
    return av;
}

audio_resampler::audio_resampler(audio_decoder &decoder, int sample_rate, int channels,
                                 AVSampleFormat format)
    : frame_buf{nullptr}, current_alloc{256}, format{format}
{
    swr = swr_alloc();
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

    grow();
}

audio_resampler::~audio_resampler()
{
    if (swr)
        swr_free(&swr);
    if (frame_buf)
        av_free(frame_buf);
}

void audio_resampler::grow()
{
    if (frame_buf)
        av_free(frame_buf);

    current_alloc *= 2;
    av_samples_alloc(&frame_buf, nullptr, 2, current_alloc, format, 0);

    if (!frame_buf)
        throw std::runtime_error{"Coult not allocate resample buffer"};

    std::cout << "[audio resampler] buffer grew to hold " << current_alloc << "\n";
}

int audio_resampler::resample(av_frame frame, void **data)
{
    assert(swr);
    auto frame_count = 0;
    if (frame.data) {
        auto swr_output_count = swr_get_out_samples(swr, frame.data->nb_samples);
        if (swr_output_count < 0) {
            *data = nullptr;
            return 0;
        }

        while (swr_output_count > current_alloc) {
            grow();
        }
        frame_count =
            swr_convert(swr, &frame_buf, current_alloc,
                        const_cast<const uint8_t **>(frame.data->data), frame.data->nb_samples);
    } else {
        frame_count = swr_convert(swr, &frame_buf, current_alloc, nullptr, 0);
    }

    *data = frame_buf;
    return frame_count;
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
simple_audio_decoder<T, sample_rate, channels, format>::simple_audio_decoder()
    : output_buffer(16384)
    , avio{std::make_unique<avio_info>(input_buffer)}
    , decoder{std::make_unique<audio_decoder>()}
    , state{decoder_state::start}
{
    static_assert(sample_rate > 0);
    static_assert(channels > 0);
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
void simple_audio_decoder<T, sample_rate, channels, format>::feed(const void *data, size_t bytes)
{
    auto source = reinterpret_cast<const uint8_t *>(data);
    if (data && bytes > 0) {
        input_buffer.insert(input_buffer.end(), source, source + bytes);
        if (state != decoder_state::ready)
            try_stream();
    }
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
int simple_audio_decoder<T, sample_rate, channels, format>::read(T *data, int samples)
{
    auto resampled = static_cast<T *>(nullptr);
    auto frame_count = 0;
    if (state == decoder_state::eof) {
        frame_count = resampler->resample({}, reinterpret_cast<void **>(&resampled));
    }
    auto avf = decoder->next_frame();
    if (data && samples > 0 && avf.data) {
        frame_count = resampler->resample(avf, reinterpret_cast<void **>(&resampled));
    }
    if (frame_count > 0) {
        output_buffer.insert(output_buffer.end(), resampled, resampled + frame_count * channels);
    }
    if (output_buffer.size() >= static_cast<size_t>(samples * channels)) {
        std::copy(output_buffer.begin(), output_buffer.begin() + samples * channels, data);
        output_buffer.erase(output_buffer.begin(), output_buffer.begin() + samples * channels);
        return samples;
    }
    if (avf.eof) {
        input_buffer.clear();
        state = decoder_state::eof;
        return 0;
    }
    return read(data, samples);
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
int simple_audio_decoder<T, sample_rate, channels, format>::available()
{
    if (ready())
        return output_buffer.size();
    return -1;
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
bool simple_audio_decoder<T, sample_rate, channels, format>::ready()
{
    return state == decoder_state::ready;
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
bool simple_audio_decoder<T, sample_rate, channels, format>::done()
{
    return state == decoder_state::eof;
}

template<typename T, int sample_rate, int channels, AVSampleFormat format>
void simple_audio_decoder<T, sample_rate, channels, format>::try_stream()
{
    try {
        switch (state) {
            // yes, fall through
            case decoder_state::start:
                if (decoder->open_input(*avio) >= 0) {
                    state = decoder_state::opened_input;
                }
            case decoder_state::opened_input:
                if (decoder->find_stream_info() >= 0) {
                    state = decoder_state::found_stream_info;
                }
            case decoder_state::found_stream_info:
                if (decoder->find_best_stream() >= 0) {
                    state = decoder_state::found_best_stream;
                }
            case decoder_state::found_best_stream:
                decoder->open_decoder();
                state = decoder_state::opened_decoder;
            case decoder_state::opened_decoder:
                resampler =
                    std::make_unique<audio_resampler>(*decoder, sample_rate, channels, format);
                state = decoder_state::ready;
            default:
                break;
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
    }
}

// explicit instantiation
template class simple_audio_decoder<float, 48000, 2, AV_SAMPLE_FMT_FLT>;
