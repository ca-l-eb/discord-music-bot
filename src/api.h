#ifndef CMD_DISCORD_API_H
#define CMD_DISCORD_API_H

#include <chrono>
#include <map>
#include <mutex>

namespace discord
{
enum class api_limit_param { channel_id, guild_id, webhook_id, global };

struct rate_limit {
    int limit;
    int remaining;
    std::time_t reset;
};

enum class api_result { failure, success, rate_limited };

class api
{
public:
    //    explicit api(const std::string &token);
    //    api(const api &) = delete;
    //    api &operator=(const api &) = delete;
    //    ~api() = default;
    //    std::string get_gateway();
    //    api_result send_message(const std::string &channel_id, const std::string &message);

private:
    using clock = std::chrono::system_clock;
    const std::string api_base = "https://discordapp.com/api/v6";
    std::string token;
    std::mutex mutex;
    std::map<api_limit_param, rate_limit> limits;
    clock::time_point last_msg_sent;

    //    void set_common_headers(http_request &request, bool requires_auth);
    //    api_response check_success(http_request &request, int code, api_limit_param param);
    //    api_result check_rate_limits(http_response &response, api_limit_param param);
};
}

#endif
