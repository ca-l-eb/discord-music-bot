#ifndef AUDIO_FILE_SOURCE_H
#define AUDIO_FILE_SOURCE_H

#include <boost/asio/io_context.hpp>
#include <string>

#include "audio/decoding.h"
#include "audio/opus_encoder.h"
#include "audio/source.h"
#include "callbacks.h"
#include "discord.h"
#include "voice/voice_connector.h"

class file_source : public audio_source
{
public:
    file_source(discord::voice_context &voice_context, const std::string &file_path);
    virtual ~file_source() = default;
    virtual opus_frame next();
    virtual void prepare();

private:
    discord::voice_context &voice_context;
    const std::string &file_path;

    float_audio_decoder decoder;
    std::array<uint8_t, 8192> buffer;
};

#endif
