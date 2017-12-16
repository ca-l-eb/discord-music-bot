#include <string_utils.h>
#include <iostream>
#include <regex>

#include <net/resource_parser.h>
#include <voice/voice_state_listener.h>

cmd::discord::voice_state_listener::voice_state_listener(boost::asio::io_service &service,
                                                         cmd::discord::gateway &gateway,
                                                         cmd::discord::gateway_store &store)
    : service{service}, gateway{gateway}, store{store}
{
}

cmd::discord::voice_state_listener::~voice_state_listener() {}

void cmd::discord::voice_state_listener::handle(cmd::discord::gateway &, gtw_op_recv,
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

void cmd::discord::voice_state_listener::voice_state_update(const nlohmann::json &data)
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

    // Get a reference to the entry fot his guild
    auto &vg = voice_gateways[guild_id];
    vg.guild_id = guild_id;

    auto session = data["session_id"];
    if (session.is_string())
        vg.session_id = session.get<std::string>();

    auto channel = data["channel_id"];
    if (channel.is_string())
        vg.channel_id = channel.get<std::string>();
}

void cmd::discord::voice_state_listener::voice_server_update(const nlohmann::json &data)
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

    auto &vg = it->second;
    if (vg.session_id.empty() || vg.channel_id.empty())
        return;

    vg.token = data.at("token").get<std::string>();
    vg.endpoint = data.at("endpoint").get<std::string>();

    // We got all the information needed to join a voice gateway now
    vg.gateway = std::make_unique<cmd::discord::voice_gateway>(service, vg, gateway.get_user_id());
    vg.gateway->connect([&](const boost::system::error_code &e) {
        if (e) {
            std::cerr << "Voice gateway connection error: " << e.message() << "\n";
        } else {
            std::cout << "Connected to voice gateway. Ready to send audio\n";
            vg.state = voice_gateway_entry::gateway_state::connected;
        }
    });
}

void cmd::discord::voice_state_listener::message_create(const nlohmann::json &data)
{
    // If this isn't a guild text, ignore the message
    auto msg_type = data["type"];
    if (!msg_type.is_number())
        return;
    if (msg_type.get<int>() != static_cast<int>(cmd::discord::channel::channel_type::guild_text))
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

void cmd::discord::voice_state_listener::check_command(const std::string &content,
                                                       const nlohmann::json &json)
{
    static std::regex command_re{R"(^:(\S+)(?:\s+(.+))?$)"};
    std::smatch matcher;
    std::regex_search(content, matcher, command_re);

    if (matcher.empty())
        return;

    std::string command = cmd::string_utils::to_lower(matcher.str(1));
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

void cmd::discord::voice_state_listener::do_join(const std::string &params,
                                                 const nlohmann::json &json)
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
        if (channel.type == cmd::discord::channel::channel_type::guild_voice &&
            channel.name == params) {
            // Found a matching voice channel name! Join it if it is different than the currently
            // connected channel (if any)
            if (!connected || it->second.channel_id != channel.id) {
                join_voice_server(guild.id, channel.id.c_str());
                return;
            }
        }
    }
}

void cmd::discord::voice_state_listener::do_leave(const nlohmann::json &json)
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

    if (it->second.state != voice_gateway_entry::gateway_state::disconnected) {
        it->second.state = voice_gateway_entry::gateway_state::disconnected;
        it->second.music_queue.clear();
        
        auto &process = it->second.process;
        if (process) {
            process->timer.cancel();
            process->new_pipes();
            process->kill();
            process->wait();
            process->frames.clear();
            //it->second.process = nullptr;
        }
        leave_voice_server(guild_id);
    }
}

void cmd::discord::voice_state_listener::do_list(const nlohmann::json &)
{
}

void cmd::discord::voice_state_listener::do_add(const std::string &params,
                                                const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    it->second.music_queue.push_back(params);
    if (it->second.state == voice_gateway_entry::gateway_state::connected) {
        do_play(json);
    }
}

void cmd::discord::voice_state_listener::do_skip(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    if (it->second.state == voice_gateway_entry::gateway_state::playing ||
        it->second.state == voice_gateway_entry::gateway_state::paused) {
        it->second.state = voice_gateway_entry::gateway_state::connected;
        auto &process = it->second.process;
        if (process) {
            process->timer.cancel();
            process->new_pipes();
            process->kill();
            process->wait();
            process->frames.clear();
        }
        if (!it->second.music_queue.empty())
            do_play(json);
    }
}

