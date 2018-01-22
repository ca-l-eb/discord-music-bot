#ifndef CMD_AUDIO_SOURCE_H
#define CMD_AUDIO_SOURCE_H

struct audio_frame {
    unsigned char *opus_encoded_data;
    int encoded_len;
    int frame_count;
};

struct audio_source {
    audio_frame next();
};

#endif

