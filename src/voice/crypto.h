#ifndef CMD_DISCORD_CRYPTO_H
#define CMD_DISCORD_CRYPTO_H

#include <sodium.h>
#include <cstdint>

namespace discord
{
namespace crypto
{
int xsalsa20_poly1305_encrypt(const uint8_t *src, uint8_t *dest, uint64_t src_len,
                              uint8_t *secret_key, uint8_t *nonce);
}
}

#endif
