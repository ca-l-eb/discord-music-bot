#ifndef CMD_DISCORD_OPUS_ENCODER_H
#define CMD_DISCORD_OPUS_ENCODER_H

#include <cstdint>

#include <opus/opus.h>

namespace discord
{
class opus_encoder
{
public:
    opus_encoder(int channels, int sample_rate);
    ~opus_encoder();

    int32_t encode(const int16_t *src, int frame_size, unsigned char *dest, int dest_size);
    int32_t encode(const float *src, int frame_size, unsigned char *dest, int dest_size);
    void set_bitrate(int bitrate);

private:
    OpusEncoder *encoder;
};
}  // namespace discord

#endif
