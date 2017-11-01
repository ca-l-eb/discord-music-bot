#include <net/base64.h>
#include <cmath>

static const unsigned char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// clang-format off
static const unsigned char inv_table[256] = {
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
        64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};
// clang-format on

std::string base64::encode(const std::string &message)
{
    return encode(message.c_str(), message.length());
}

std::string base64::encode(const void *msg, size_t size)
{
    size_t out_size = static_cast<size_t>(std::ceil(size / 3.0f)) * 4;
    std::string encoded;
    encoded.resize(out_size);
    int loc = 0;
    auto message = reinterpret_cast<const char *>(msg);
    while (size > 2) {
        // Process the next 3 bytes
        encoded[loc++] = b64_table[(message[0] >> 2) & 0x3F];
        encoded[loc++] = b64_table[((message[0] & 0x03) << 4) | ((message[1] & 0xF0) >> 4)];
        encoded[loc++] = b64_table[((message[1] & 0x0F) << 2) | ((message[2] & 0xC0) >> 6)];
        encoded[loc++] = b64_table[message[2] & 0x3F];
        message += 3;
        size -= 3;
    }
    if (size > 0) {
        encoded[loc++] = b64_table[(message[0] >> 2) & 0x3F];
        if (size == 2) {
            encoded[loc++] = b64_table[((message[0] & 0x03) << 4) | ((message[1] & 0xF0) >> 4)];
            encoded[loc++] = b64_table[(message[1] & 0x0F) << 2];
        } else {
            encoded[loc++] = b64_table[(message[0] & 0x03) << 4];
            encoded[loc++] = '=';
        }
        encoded[loc++] = '=';
    }
    return encoded;
}

std::vector<unsigned char> base64::decode(const std::string &message)
{
    return decode(message.c_str(), message.length());
}

std::vector<unsigned char> base64::decode(const char *message, size_t size)
{
    if (message[size - 1] == '=') {
        size--;
        if (message[size - 1] == '=')
            size--;
    }
    size_t out_size = (3 * size) / 4;
    std::vector<unsigned char> decoded(out_size);
    int loc = 0;
    while (size > 3) {
        decoded[loc++] = inv_table[(int) message[0]] << 2 | inv_table[(int) message[1]] >> 4;
        decoded[loc++] = inv_table[(int) message[1]] << 4 | inv_table[(int) message[2]] >> 2;
        decoded[loc++] = inv_table[(int) message[2]] << 6 | inv_table[(int) message[3]];
        message += 4;
        size -= 4;
    }
    if (size == 3) {
        // There are 2 more data bytes, 3 more Base64 characters
        decoded[loc++] = inv_table[(int) message[0]] << 2 | inv_table[(int) message[1]] >> 4;
        decoded[loc] = inv_table[(int) message[1]] << 4 | inv_table[(int) message[2]] >> 2;
    } else if (size == 2) {
        // There is 1 more data byte, 2 Base64 characters
        decoded[loc] = inv_table[(int) message[0]] << 2 | inv_table[(int) message[1]] >> 4;
    }
    return decoded;
}
