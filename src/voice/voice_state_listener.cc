#include <algorithm>
#include <iostream>
#include <regex>
#include <set>

#include "audio_source/youtube_dl.h"
#include "net/resource_parser.h"
#include "voice/voice_state_listener.h"

static discord::guild *get_guild_from_channel(uint64_t channel_id, discord::gateway_store &store)
{
    auto guild_id = store.lookup_channel(channel_id);
    if (!guild_id)
        return nullptr;

    return store.get_guild(guild_id);
}

static void update_bitrate(discord::voice_gateway_entry &entry, discord::gateway_store &store)
{
    auto guild = get_guild_from_channel(entry.channel_id, store);
    if (!guild)
        return;

    // to_find contains channel_id to match with the desired channel to determine the audio
    // bitrate
    auto to_find = discord::channel{};
    to_find.id = entry.channel_id;
    auto channel = guild->channels.find(to_find);
    if (channel != guild->channels.end()) {
        entry.process->encoder.set_bitrate(channel->bitrate);
        std::cout << "[voice state] '" << channel->name << "' playing at "
                  << (channel->bitrate / 1000) << "Kbps\n";
    }
}

discord::voice_state_listener::voice_state_listener(boost::asio::io_context &ctx, ssl::context &tls,
                                                    discord::gateway &gateway)
    : ctx{ctx}, tls{tls}, gateway{gateway}
{
}

discord::voice_state_listener::~voice_state_listener()
{
    disconnect();
}

void discord::voice_state_listener::disconnect()
{
    for (auto &it : voice_gateways) {
        it.second->gateway->disconnect();
    }
    voice_gateways.clear();
}

void discord::voice_state_listener::on_voice_state_update(const nlohmann::json &data)
{
    auto state = data.get<discord::voice_state>();
    assert(state.guild_id);

    // We're looking for voice state update for this user_id
    if (gateway.get_user_id() != state.user_id) {
        return;
    }

    // Create the entry if it doesn't exist
    if (voice_gateways.count(state.guild_id) == 0) {
        voice_gateways[state.guild_id] = std::make_shared<voice_gateway_entry>();
    }

    auto &entry = *voice_gateways[state.guild_id];
    entry.channel_id = state.channel_id;
    entry.guild_id = state.guild_id;
    entry.session_id = std::move(state.session_id);

    if (entry.process)
        update_bitrate(entry, gateway.get_gateway_store());
}

void discord::voice_state_listener::on_voice_server_update(const nlohmann::json &data)
{
    auto vsu = data.get<discord::event::voice_server_update>();

    auto it = voice_gateways.find(vsu.guild_id);
    if (it == voice_gateways.end()) {
        return;
    }

    auto &entry = it->second;
    assert(vsu.guild_id == entry->guild_id);
    entry->token = std::move(vsu.token);
    entry->endpoint = std::move(vsu.endpoint);

    auto gateway_connect_cb = [weak = weak_from_this(), &entry](auto &ec) {
        if (auto self = weak.lock()) {
            if (ec) {
                std::cerr << "[voice state] voice gateway connection error: " << ec.message()
                          << "\n";
            } else {
                std::cout << "[voice state] connected to voice gateway. Ready to send audio\n";
                entry->p_state = voice_gateway_entry::state::connected;
            }
        }
    };

    // We got all the information needed to join a voice gateway now
    entry->gateway =
        std::make_shared<discord::voice_gateway>(ctx, tls, entry, gateway.get_user_id());
    std::cout << "[voice state] created voice gateway\n";
    entry->gateway->connect(gateway_connect_cb);
}

