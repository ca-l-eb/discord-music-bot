#ifndef AUDIO_FILE_SOURCE_H
#define AUDIO_FILE_SOURCE_H

#include <boost/asio/io_context.hpp>
#include <string>

#include "audio/decoding.h"
#include "audio/opus_encoder.h"
#include "audio/source.h"
#include "callbacks.h"
#include "discord.h"

class file_source : public audio_source
{
public:
    file_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                const std::string &file_path, error_cb c);
    virtual ~file_source() = default;
    virtual opus_frame next();
    virtual void prepare();

private:
    boost::asio::io_context &ctx;
    discord::opus_encoder &encoder;
    const std::string &file_path;
    error_cb callback;

    float_audio_decoder decoder;
    std::array<uint8_t, 8192> buffer;
};

#endif
