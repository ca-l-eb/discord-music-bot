#include <algorithm>
#include <boost/process.hpp>
#include <iostream>
#include <regex>

#include <net/resource_parser.h>
#include <voice/voice_state_listener.h>

static void stop_process(discord::music_process &process)
{
    process.timer.cancel();
    process.pipe.close();
    process.youtube_dl.terminate();
}

static discord::guild *get_guild_from_channel(uint64_t channel_id, discord::gateway_store &store)
{
    auto guild_id = store.lookup_channel(channel_id);
    if (!guild_id)
        return nullptr;

    return store.get_guild(guild_id);
}

discord::voice_state_listener::voice_state_listener(boost::asio::io_context &ctx,
                                                    discord::gateway &gateway,
                                                    discord::gateway_store &store)
    : ctx{ctx}, gateway{gateway}, store{store}
{
}

discord::voice_state_listener::~voice_state_listener() {}

void discord::voice_state_listener::handle(discord::gateway &, gateway_op,
                                           const nlohmann::json &data, const std::string &type)
{
    if (type == "VOICE_STATE_UPDATE") {
        voice_state_update(data);
    } else if (type == "VOICE_SERVER_UPDATE") {
        voice_server_update(data);
    } else if (type == "MESSAGE_CREATE") {
        message_create(data);
    }
}

void discord::voice_state_listener::voice_state_update(const nlohmann::json &data)
{
    auto state = data.get<discord::voice_state>();
    assert(state.guild_id);

    // We're looking for voice state update for this user_id
    if (gateway.get_user_id() != state.user_id) {
        return;
    }

    // Create the entry if it doesn't exist
    auto it = voice_gateways.find(state.guild_id);
    if (it == voice_gateways.end()) {
        voice_gateways[state.guild_id] = {};
    }
    auto &entry = voice_gateways[state.guild_id];
    entry.channel_id = state.channel_id;
    entry.guild_id = state.guild_id;
    entry.session_id = std::move(state.session_id);
}

void discord::voice_state_listener::voice_server_update(const nlohmann::json &data)
{
    auto vsu = data.get<discord::event::voice_server_update>();

    auto it = voice_gateways.find(vsu.guild_id);
    if (it == voice_gateways.end()) {
        return;
    }

    auto &entry = it->second;
    assert(vsu.guild_id == entry.guild_id);
    entry.token = std::move(vsu.token);
    entry.endpoint = std::move(vsu.endpoint);

    auto gateway_connect_cb = [&](const boost::system::error_code &e) {
        if (e) {
            std::cerr << "[voice state] voice gateway connection error: " << e.message() << "\n";
        } else {
            std::cout << "[voice state] connected to voice gateway. Ready to send audio\n";
            entry.p_state = voice_gateway_entry::state::connected;
        }
    };

    // We got all the information needed to join a voice gateway now
    entry.gateway = std::make_unique<discord::voice_gateway>(ctx, entry, gateway.get_user_id());
    entry.gateway->connect(gateway.resolver, gateway_connect_cb);
}

// Listen for guild text messages indicating to join, leave, play, pause, etc.
void discord::voice_state_listener::message_create(const nlohmann::json &data)
{
    auto msg = data.get<discord::message>();
    if (msg.type != discord::message::message_type::default_)
        return;

    if (msg.content.empty())
        return;

    if (msg.content[0] == ':')
        check_command(msg);
}

void discord::voice_state_listener::check_command(const discord::message &m)
{
    static std::regex command_re{R"(^:(\S+)(?:\s+(.+))?$)"};
    std::smatch matcher;
    std::regex_search(m.content, matcher, command_re);

    if (matcher.empty())
        return;

    std::string command = matcher.str(1);
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);
    std::string params = matcher.str(2);

    auto guild_id = store.lookup_channel(m.channel_id);
    auto it = voice_gateways.find(guild_id);

    // TODO: consider making this a lookup table setup in constructor
    if (command == "join") {
        do_join(m, params);
    } else if (it != voice_gateways.end()) {
        auto &entry = it->second;
        if (command == "leave")
            do_leave(entry);
        else if (command == "list" || command == "l")
            do_list(entry);
        else if (command == "add" || command == "a")
            do_add(entry, params);
        else if (command == "skip" || command == "next")
            do_skip(entry);
        else if (command == "play")
            do_play(entry);
        else if (command == "pause")
            do_pause(entry);
    }
}

