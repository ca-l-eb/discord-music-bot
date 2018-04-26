#ifndef CMD_AUDIO_SOURCE_H
#define CMD_AUDIO_SOURCE_H

#include <cstdint>
#include <vector>

struct audio_frame {
    std::vector<uint8_t> opus_encoded_data;
    int frame_count;
};

struct audio_source {
    virtual ~audio_source() = default;
    virtual audio_frame next() = 0;

    // The audio source might need some preparation that can't be done in the constructor.
    // E.g. youtube_dl_source needs to create a child process and begin reading from async_pipe,
    // but it cannot retrieve a weak_ptr to itself until after the constructor has finished.
    virtual void prepare() = 0;
};

#endif
