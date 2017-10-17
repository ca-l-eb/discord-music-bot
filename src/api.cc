#include <http_pool.h>
#include <iostream>
#include <thread>

#include "api.h"
#include "json.hpp"

cmd::discord::api::api(const std::string &token)
    : sock{cmd::http_pool::get_connection("discordapp.com", 443, true)}, stream{sock}, token{token}
{
    // Guess that the global limit is around 20
    limits[api_limit_param::global] = {20, 20, 0};
    limits[api_limit_param::channel_id] = {5, 5, 0};
    limits[api_limit_param::guild_id] = {5, 5, 0};
    limits[api_limit_param::webhook_id] = {5, 5, 0};
}

cmd::discord::api_result cmd::discord::api::send_message(const std::string &channel_id,
                                                         const std::string &message)
{
    // Try to send the message twice, if the first time fails, we get updated
    // rate limits so we know when the second message can successfully be sent
    http_request request{stream};
    set_common_headers(request);
    request.set_request_method("POST");
    request.set_resource(api_base + "/channels/" + channel_id + "/messages");
    nlohmann::json body{{"content", message}};
    request.set_body(body.dump());
    return check_success(request, 200, api_limit_param::channel_id);
}

void cmd::discord::api::set_common_headers(cmd::http_request &request)
{
    request.set_header("Authorization", "Bot " + token);
    request.set_header("Content-Type", "application/json");
    request.set_header("User-Agent", "TestBot (https://alucard.io, v0.1)");
}

cmd::discord::api_result cmd::discord::api::check_success(cmd::http_request &request, int code,
                                                          api_limit_param param)
{
    std::lock_guard<std::mutex> guard(mutex);

    auto &global = limits[api_limit_param::global];
    auto &specific = limits[param];

    // Check global rate limit, if we can send, then check the api_limit_param
    // If we can't send for either, sleep for until the reset max of the 2 reset times
    if (global.remaining > 0 && specific.remaining > 0) {
        // We can send. Decrement remaining for both limits, but be careful not to decrement
        // the global remaining twice in cases where the is no specific route limit parameter
        global.remaining--;
        if (param != api_limit_param::global)
            specific.remaining--;
    } else {
        std::time_t reset = std::max(global.reset, specific.reset);

        std::time_t sleep_for = reset - std::time(nullptr);
        if (sleep_for > 0) {
            std::cout << "Sleeping for " << sleep_for << " seconds\n";
            std::this_thread::sleep_for(std::chrono::seconds(sleep_for));
        }
        // Reset time passed, reset limits
        if (reset >= specific.reset)
            specific.remaining = specific.limit;

        if ((global.reset != 0 && reset >= global.reset) || global.remaining == 0)
            global.remaining = global.limit;
    }

    try {
        request.connect();
        auto response = request.response();
        auto limited = check_rate_limits(response, param);

        if (limited == api_result::rate_limited)
            return api_result::rate_limited;
        if (response.status_code() == code)
            return api_result::success;
    } catch (std::exception &e) {
        // Something happened, create new connection
        cmd::http_pool::mark_closed(sock->get_host(), sock->get_port());
        sock = cmd::http_pool::get_connection(sock->get_host(), sock->get_port(), true);
        stream = cmd::stream{sock};
    }
    return api_result::failure;
}

cmd::discord::api_result cmd::discord::api::check_rate_limits(cmd::http_response &response,
                                                              api_limit_param param)
{
    auto &headers = response.headers();

    auto limit = headers.find("x-ratelimit-limit");
    auto remaining = headers.find("x-ratelimit-remaining");
    auto reset = headers.find("x-ratelimit-reset");
    bool global = headers.find("x-ratelimit-global") != headers.end();

    rate_limit &which = global ? limits[api_limit_param::global] : limits[param];

    // Update rate-limits
    if (limit != headers.end())
        which.limit = std::stoi(limit->second);

    if (remaining != headers.end())
        which.remaining = std::stoi(remaining->second);

    if (reset != headers.end())
        which.reset = std::stoi(reset->second);

    if (response.status_code() == 429) {
        // We're being rate limited, check when to try again.
        // First check header for when to retry if 'retry-after' field is present
        // otherwise fallback by checking body.
        auto retry_after = headers.find("retry-after");
        int retry_time_ms;
        if (retry_after != headers.end()) {
            retry_time_ms = std::stoi(retry_after->second);
        } else {
            nlohmann::json json = response.body();
            if (json.is_object()) {
                retry_time_ms = json["retry_after"];
                global = json["global"];
            }
        }
        which = global ? limits[api_limit_param::global] : limits[param];

        if (global) {
            // If global remaining has more, subtract the remaining amount
            // from the global limit to get closer to the actual global limit imposed by Discord
            // since it unclear if it is ever actually sent in http response headers
            //
            // e.g. We start off assuming global limit is 20, but after 15 messages in
            // a somewhat rapid succession, we get 429. We then know that remaining would be
            // 5, so 20 - 5 = 15, which would be the new global limit approximation we set.
            // We do the max test to make sure there is no negative limits.
            if (which.remaining != 0)
                which.limit = std::max(which.remaining, which.limit - which.remaining);
        }

        which.remaining = 0;
        which.reset =
            std::time(nullptr) + static_cast<std::time_t>(std::ceil(retry_time_ms / 1000));

        return api_result::rate_limited;
    }
    // success does not really mean anything here, just signals not rate limited
    return api_result::success;
}
