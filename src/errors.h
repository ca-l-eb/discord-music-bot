#ifndef DISCORD_ERRORS_H
#define DISCORD_ERRORS_H

#include <boost/system/error_code.hpp>
#include <boost/type_traits/integral_constant.hpp>
#include <string>

enum class gateway_errc {
    unknown_error = 4000,
    unknown_opcode = 4001,
    decode_error = 4002,
    not_authenticated = 4003,
    authentication_failed = 4004,
    already_authenticated = 4005,
    invalid_seq = 4007,
    rate_limited = 4008,
    session_timeout = 4009,
    invalid_shard = 4010,
    sharding_required = 4011
};

struct gateway_error_category : public boost::system::error_category {
    virtual const char *name() const noexcept;
    virtual std::string message(int ev) const noexcept;
    virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept;
    static const boost::system::error_category &instance();
};

enum class voice_errc {
    ip_discovery_failed = 1,
    unknown_opcode = 4001,
    not_authenticated = 4003,
    authentication_failed = 4004,
    already_authenticated = 4005,
    session_no_longer_valid = 4006,
    session_timeout = 4009,
    server_not_found = 4011,
    unknown_protocol = 4012,
    disconnected = 4014,
    voice_server_crashed = 4015,
    unknown_encryption_mode = 4016
};

struct voice_error_category : public boost::system::error_category {
    virtual const char *name() const noexcept;
    virtual std::string message(int ev) const noexcept;
    virtual bool equivalent(const boost::system::error_code &code, int condition) const noexcept;
    static const boost::system::error_category &instance();
};

boost::system::error_code make_error_code(gateway_errc code) noexcept;
boost::system::error_code make_error_code(voice_errc code) noexcept;

template<>
struct boost::system::is_error_code_enum<gateway_errc> : public boost::true_type {
};

template<>
struct boost::system::is_error_code_enum<voice_errc> : public boost::true_type {
};

#endif
