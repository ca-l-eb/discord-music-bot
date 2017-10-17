#include <http_pool.h>
#include <iostream>
#include <thread>

#include "api.h"
#include "json.hpp"

cmd::discord::api::api(const std::string &token)
    : sock{cmd::http_pool::get_connection("discordapp.com", 443, true)}
    , stream{sock}
    , token{token}
    , remaining_requests{1}
    , wait_until{clock::now()}
{
}

bool cmd::discord::api::send_message(const std::string &channel_id, const std::string &message)
{
    http_request request{stream};
    set_common_headers(request);
    request.set_request_method("POST");
    request.set_resource(api_base + "/channels/" + channel_id + "/messages");
    nlohmann::json body{{"content", message}};
    request.set_body(body.dump());
    check_success(request, 200);
}

void cmd::discord::api::set_common_headers(cmd::http_request &request)
{
    request.set_header("Authorization", "Bot " + token);
    request.set_header("Content-Type", "application/json");
    request.set_header("User-Agent", "TestBot (https://alucard.io, v0.1)");
}

bool cmd::discord::api::check_success(cmd::http_request &request, int code)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (remaining_requests < 1)
        std::this_thread::sleep_until(wait_until);
    remaining_requests--;

    try {
        request.connect();
        auto response = request.response();
        check_rate_limits(response);
        return response.status_code() == code;
    } catch (std::exception &e) {
        // Something happened, create new connection
        cmd::http_pool::mark_closed(sock->get_host(), sock->get_port());
        sock = cmd::http_pool::get_connection(sock->get_host(), sock->get_port(), true);
        stream = cmd::stream{sock};
    }
    return false;
}

void cmd::discord::api::check_rate_limits(cmd::http_response &response)
{
    auto &headers = response.headers();
    auto now = clock::now();

    if (response.status_code() == 429) {
        // We're being rate limited, check when to try again.
        // First check header for when to retry if 'retry-after' field is present
        // otherwise fallback by checking body.
        auto retry_after = headers.find("retry-after");
        if (retry_after != headers.end()) {
            int time_ms = std::stoi(retry_after->second);
            wait_until = now + std::chrono::milliseconds{time_ms};
        } else {
            nlohmann::json json = response.body();
            if (json["retry_after"].is_number())
                wait_until = now + std::chrono::milliseconds{json.at("retry_after").get<int>()};
            else
                wait_until = now + std::chrono::seconds{10};  // Try again in 10 seconds
        }
        remaining_requests = 0;
    }
    else {
        auto limit = headers.find("x-ratelimit-limit");
        auto remaining = headers.find("x-ratelimit-remaining");
        auto reset = headers.find("x-ratelimit-reset");
        auto global = headers.find("x-ratelimit-global");

        if (limit != headers.end()) {
            remaining_requests = std::stoi(limit->second);
        }
        std::cout << "Remaining requests: " << remaining_requests << "\n";
        if (limit != headers.end()) {
            std::cout << "Limit: " << limit->second << "\n";
        }
        if (global != headers.end()) {
            std::cout << "Global: " << global->second << "\n";
        }
        if (reset != headers.end()) {
            std::time_t unix_epoch_reset = std::stoi(reset->second);
            std::time_t unix_epoch_now = std::time(nullptr);
            wait_until = now + std::chrono::seconds{unix_epoch_reset - unix_epoch_now};
            std::cout << "Unix now: " << unix_epoch_now << " resets at " << unix_epoch_reset << " (" << (unix_epoch_reset - unix_epoch_now) << " s)\n";
        }
        std::cout << "Clock now: " << now.time_since_epoch().count() << " resets at " << wait_until.time_since_epoch().count() << "\n";

    }
}
