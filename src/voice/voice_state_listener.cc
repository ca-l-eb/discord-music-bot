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
    // We're looking for voice state update for this user_id
    auto user = data["user_id"];
    if (!(user.is_string() && user == gateway.get_user_id()))
        return;

    auto guild = data["guild_id"];
    if (!guild.is_string())
        return;

    std::string guild_id = guild.get<std::string>();

    // Create the entry if there is not one for this guild already
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        voice_gateways[guild_id] = {};

    // Get a reference to the entry for this guild
    auto &vg = voice_gateways[guild_id];

    // Fill out the necessary fields for joining a voice channel (guild_id, session_id, and
    // channel_id)
    vg.guild_id = guild_id;

    auto session = data["session_id"];
    if (session.is_string())
        vg.session_id = session.get<std::string>();

    auto channel = data["channel_id"];
    if (channel.is_string())
        vg.channel_id = channel.get<std::string>();
}

void discord::voice_state_listener::voice_server_update(const nlohmann::json &data)
{
    auto guild = data["guild_id"];
    if (!guild.is_string())
        return;
    std::string guild_id = guild.get<std::string>();

    // Lookup the voice_gateway_entry from the map, if it doesn't exist, or doesn't contain a
    // session_id and channel_id, don't continue
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;
    if (entry.session_id.empty() || entry.channel_id.empty())
        return;

    entry.token = data.at("token").get<std::string>();
    entry.endpoint = data.at("endpoint").get<std::string>();

    auto gateway_connect_cb = [&](const boost::system::error_code &e) {
        if (e) {
            std::cerr << "[voice state] voice gateway connection error: " << e.message() << "\n";
        } else {
            std::cout << "[voice state] connected to voice gateway. Ready to send audio\n";
            entry.state = voice_gateway_entry::state::connected;
        }
    };

    // We got all the information needed to join a voice gateway now
    entry.gateway = std::make_unique<discord::voice_gateway>(ctx, entry, gateway.get_user_id());
    entry.gateway->connect(gateway.resolver, gateway_connect_cb);
}

// Listen for guild text messages indicating to join, leave, play, pause, etc.
void discord::voice_state_listener::message_create(const nlohmann::json &data)
{
    // If this isn't a guild text, ignore the message
    auto msg_type = data["type"];
    if (!msg_type.is_number())
        return;
    if (msg_type.get<int>() != static_cast<int>(discord::channel::channel_type::guild_text))
        return;

    // Otherwise lookup the message contents and check if it is a command to change the voice
    // state
    auto msg = data["content"];
    if (!msg.is_string())
        return;
    std::string msg_str = msg.get<std::string>();
    if (msg_str.empty())
        return;

    if (msg_str[0] == ':')
        check_command(msg_str, data);
}

void discord::voice_state_listener::check_command(const std::string &content,
                                                  const nlohmann::json &json)
{
    static std::regex command_re{R"(^:(\S+)(?:\s+(.+))?$)"};
    std::smatch matcher;
    std::regex_search(content, matcher, command_re);

    if (matcher.empty())
        return;

    std::string command = matcher.str(1);
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);
    std::string params = matcher.str(2);

    // TODO: consider making this a lookup table setup in constructor
    if (command == "join")
        do_join(params, json);
    else if (command == "leave")
        do_leave(json);
    else if (command == "list" || command == "l")
        do_list(json);
    else if (command == "add" || command == "a")
        do_add(params, json);
    else if (command == "skip" || command == "next")
        do_skip(json);
    else if (command == "play")
        do_play(json);
    else if (command == "pause")
        do_pause(json);
}

void discord::voice_state_listener::do_join(const std::string &params, const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    // Look up what guild this channel is in
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;  // We couldn't find the guild for this channel

    auto guild = store.get_guild(guild_id);
    if (guild.id != guild_id)
        return;

    auto it = voice_gateways.find(guild_id);
    bool connected = it == voice_gateways.end();

    // Look through the guild's channels for channel <params>, if it exists, join, else fail
    // silently
    for (auto &channel : guild.channels) {
        if (channel.type == discord::channel::channel_type::guild_voice && channel.name == params) {
            // Found a matching voice channel name! Join it if it is different than the currently
            // connected channel (if any)
            if (!connected || it->second.channel_id != channel.id) {
                join_voice_server(guild.id, channel.id.c_str());
                return;
            }
        }
    }
}

