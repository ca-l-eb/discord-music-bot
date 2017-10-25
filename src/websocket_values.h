#ifndef WEBSOCKET_VALUES_H
#define WEBSOCKET_VALUES_H

#include <cstdint>

static const char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum class websocket_opcode : int8_t {
    none = -1,
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA
};

enum class websocket_status_code : uint16_t {
    normal = 1000,
    going_away = 1001,
    protocol_error = 1002,
    data_error = 1003,  // e.g. got binary when expected text
    reserved = 1004,
    no_status_code_present = 1005,  // don't send
    closed_abnormally = 1006,       // don't send
    inconsistent_data = 1007,
    policy_violation = 1008,  // generic code return
    message_too_big = 1009,
    extension_negotiation_failure = 1010,
    unexpected_error = 1011,
    tls_handshake_error = 1015  // don't send
};

#endif
