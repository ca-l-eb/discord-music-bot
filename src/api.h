#ifndef CMD_DISCORD_API_H
#define CMD_DISCORD_API_H

#include <http_request.h>
#include <chrono>
#include <mutex>
#include <queue>

namespace cmd
{
namespace discord
{
struct timeout {
    int limit;
    int remaining;
    std::time_t reset;

    timeout() : limit{1}, remaining{1}, reset{0} {}
};

class api
{
public:
    api(const std::string &token);
    api(const api &) = delete;
    api &operator=(const api &) = delete;
    ~api() = default;
    bool send_message(const std::string &channel_id, const std::string &message);

private:
    using clock = std::chrono::steady_clock;
    const std::string api_base = "/api/v6";
    cmd::socket::ptr sock;
    cmd::stream stream;
    std::string token;
    std::mutex mutex;
    int remaining_requests;
    clock::time_point wait_until;

    void set_common_headers(cmd::http_request &request);
    bool check_success(cmd::http_request &request, int code);
    void check_rate_limits(cmd::http_response &response);
};
}
}

#endif