void discord::voice_state_listener::do_join(const discord::message &m, const std::string &s)
{
    auto *guild = get_guild_from_channel(m.channel_id, store);

    auto it = voice_gateways.find(guild->id);
    bool connected = it == voice_gateways.end();

    // Look through the guild's channels for channel s, if it exists, join, else fail
    // silently
    for (auto channel : guild->channels) {
        if (channel.type == discord::channel::channel_type::guild_voice && channel.name == s) {
            // Found a matching voice channel name! Join it if it is different than the currently
            // connected channel (if any)
            if (!connected || it->second.channel_id != channel.id) {
                join_voice_server(guild->id, channel.id);
                return;
            }
        }
    }
}

void discord::voice_state_listener::do_leave(discord::voice_gateway_entry &entry)
{
    if (entry.p_state != voice_gateway_entry::state::disconnected) {
        entry.p_state = voice_gateway_entry::state::disconnected;
        entry.music_queue.clear();

        if (entry.process) {
            stop_process(*entry.process);
        }
        leave_voice_server(entry.guild_id);
    }
}

void discord::voice_state_listener::do_list(discord::voice_gateway_entry &) {}

void discord::voice_state_listener::do_add(discord::voice_gateway_entry &entry,
                                           const std::string &params)
{
    entry.music_queue.push_back(params);
    if (entry.p_state == voice_gateway_entry::state::connected) {
        do_play(entry);
    }
}

void discord::voice_state_listener::do_skip(discord::voice_gateway_entry &entry)
{
    if (entry.p_state == voice_gateway_entry::state::playing ||
        entry.p_state == voice_gateway_entry::state::paused) {
        entry.p_state = voice_gateway_entry::state::connected;
        if (entry.process) {
            stop_process(*entry.process);
        }
        if (!entry.music_queue.empty())
            do_play(entry);
        else
            entry.gateway->stop();
    }
}

void discord::voice_state_listener::do_play(discord::voice_gateway_entry &entry)
{
    // Create a music process if there is not already one for this guild
    if (!entry.process)
        entry.process = std::make_unique<discord::music_process>(ctx);

    if (!entry.music_queue.empty() || entry.p_state == voice_gateway_entry::state::paused) {
        play(entry);
    }
}

void discord::voice_state_listener::do_pause(discord::voice_gateway_entry &entry)
{
    if (entry.p_state == voice_gateway_entry::state::playing) {
        entry.p_state = voice_gateway_entry::state::paused;
        entry.gateway->stop();
    }
}

void discord::voice_state_listener::join_voice_server(uint64_t guild_id, uint64_t channel_id)
{
    std::string guild_str = std::to_string(guild_id);
    std::string channel_str = std::to_string(channel_id);
    const char *channel_ptr = channel_id == 0 ? nullptr : channel_str.c_str();

    // After join, we expect back a VOICE_STATE_UPDATE event
    nlohmann::json json{{"op", static_cast<int>(gateway_op::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_str},
                          {"channel_id", channel_ptr},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), print_transfer_info);
}

void discord::voice_state_listener::leave_voice_server(uint64_t guild_id)
{
    join_voice_server(guild_id, 0);
}

void discord::voice_state_listener::play(voice_gateway_entry &entry)
{
    // Connected state meaning ready to send audio, but nothing loaded at the moment
    if (entry.p_state == voice_gateway_entry::state::connected) {
        make_audio_process(entry);
    } else if (entry.p_state == voice_gateway_entry::state::paused) {
        // Resume
        entry.p_state = voice_gateway_entry::state::playing;
    }
    if (entry.p_state == voice_gateway_entry::state::playing) {
        send_audio(entry);
    }
}

