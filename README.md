## About
This is a self-hosted, basic music streaming bot for [Discord](https://discordapp.com/) written in C++(17). I've tested and got it working on Linux, macOS, and Windows, but runs best on Linux.

## Building
```
mkdir build
cd build
conan install ..
cmake ..
cmake --build .
```

## Running
Create a bot account [here](https://discordapp.com/developers/applications/me/). Use http://localhost for the redirect uri. Select the public bot checkbox and keep the bot's token safe.

Then go to
`https://discordapp.com/api/oauth2/authorize?client_id=$CLIENT_ID&permissions=36766720&redirect_uri=http%3A%2F%2Flocalhost&scope=bot`
replacing $CLIENT_ID with your bot's client id to invite the bot to your guild.

Finally `./discord <bot-token>` will run the bot.

### Using the bot
- Joining channels `:join <channel name>`
- Adding music to queue `:add <youtube link>`
- Pausing `:pause`
- Playing `:play`
- Stopping `:stop`
- Skipping song `:skip` or `:next`
- Leaving voice channel `:leave`

## Dependencies
- [Boost.Asio](https://think-async.com/)
- [Boost.Beast](https://github.com/boostorg/beast)
- [Boost.Process](https://github.com/klemens-morgenstern/boost-process)
- [FFmpeg](https://www.ffmpeg.org/)
- [OpenSSL](https://www.openssl.org/)
- [libsodium](https://download.libsodium.org/doc/)
- [opus](http://opus-codec.org/)
- [youtube-dl](https://github.com/rg3/youtube-dl)
- [nlohmann-json](https://github.com/nlohmann/json)
