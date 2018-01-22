#ifndef CMD_AUDIO_SOURCE_YOUTUBE_DL_H
#define CMD_AUDIO_SOURCE_YOUTUBE_DL_H

#include <array>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <memory>

#include <audio_source/source.h>
#include <voice/decoding.h>
#include <voice/opus_encoder.h>

class youtube_dl_source : public audio_source
{
public:
    youtube_dl_source(boost::asio::io_context &ctx, discord::opus_encoder &encoder,
                      const std::string &url, error_cb c);
    virtual audio_frame next();

private:
    boost::process::child child;
    boost::process::async_pipe pipe;
    discord::opus_encoder &encoder;

    // Holds the entire contents of an audio file in some format
    std::vector<uint8_t> audio_file_data;
    std::array<uint8_t, 4096> buffer;

    std::unique_ptr<avio_info> avio;
    std::unique_ptr<audio_decoder> decoder;
    std::unique_ptr<audio_resampler> resampler;

    error_cb callback;

    void make_process(boost::asio::io_context &ctx, const std::string url);
    void read_from_pipe(const boost::system::error_code &e, size_t transferred);
};

#endif
