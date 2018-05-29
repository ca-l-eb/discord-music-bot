#ifndef CMD_AUDIO_SOURCE_YOUTUBE_DL_H
#define CMD_AUDIO_SOURCE_YOUTUBE_DL_H

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/process/async_pipe.hpp>
#include <boost/process/child.hpp>
#include <memory>

#include "audio/decoding.h"
#include "audio/opus_encoder.h"
#include "audio/source.h"
#include "callbacks.h"

class youtube_dl_source : public audio_source,
                          public std::enable_shared_from_this<youtube_dl_source>
{
public:
    youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                      const std::string &url, error_cb c);
    virtual ~youtube_dl_source() = default;
    virtual opus_frame next();
    virtual void prepare();

private:
    boost::asio::io_context &ctx;
    discord::opus_encoder &encoder;
    boost::process::child child;
    boost::process::async_pipe pipe;

    // Holds the entire contents of an audio file in some format
    std::vector<uint8_t> audio_file_data;
    std::array<uint8_t, 4096> buffer;

    std::unique_ptr<avio_info> avio;
    std::unique_ptr<audio_decoder> decoder;
    std::unique_ptr<audio_resampler> resampler;

    error_cb callback;
    const std::string &url;
    bool notified;

    enum class decoder_state {
        start,
        opened_input,
        found_stream_info,
        found_best_stream,
        opened_decoder,
        ready
    } state;

    void make_process(const std::string &url);
    void read_from_pipe(const boost::system::error_code &e, size_t transferred);
    void try_stream();
};

#endif
