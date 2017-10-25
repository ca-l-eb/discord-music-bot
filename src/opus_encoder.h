#ifndef CMD_DISCORD_OPUS_ENCODER_H
#define CMD_DISCORD_OPUS_ENCODER_H

#include <opus/opus.h>
#include <cstdint>
#include <cstddef>

namespace cmd
{
namespace discord
{
enum class audio_format { F32, S32, S16, S8, U32, U16, U8 };

class opus_encoder
{
public:
    opus_encoder(int channels, int sample_rate);
    ~opus_encoder();

    int32_t encode(const int16_t *src, int frame_size, unsigned char *dest, int dest_size);
    int32_t encode_float(const float *src, int frame_size, unsigned char *dest, int dest_size);

private:
    OpusEncoder *encoder;
};
}
}

#endif
