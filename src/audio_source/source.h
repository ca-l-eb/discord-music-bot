#ifndef CMD_AUDIO_SOURCE_H
#define CMD_AUDIO_SOURCE_H

#include <vector>

#include <callbacks.h>

struct audio_frame {
    std::vector<uint8_t> opus_encoded_data;
    int frame_count;
};

struct audio_source {
    virtual audio_frame next() = 0;
};

#endif