void cmd::discord::voice_state_listener::do_play(const nlohmann::json &json)
{
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    if (!it->second.process)
        it->second.process = std::make_unique<cmd::discord::music_process>(service);

    if (!it->second.music_queue.empty() ||
        it->second.state == voice_gateway_entry::gateway_state::paused) {
        play(it->second);
    }
}

void cmd::discord::voice_state_listener::do_pause(const nlohmann::json &json) {
    auto channel = json["channel_id"];
    std::string guild_id = store.lookup_channel(channel.get<std::string>());
    if (guild_id.empty())
        return;

    auto it = voice_gateways.find(guild_id);
    if (it == voice_gateways.end())
        return;

    if (it->second.state == voice_gateway_entry::gateway_state::playing)
        it->second.state = voice_gateway_entry::gateway_state::paused;
}

// Join and leave can't be refactored because the nlohmann::json converts char*s
// into strings before serializing the json, I guess. Doesn't like using nullptrs with char*
void cmd::discord::voice_state_listener::join_voice_server(const std::string &guild_id,
                                                           const std::string &channel_id)
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", channel_id},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
    gateway.send(json.dump(), [](const boost::system::error_code &e, size_t transferred) {
        if (e) {
            std::cerr << "voice state listener send error: " << e.message() << "\n";
        } else {
            std::cout << "voice state listener sent " << transferred << " bytes\n";
        }
    });
}

void cmd::discord::voice_state_listener::leave_voice_server(const std::string &guild_id)
{
    nlohmann::json json{{"op", static_cast<int>(gtw_op_send::voice_state_update)},
                        {"d",
                         {{"guild_id", guild_id},
                          {"channel_id", nullptr},
                          {"self_mute", false},
                          {"self_deaf", false}}}};
     gateway.send(json.dump(), [](const boost::system::error_code &e, size_t transferred) {
        if (e) {
            std::cerr << "voice state listener send error: " << e.message() << "\n";
        } else {
            std::cout << "voice state listener sent " << transferred << " bytes\n";
        }
    });
}

void cmd::discord::voice_state_listener::play(voice_gateway_entry &entry) 
{
    if (entry.state == voice_gateway_entry::gateway_state::connected) {
        make_audio_process(entry);
    } else if (entry.state == voice_gateway_entry::gateway_state::paused) {
        // Resume
        entry.state = voice_gateway_entry::gateway_state::playing;
    }

    if (entry.state == voice_gateway_entry::gateway_state::playing) {
        send_audio(entry);
    }
}


void cmd::discord::voice_state_listener::make_audio_process(voice_gateway_entry &entry)
{
    // Take song off music queue and create new youtube_dl and ffmpeg process for music
    if (entry.music_queue.empty())
        return;
    std::string next = entry.music_queue.front();
    entry.music_queue.pop_front();

    // Assumes previous pipes have been reset, and proceses already waited on
    
    entry.process->youtube_dl = boost::process::child{"youtube-dl -f 251 -o - " + next,
        boost::process::std_in < boost::process::null,
        boost::process::std_err > boost::process::null,
        boost::process::std_out > entry.process->audio_transport};
    entry.process->ffmpeg = boost::process::child{"ffmpeg -i - -ac 2 -ar 48000 -f s16le -",
        boost::process::std_in < entry.process->audio_transport,
        boost::process::std_out > entry.process->pcm_source,
        boost::process::std_err > boost::process::null};

    std::cout << "created youtube_dl and ffmpeg processes for " << next << "\n";

    // Start asynchronous reads from pipe to begin filling buffer with sound samples
    auto mutable_buffer = entry.process->buffer.prepare(24000 * 2 * sizeof(int16_t));
    boost::asio::async_read(entry.process->pcm_source, mutable_buffer,
                            [&](const boost::system::error_code &e, size_t transferred) {
                                read_from_pipe(e, transferred, entry);
                            });
}

