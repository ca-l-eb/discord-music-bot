#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#include "audio/source.h"

opus_frame next_frame(float_audio_decoder &decoder, discord::opus_encoder &encoder, uint8_t *buffer,
                      size_t buf_size)
{
    const auto channels = 2;
    const auto frames_wanted = 960;
    auto frame = opus_frame{};

    if (buf_size < frames_wanted * channels)
        throw std::runtime_error{"buffer is too small to read " + std::to_string(frames_wanted) +
                                 " samples"};

    auto float_buf = reinterpret_cast<float *>(buffer);
    auto read = decoder.read(float_buf, frames_wanted);
    if (read < frames_wanted) {
        if (!decoder.done())
            return {};

        frame.end_of_source = true;

        // Want to clear the remaining frames to 0
        auto start = buffer + static_cast<size_t>(read) * channels;
        auto end = buffer + frames_wanted * channels;
        std::fill(start, end, 0.0f);
    }
    if (read > 0) {
        auto buf = std::array<uint8_t, 512>{};
        auto encoded_len = encoder.encode(float_buf, frames_wanted, buf.data(), static_cast<int>(buf.size()));
        if (encoded_len > 0) {
            frame.data.reserve(encoded_len);
            frame.data.insert(std::begin(frame.data), buf.data(), buf.data() + encoded_len);
        }
    }
    frame.frame_count = frames_wanted;
    return frame;
}