// Listen for guild text messages indicating to join, leave, play, pause, etc.
void discord::voice_state_listener::on_message_create(const nlohmann::json &data)
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
    static auto command_re = std::regex{R"(^:(\S+)(?:\s+(.+))?$)"};
    auto matcher = std::smatch{};
    std::regex_search(m.content, matcher, command_re);

    if (matcher.empty())
        return;

    auto command = matcher.str(1);
    auto params = matcher.str(2);
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    auto guild_id = gateway.get_gateway_store().lookup_channel(m.channel_id);
    auto it = voice_gateways.find(guild_id);

    if (command == "join") {
        do_join(m, params);
    } else if (it != voice_gateways.end()) {
        auto &entry = *it->second;
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
    auto *guild = get_guild_from_channel(m.channel_id, gateway.get_gateway_store());
    if (!guild)
        return;

    auto it = voice_gateways.find(guild->id);
    auto connected = it != voice_gateways.end();

    // Look through the guild's channels for channel s, if it exists, join, else fail silently
    for (auto channel : guild->channels) {
        if (channel.type == discord::channel::channel_type::guild_voice && channel.name == s) {
            // Found a matching voice channel name! Join it if it is different
            // than the currently connected channel (if any)
            if (!connected || (it->second && it->second->channel_id != channel.id)) {
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
        entry.gateway->stop();
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

        if (!entry.music_queue.empty())
            do_play(entry);
        else
            entry.gateway->stop();
    }
}

void discord::voice_state_listener::do_play(discord::voice_gateway_entry &entry)
{
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
    auto guild_str = std::to_string(guild_id);
    auto channel_str = std::to_string(channel_id);

    // After join, we expect back a VOICE_STATE_UPDATE event
    auto json = nlohmann::json{{"op", static_cast<int>(gateway_op::voice_state_update)},
                               {"d",
                                {{"guild_id", guild_str},
                                 {"channel_id", channel_str},
                                 {"self_mute", false},
                                 {"self_deaf", false}}}};
    gateway.send(json.dump(), print_transfer_info);
}

void discord::voice_state_listener::leave_voice_server(uint64_t guild_id)
{
    // json serializer doesn't like being passed char* pointing to nullptr,
    // so I guess we need to double this method
    auto guild_str = std::to_string(guild_id);
    auto json = nlohmann::json{{"op", static_cast<int>(gateway_op::voice_state_update)},
                               {"d",
                                {{"guild_id", guild_str},
                                 {"channel_id", nullptr},
                                 {"self_mute", false},
                                 {"self_deaf", false}}}};
    gateway.send(json.dump(), print_transfer_info);
}

void discord::voice_state_listener::play(voice_gateway_entry &entry)
{
    if (entry.p_state == voice_gateway_entry::state::playing)
        return;  // We are already playing!

    // Connected state meaning ready to send audio, but nothing loaded at the moment
    if (entry.p_state == voice_gateway_entry::state::connected) {
        next_audio_source(entry);
    } else if (entry.p_state == voice_gateway_entry::state::paused) {
        entry.p_state = voice_gateway_entry::state::playing;  // Resume
    }
    if (entry.p_state == voice_gateway_entry::state::playing) {
        send_audio(entry);
    }
}

void discord::voice_state_listener::next_audio_source(voice_gateway_entry &entry)
{
    if (entry.music_queue.empty())
        return;

    if (!entry.process) {
        entry.process = std::make_unique<music_process>(ctx);
        update_bitrate(entry, gateway.get_gateway_store());
    }

    // Get the next song from the queue
    auto next = std::move(entry.music_queue.front());
    entry.music_queue.pop_front();

    auto callback = [&](auto &ec) {
        if (ec) {
            std::cerr << "[voice state] error making audio source: " << ec.message() << "\n";
            return;
        }
        entry.p_state = voice_gateway_entry::state::playing;
        send_audio(entry);
    };

    auto parsed = resource_parser::parse(next);
    if (parsed.host.empty()) {
        std::cerr << "[voice state] invalid audio source\n";
        return;
    }
    static auto valid_youtube_dl_sources =
        std::set<std::string>{"youtube.com", "youtu.be", "www.youtube.com"};

    if (valid_youtube_dl_sources.count(parsed.host)) {
        entry.process->source =
            std::make_shared<youtube_dl_source>(ctx, entry.process->encoder, next, callback);
        entry.process->source->prepare();
    }
}

void discord::voice_state_listener::send_audio(voice_gateway_entry &entry)
{
    assert(entry.process);
    assert(entry.process->source);
    assert(entry.p_state == voice_gateway_entry::state::playing);

    auto start = std::chrono::high_resolution_clock::now();
    auto frame = entry.process->source->next();
    auto end = std::chrono::high_resolution_clock::now();
    auto time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    auto timer_done_cb = [weak = weak_from_this(), &entry](auto &ec) {
        if (auto self = weak.lock()) {
            if (!ec && entry.p_state == voice_gateway_entry::state::playing) {
                self->send_audio(entry);
            }
        }
    };

    if (!frame.data.empty()) {
        auto fc = frame.frame_count;
        if (!(fc == 120 || fc == 240 || fc == 480 || fc == 960 || fc == 1920 || fc == 2880)) {
            std::cerr << "[voice state] invalid frame size: " << fc << "\n";
            return;
        }
        // Next timer expires after frame_size / 48000 seconds, or frame size / 48 ms
        entry.process->timer.expires_from_now(
            boost::posix_time::microseconds(((frame.frame_count * 1000) / 48) - time_us.count()));

        // Play the frame
        entry.gateway->play(frame);
    } else if (!frame.end_of_source) {
        // Data from source not yet available... try again in a little
        entry.process->timer.expires_from_now(boost::posix_time::milliseconds(20));
    }
    if (frame.end_of_source) {
        // Done with the current source, play next entry
        std::cout << "[voice state] sound clip finished\n";
        entry.gateway->stop();
        entry.p_state = voice_gateway_entry::state::connected;
        play(entry);
    }
    entry.process->timer.async_wait(timer_done_cb);
}
