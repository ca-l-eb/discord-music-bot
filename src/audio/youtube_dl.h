#ifndef AUDIO_SOURCE_YOUTUBE_DL_H
#define AUDIO_SOURCE_YOUTUBE_DL_H

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/process/async_pipe.hpp>
#include <boost/process/child.hpp>
#include <memory>

#include "audio/decoding.h"
#include "audio/opus_encoder.h"
#include "audio/source.h"
#include "callbacks.h"
#include "voice/voice_connector.h"

class youtube_dl_source : public audio_source,
                          public std::enable_shared_from_this<youtube_dl_source>
{
public:
    youtube_dl_source(discord::voice_context &voice_context, const std::string &url);
    virtual ~youtube_dl_source() = default;
    virtual opus_frame next();
    virtual void prepare();

private:
    discord::voice_context &voice_context;
    boost::process::child child;
    boost::process::async_pipe pipe;

    float_audio_decoder decoder;
    std::array<uint8_t, 8192> buffer;
    int bytes_sent_to_decoder;

    const std::string &url;
    bool notified;

    void make_process(const std::string &url);
    void read_from_pipe(const boost::system::error_code &e, size_t transferred);
};

#endif
