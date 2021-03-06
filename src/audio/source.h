#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <cstdint>
#include <vector>

#include "audio/decoding.h"
#include "audio/opus_encoder.h"

struct opus_frame {
    std::vector<uint8_t> data;
    int frame_count;
    bool end_of_source;
};

opus_frame next_frame(float_audio_decoder &decoder, discord::opus_encoder &encoder, uint8_t *buffer,
                      size_t buf_size);

struct audio_source {
    virtual ~audio_source() = default;
    virtual opus_frame next() = 0;

    // The audio source might need some preparation that can't be done in the constructor.
    // E.g. youtube_dl_source needs to create a child process and begin reading from async_pipe,
    // but it cannot retrieve a weak_ptr to itself until after the constructor has finished.
    virtual void prepare() = 0;
};

#endif
