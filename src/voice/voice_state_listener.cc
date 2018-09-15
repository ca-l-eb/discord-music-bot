#include <algorithm>
#include <iostream>
#include <regex>
#include <set>

#include "audio/file_source.h"
#include "audio/youtube_dl.h"
#include "gateway.h"
#include "net/uri.h"
#include "voice/voice_gateway.h"
#include "voice/voice_state_listener.h"

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
    for (auto &it : voice_map) {
        it.second->disconnect();
    }
    voice_map.clear();
}

void discord::voice_state_listener::on_voice_state_update(const nlohmann::json &data)
{
    auto state = data.get<discord::voice_state>();

    // We're looking for voice state update for this user_id
    if (gateway.get_user_id() != state.user_id) {
        return;
    }

    // Create the context if it doesn't exist
    if (voice_map.count(state.guild_id) == 0) {
        voice_map[state.guild_id] =
            std::make_shared<voice_context>(ctx, gateway.get_gateway_store());
    }

    voice_map[state.guild_id]->on_voice_state_update(std::move(state));
}

void discord::voice_state_listener::on_voice_server_update(const nlohmann::json &data)
{
    auto vsu = data.get<discord::event::voice_server_update>();

    auto it = voice_map.find(vsu.guild_id);
    if (it == voice_map.end()) {
        return;
    }
    it->second->on_voice_server_update(std::move(vsu), gateway.get_user_id(), tls);
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
    auto it = voice_map.find(guild_id);

    if (command == "join") {
        join_channel(m, params);
    } else if (it != voice_map.end()) {
        auto &context = *it->second;
        if (command == "leave") {
            context.leave_channel();
            leave_voice_server(guild_id);
        } else if (command == "list" || command == "l")
            context.list_queue();
        else if (command == "add" || command == "a")
            context.add_queue(params);
        else if (command == "skip" || command == "next")
            context.skip_current();
        else if (command == "play")
            context.play();
        else if (command == "pause")
            context.pause();
    }
}

static const discord::guild *get_guild_from_channel(discord::snowflake channel_id,
                                                    const discord::gateway_store &store)
{
    auto guild_id = store.lookup_channel(channel_id);
    if (guild_id == 0)
        return nullptr;

    return store.get_guild(guild_id);
}

void discord::voice_state_listener::join_channel(const discord::message &m,
                                                 const std::string &channel_name)
{
    auto *guild = get_guild_from_channel(m.channel_id, gateway.get_gateway_store());
    if (!guild)
        return;

    auto it = voice_map.find(guild->id);
    auto connected = it != voice_map.end();
    auto &context = it->second;

    // If the user does not specify a channel to join, join the channel the user is in
    if (channel_name.empty()) {
        auto find = discord::voice_state{};
        find.user_id = m.author.id;

        if (const auto &user_voice_state = guild->voice_states.find(find);
            user_voice_state != guild->voice_states.end()) {
            join_voice_server(guild->id, user_voice_state->channel_id);
        }

    } else {
        // Look through the guild's channels for matching channel name, if it exists, join, else
        // fail silently
        for (auto channel : guild->channels) {
            if (channel.type == discord::channel::channel_type::guild_voice &&
                channel.name == channel_name) {
                // Found a matching voice channel name! Join it if it is different
                // than the currently connected channel (if any)
                if (!connected || (context && context->get_channel_id() != channel.id)) {
                    join_voice_server(guild->id, channel.id);
                    return;
                }
            }
        }
    }
}

void discord::voice_state_listener::join_voice_server(discord::snowflake guild_id,
                                                      discord::snowflake channel_id)
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

void discord::voice_state_listener::leave_voice_server(discord::snowflake guild_id)
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

const discord::gateway &discord::voice_state_listener::get_gateway() const
{
    return gateway;
}

discord::voice_context::voice_context(boost::asio::io_context &ctx,
                                      const discord::gateway_store &store)
    : ctx{ctx}, timer{ctx}, store{store}
{
}

discord::voice_context::~voice_context()
{
    disconnect();
}

void discord::voice_context::disconnect()
{
    timer.cancel();
    gateway.reset();
    source.reset();
}

void discord::voice_context::on_voice_state_update(discord::voice_state state)
{
    channel_id = state.channel_id;
    guild_id = state.guild_id;
    session_id = std::move(state.session_id);
    update_bitrate();
}

void discord::voice_context::on_voice_server_update(discord::event::voice_server_update v,
                                                    discord::snowflake user_id, ssl::context &tls)
{
    if (guild_id == v.guild_id) {
        token = std::move(v.token);
        endpoint = std::move(v.endpoint);

        // We got all the information needed to connect to a voice gateway
        gateway = std::make_shared<discord::voice_gateway>(ctx, tls, *this, user_id);

        std::cout << "[voice] created voice gateway\n";

        auto gateway_connect_cb = [weak = weak_from_this()](const auto &ec) {
            if (auto self = weak.lock()) {
                if (ec) {
                    std::cerr << "[voice] voice gateway connection error: " << ec.message() << "\n";
                } else {
                    std::cout << "[voice] connected to voice gateway. Ready to send audio\n";
                    self->p_state = voice_context::state::connected;
                }
            }
        };

        gateway->connect(gateway_connect_cb);
    }
}

