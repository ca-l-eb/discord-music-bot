#include "errors.h"

const char *gateway_error_category::name() const noexcept
{
    return "gateway";
}

std::string gateway_error_category::message(int ev) const noexcept
{
    switch (gateway_errc(ev)) {
        case gateway_errc::unknown_error:
            return "unknown error";
        case gateway_errc::unknown_opcode:
            return "invalid opcode";
        case gateway_errc::decode_error:
            return "decode gateway_errc";
        case gateway_errc::not_authenticated:
            return "sent payload before identified";
        case gateway_errc::authentication_failed:
            return "incorrect token in identify payload";
        case gateway_errc::already_authenticated:
            return "sent more than one identify payload";
        case gateway_errc::invalid_seq:
            return "invalid sequence number";
        case gateway_errc::rate_limited:
            return "rate limited";
        case gateway_errc::session_timeout:
            return "session has timed out";
        case gateway_errc::invalid_shard:
            return "invalid shard";
        case gateway_errc::sharding_required:
            return "sharding required";
    }
    return "Unknown gateway error";
}

bool gateway_error_category::equivalent(const boost::system::error_code &code, int condition) const
    noexcept
{
    return &code.category() == this && static_cast<int>(code.value()) == condition;
}

const boost::system::error_category &gateway_error_category::instance()
{
    static gateway_error_category instance;
    return instance;
}

boost::system::error_code make_error_code(gateway_errc code) noexcept
{
    return {(int) code, gateway_error_category::instance()};
}

const char *voice_error_category::name() const noexcept
{
    return "voice_gateway";
}

std::string voice_error_category::message(int ev) const noexcept
{
    switch (voice_errc(ev)) {
        case voice_errc::ip_discovery_failed:
            return "ip discovery failed";
        case voice_errc::unknown_opcode:
            return "invalid opcode";
        case voice_errc::not_authenticated:
            return "sent payload before identified";
        case voice_errc::authentication_failed:
            return "incorrect token in identify payload";
        case voice_errc::already_authenticated:
            return "sent more than one identify payload";
        case voice_errc::session_no_longer_valid:
            return "session is no longer valid";
        case voice_errc::session_timeout:
            return "session has timed out";
        case voice_errc::server_not_found:
            return "server not found";
        case voice_errc::unknown_protocol:
            return "unrecognized protocol";
        case voice_errc::disconnected:
            return "disconnected";
        case voice_errc::voice_server_crashed:
            return "voice server crashed";
        case voice_errc::unknown_encryption_mode:
            return "unrecognized encryption";
    }
    return "Unknown voice gateway error";
}

bool voice_error_category::equivalent(const boost::system::error_code &code, int condition) const
    noexcept
{
    return &code.category() == this && static_cast<int>(code.value()) == condition;
}

const boost::system::error_category &voice_error_category::instance()
{
    static voice_error_category instance;
    return instance;
}

boost::system::error_code make_error_code(voice_errc code) noexcept
{
    return {(int) code, voice_error_category::instance()};
}