void discord::voice_state_listener::do_leave(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    // Look up what guild this channel is in
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;  // We couldn't find the guild for this channel

    // If we are currently connected to a voice channel in this guild, disconnect
    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;
    if (entry.state != voice_gateway_entry::state::disconnected) {
        entry.state = voice_gateway_entry::state::disconnected;
        entry.music_queue.clear();

        if (entry.process) {
            stop_process(*entry.process);
        }
        leave_voice_server(guild_id);
    }
}

void discord::voice_state_listener::do_list(const nlohmann::json &) {}

void discord::voice_state_listener::do_add(const std::string &params, const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;
    entry.music_queue.push_back(params);
    if (entry.state == voice_gateway_entry::state::connected) {
        do_play(json);
    }
}

void discord::voice_state_listener::do_skip(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;
    if (entry.state == voice_gateway_entry::state::playing ||
        entry.state == voice_gateway_entry::state::paused) {
        entry.state = voice_gateway_entry::state::connected;
        if (entry.process) {
            stop_process(*entry.process);
        }
        if (!entry.music_queue.empty())
            do_play(json);
    }
}

void discord::voice_state_listener::do_play(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;

    // Create a music process if there is not already one for this guild
    if (!entry.process)
        entry.process = std::make_unique<discord::music_process>(ctx);

    if (!entry.music_queue.empty() || entry.state == voice_gateway_entry::state::paused) {
        play(entry);
    }
}

void discord::voice_state_listener::do_pause(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    auto &entry = it->second;
    if (entry.state == voice_gateway_entry::state::playing)
        entry.state = voice_gateway_entry::state::paused;
}

// Join and leave can't be refactored because the nlohmann::json converts char*s
// into strings before serializing the json, I guess. Doesn't like using nullptrs with char*
void discord::voice_state_listener::join_voice_server(const std::string &guild_id,
                                                      const std::string &channel_id)
{
    // After sending this message, we expect back a VOICE_STATE_UPDATE event
    nlohmann::json json{{"op", static_cast<int>(gateway_op::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", channel_id},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), print_transfer_info);
}

void discord::voice_state_listener::leave_voice_server(const std::string &guild_id)
{
    nlohmann::json json{{"op", static_cast<int>(gateway_op::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", nullptr},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), print_transfer_info);
}

void discord::voice_state_listener::play(voice_gateway_entry &entry)
{
    // Connected state meaning ready to send audio, but nothing loaded at the moment
    if (entry.state == voice_gateway_entry::state::connected) {
        make_audio_process(entry);
    } else if (entry.state == voice_gateway_entry::state::paused) {
        // Resume
        entry.state = voice_gateway_entry::state::playing;
    }
    if (entry.state == voice_gateway_entry::state::playing) {
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
        entry.state = voice_gateway_entry::state::playing;

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
    // Fetch the next frame from the decoder
    auto &decoder = entry.process->decoder;
    auto *frame = decoder->next_frame();

    if (frame && entry.state == voice_gateway_entry::state::playing) {
        // Next timer expires after frame_size / 48000 seconds, or frame_size / 48 ms
        entry.process->timer.expires_from_now(
            boost::posix_time::milliseconds(frame->nb_samples / 48));

        int frame_count = 0;
        auto resampled =
            reinterpret_cast<int16_t *>(entry.process->resampler->resample(frame, frame_count));

        auto encoded_len = encode_audio(*entry.process, resampled, frame_count);

        // Play the frame
        entry.gateway->play(entry.process->buffer.data(), encoded_len, frame_count);

        auto timer_done_cb = [&](auto &ec) {
            if (!ec && entry.state == voice_gateway_entry::state::playing) {
                send_audio(entry);
            }
        };
        // Enqueue wait for the frame to send
        entry.process->timer.async_wait(timer_done_cb);
    } else if (entry.state == voice_gateway_entry::state::playing) {
        // If we are still in the playing state and there are no more frames to read,
        // play the next entry
        std::cout << "[voice state] sound clip finished\n";
        entry.state = voice_gateway_entry::state::connected;
        play(entry);
    } else {
        std::cout << "[voice state] not in playing state\n";
    }
}