void discord::voice_context::update_bitrate()
{
    auto guild = get_guild_from_channel(channel_id, store);
    if (!guild)
        return;

    // to_find contains channel_id to match with the desired channel to determine the audio
    // bitrate
    auto to_find = discord::channel{};
    to_find.id = channel_id;
    auto channel = guild->channels.find(to_find);
    if (channel != guild->channels.end()) {
        encoder.set_bitrate(channel->bitrate);
        std::cout << "[voice] '" << channel->name << "' playing at " << (channel->bitrate / 1000)
                  << "Kbps\n";
    }
}

void discord::voice_context::leave_channel()
{
    if (p_state != voice_context::state::disconnected) {
        p_state = voice_context::state::disconnected;
        music_queue.clear();
        gateway->stop();
    }
}

void discord::voice_context::list_queue() {}

void discord::voice_context::add_queue(const std::string &params)
{
    music_queue.push_back(params);
    if (p_state == voice_context::state::connected) {
        play();
    }
}

void discord::voice_context::skip_current()
{
    if (p_state == voice_context::state::playing || p_state == voice_context::state::paused) {
        p_state = voice_context::state::connected;

        if (!music_queue.empty())
            play();
        else
            gateway->stop();
    }
}

void discord::voice_context::play()
{
    if (p_state == voice_context::state::playing)
        return;  // We are already playing!

    // Connected state meaning ready to send audio, but nothing loaded at the moment
    if (p_state == voice_context::state::connected) {
        next_audio_source();
    } else if (p_state == voice_context::state::paused) {
        p_state = voice_context::state::playing;  // Resume
        send_next_frame();
    }
}

void discord::voice_context::pause()
{
    if (p_state == voice_context::state::playing) {
        p_state = voice_context::state::paused;
        gateway->stop();
    }
}

void discord::voice_context::notify_audio_source_ready(const boost::system::error_code &ec)
{
    if (ec) {
        std::cerr << "[voice] error making audio source: " << ec.message() << "\n";
        return;
    }
    p_state = voice_context::state::playing;
    send_next_frame();
}

void discord::voice_context::next_audio_source()
{
    if (music_queue.empty())
        return;

    // Get the next song from the queue
    auto next = std::move(music_queue.front());
    music_queue.pop_front();

    auto parsed = uri::parse(next);
    if (parsed.authority.empty()) {
        std::cerr << "[voice] invalid audio source\n";
        return;
    }
    static auto valid_youtube_dl_sources =
        std::set<std::string>{"youtube.com", "youtu.be", "www.youtube.com"};

    if (valid_youtube_dl_sources.count(parsed.authority)) {
        source = std::make_shared<youtube_dl_source>(*this, next);
        source->prepare();
    } else if (parsed.scheme == "file") {
        source = std::make_shared<file_source>(*this, parsed.path);
        source->prepare();
    }
}

void discord::voice_context::send_next_frame()
{
    if (p_state != voice_context::state::playing)
        return;

    assert(source);

    using namespace std::chrono;
    using namespace std::chrono;
    static auto last_frame_time = high_resolution_clock::now();
    static auto last_frame_size = 0;

    auto start = high_resolution_clock::now();
    auto frame = source->next();
    auto retrieval_time_us =
        duration_cast<microseconds>(high_resolution_clock::now() - start).count();
    auto time_since_last_frame_us = duration_cast<microseconds>(start - last_frame_time).count();

    // auto timer_done_cb = [weak = weak_from_this()](const auto &ec) {
    auto timer_done_cb = [this](const auto &ec) {
        if (!ec)
            send_next_frame();
    };

    if (!frame.data.empty()) {
        auto fc = frame.frame_count;
        if (!(fc == 120 || fc == 240 || fc == 480 || fc == 960 || fc == 1920 || fc == 2880)) {
            std::cerr << "[voice] invalid frame size: " << fc << "\n";
            return;
        }

        auto expected_time_diff = last_frame_size * 1000 / 48;
        auto time_offset = std::max<int64_t>(time_since_last_frame_us - expected_time_diff, 0);

        // Next timer expires after frame_size / 48000 seconds, or frame size / 48 ms
        auto expires_us = frame.frame_count * 1000 / 48 - retrieval_time_us - time_offset;
        timer.expires_after(microseconds(expires_us));

        // Play the frame
        gateway->play(frame);
    } else if (!frame.end_of_source) {
        // Data from source not yet available... try again in a little
        timer.expires_from_now(microseconds(500));
    }
    if (frame.end_of_source) {
        // Done with the current source, play next entry
        std::cout << "[voice] sound clip finished\n";
        timer.cancel();
        gateway->stop();
        p_state = voice_context::state::connected;
        play();
    }
    last_frame_size = frame.frame_count;
    last_frame_time = start;
    timer.async_wait(timer_done_cb);
}

discord::snowflake discord::voice_context::get_channel_id() const
{
    return channel_id;
}

discord::snowflake discord::voice_context::get_guild_id() const
{
    return guild_id;
}

const std::string &discord::voice_context::get_session_id() const
{
    return session_id;
}

const std::string &discord::voice_context::get_token() const
{
    return token;
}

const std::string &discord::voice_context::get_endpoint() const
{
    return endpoint;
}

void discord::voice_context::set_endpoint(const std::string &s)
{
    endpoint = s;
}

discord::opus_encoder &discord::voice_context::get_encoder()
{
    return encoder;
}

boost::asio::io_context &discord::voice_context::get_io_context()
{
    return ctx;
}
