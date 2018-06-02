#include <stdexcept>

#include "audio/opus_encoder.h"

discord::opus_encoder::opus_encoder(int channels, int sample_rate)
{
    int error = 0;
    encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_AUDIO, &error);
    if (error)
        throw std::runtime_error("Could not create opus encoder");

    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
}

discord::opus_encoder::~opus_encoder()
{
    if (encoder)
        opus_encoder_destroy(encoder);
}

int32_t discord::opus_encoder::encode(const int16_t *src, int frame_size, unsigned char *dest,
                                      int dest_size)
{
    return opus_encode(encoder, src, frame_size, dest, dest_size);
}

int32_t discord::opus_encoder::encode(const float *src, int frame_size, unsigned char *dest,
                                      int dest_size)
{
    return opus_encode_float(encoder, src, frame_size, dest, dest_size);
}

void discord::opus_encoder::set_bitrate(int bitrate)
{
    if (bitrate < 8000)
        bitrate = 8000;
    if (bitrate > 128000)
        bitrate = 128000;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
}