void cmd::discord::voice_state_listener::read_from_pipe(const boost::system::error_code &e,
                                                        size_t transferred,
                                                        voice_gateway_entry &entry)
{
    // std::cout << "Read " << transferred << " bytes, " << e.message() << "\n";
    if (transferred > 0) {
        entry.process->buffer.commit(transferred);
        encode_audio(entry);
    }
    if(!e) {
        // Read more, until we get eof
        auto mutable_buffer = entry.process->buffer.prepare(24000 * 2 * sizeof(int16_t));
        boost::asio::async_read(entry.process->pcm_source, mutable_buffer,
                                [&](const boost::system::error_code &e, size_t transferred) {
                                    read_from_pipe(e, transferred, entry);
                                });

        if (!entry.process->frames.empty() &&
            entry.state == voice_gateway_entry::gateway_state::connected) {
            std::cout << "Playing audio...\n";
            entry.state = voice_gateway_entry::gateway_state::paused;
            play(entry); // This time we'll actually send data
        }
    } else if (e == boost::asio::error::eof) {
        // Processes have completed
        std::cout << "Process finished reading from pipe\n";
        try {
            entry.process->new_pipes();
        } catch(std::exception &e) {
            std::cerr << e.what() << "\n";
        }
        entry.process->kill();
        entry.process->wait();
    } else {
        std::cerr << "Pipe read error: " << e.message() << "\n";
    }
}

void cmd::discord::voice_state_listener::encode_audio(voice_gateway_entry &entry)
{
    std::lock_guard<std::mutex>(entry.process->mutex);

    const auto *pcm = boost::asio::buffer_cast<const int16_t *>(entry.process->buffer.data());

    // 960 samples (20ms) of 2 channel audio at 16 bit
    constexpr int frame_size = 960;
    constexpr int len = frame_size * 2 * sizeof(int16_t);
    uint8_t buf[512];

    int num_blocks = entry.process->buffer.size() / len;
    for (int i = 0; i < num_blocks; i++) {
        auto encoded_len = entry.process->encoder.encode(pcm, frame_size, buf, sizeof(buf));
        if (encoded_len > 0) {
            std::vector<uint8_t> vec(buf, buf + encoded_len);
            entry.process->frames.emplace_back(std::move(vec), frame_size);
        }
        pcm += frame_size * 2;
        entry.process->buffer.consume(len);
    }
    if (entry.process->buffer.size() > 0) {
        auto remaining_samples = entry.process->buffer.size() / (2 * sizeof(int16_t));
        auto encoded_len = entry.process->encoder.encode(pcm, remaining_samples, buf, sizeof(buf));
        if (encoded_len > 0) {
            std::vector<uint8_t> vec(buf, buf + encoded_len);
            entry.process->frames.emplace_back(std::move(vec), frame_size);
        }
        entry.process->buffer.consume(entry.process->buffer.size());
    }
}

void cmd::discord::voice_state_listener::send_audio(voice_gateway_entry &entry)
{
    if (!entry.process->frames.empty()) {
        if (entry.state == voice_gateway_entry::gateway_state::playing) {
            std::lock_guard<std::mutex> guard(entry.process->mutex);

            entry.process->current_frame = entry.process->frames.front();
            entry.process->frames.pop_front();
            auto &frame = entry.process->current_frame;

            // Next timer expires after frame_size / 48000 seconds, or frame_size / 48 ms
            entry.process->timer.expires_from_now(
                boost::posix_time::milliseconds(frame.frame_size / 48));

            // Play the frame
            entry.gateway->play(frame.encoded_data.data(), frame.encoded_data.size(),
                                frame.frame_size);

            // Enqueue wait for the frame to send
            entry.process->timer.async_wait([&](const boost::system::error_code &e) {
                if (!e && entry.state == voice_gateway_entry::gateway_state::playing) {
                    send_audio(entry);
                }
            });
        }
    } else if(entry.state == voice_gateway_entry::gateway_state::playing) {
        // If we are still in the playing state and there are no more frames to read,
        // play the next entry
        std::cout << "Sound clip finished\n";
        entry.state = voice_gateway_entry::gateway_state::connected;
        play(entry);
    }
}

void cmd::discord::music_process::close_pipes()
{
    if (audio_transport.is_open()) {
        std::cout << "Closing audio transport\n";
        audio_transport.close();
    }
    if (pcm_source.is_open()) {
        std::cout << "Closing pcm source\n";
        pcm_source.close();
    }
}

void cmd::discord::music_process::new_pipes()
{
    close_pipes();
    audio_transport = boost::process::pipe{};
    std::cout << "Assigned new audio transport pipe\n";
    pcm_source = boost::process::async_pipe{service};
    std::cout << "Assigned new pcm source\n";
}

void cmd::discord::music_process::kill()
{
    youtube_dl.terminate();
    ffmpeg.terminate();
}

void cmd::discord::music_process::wait()
{
    youtube_dl.wait();
    ffmpeg.wait();
}