void discord::voice_state_listener::make_audio_process(voice_gateway_entry &entry)
{
    namespace bp = boost::process;
    // Take song off music queue and create new youtube_dl process
    if (entry.music_queue.empty())
        return;

    // Get the next song from the queue, to be sent to youtube-dl for downloading
    std::string next = entry.music_queue.front();
    entry.music_queue.pop_front();

    // Assumes previous pipes have been reset, and proceses already waited on

    // Formats at https://github.com/rg3/youtube-dl/blob/master/youtube_dl/extractor/youtube.py
    // Prefer opus, vorbis, aac
    entry.process->youtube_dl = bp::child{
        "youtube-dl -r 1000000 -f 251/250/249/172/171/141/140/139/256/258/325/328/13 -o - " + next,
        bp::std_in<bp::null, bp::std_err> bp::null, bp::std_out > entry.process->pipe};

    std::cout << "[voice state] created youtube_dl processes for " << next << "\n";

    read_from_pipe({}, 0, entry);
}

void discord::voice_state_listener::read_from_pipe(const boost::system::error_code &e,
                                                   size_t transferred, voice_gateway_entry &entry)
{
    if (transferred > 0) {
        // Commit any transferred data to the entry.process->audio_file_data vector
        auto &vec = entry.process->audio_file_data;
        auto &buf = entry.process->buffer;
        vec.insert(vec.end(), buf.begin(), buf.begin() + transferred);
    }
    if (!e) {
        auto pipe_read_cb = [&](auto &ec, size_t transferred) {
            read_from_pipe(ec, transferred, entry);
        };

        // Read from the pipe and fill up the audio_file_data vector
        boost::asio::async_read(entry.process->pipe, boost::asio::buffer(entry.process->buffer),
                                pipe_read_cb);

    } else if (e == boost::asio::error::eof) {
        std::cout << "[voice state] got eof from async_pipe\n";
        // Create avio structure (holds audio data), decoder (demuxing and decoding), and
        // resampler
        entry.process->avio = std::make_unique<avio_info>(entry.process->audio_file_data);
        entry.process->decoder = std::make_unique<audio_decoder>(*entry.process->avio);
        entry.process->resampler =
            std::make_unique<audio_resampler>(*entry.process->decoder, 48000, 2, AV_SAMPLE_FMT_S16);

        // Audio is ready to be consumed, via decoder.next_frame()
        // or via decoder.read() and using decoder.packet to retrieve encoded frame

        // Close the pipe, allow the child to terminate
        entry.process->pipe.close();
        entry.process->youtube_dl.wait();

        // We are now ready to play
        entry.p_state = voice_gateway_entry::state::playing;

        // Begin sending the audio!
        send_audio(entry);
    } else {
        std::cerr << "Pipe read error: " << e.message() << "\n";
    }
}

int discord::voice_state_listener::encode_audio(music_process &m, int16_t *pcm, int frame_count)
{
    return m.encoder.encode(pcm, frame_count, m.buffer.data(), m.buffer.size());
}

void discord::voice_state_listener::send_audio(voice_gateway_entry &entry)
{
    entry.process->timer.expires_from_now(boost::posix_time::milliseconds(20));
    // Fetch the next frame from the decoder
    auto &decoder = entry.process->decoder;
    auto *frame = decoder->next_frame();

    if (frame && entry.p_state == voice_gateway_entry::state::playing) {
        // Next timer expires after frame_size / 48000 seconds, or frame_size / 48 ms
        // entry.process->timer.expires_from_now(
        //    boost::posix_time::milliseconds(frame->nb_samples / 48));

        int frame_count = 0;
        auto resampled =
            reinterpret_cast<int16_t *>(entry.process->resampler->resample(frame, frame_count));

        auto encoded_len = encode_audio(*entry.process, resampled, frame_count);

        // Play the frame
        // entry.gateway->play(entry.process->buffer.data(), encoded_len, frame_count);

        auto timer_done_cb = [&](auto &ec) {
            if (!ec && entry.p_state == voice_gateway_entry::state::playing) {
                send_audio(entry);
            }
        };
        // Enqueue wait for the frame to send
        entry.process->timer.async_wait(timer_done_cb);
    } else if (entry.p_state == voice_gateway_entry::state::playing) {
        // If we are still in the playing state and there are no more frames to read,
        // play the next entry
        std::cout << "[voice state] sound clip finished\n";
        entry.p_state = voice_gateway_entry::state::connected;
        play(entry);
    } else {
        std::cout << "[voice state] not in playing state\n";
    }
}
