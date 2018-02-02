#include "voice/crypto.h"

int discord::crypto::xsalsa20_poly1305_encrypt(const uint8_t *src, uint8_t *dest, uint64_t src_len,
                                               uint8_t *secret_key, uint8_t *nonce)
{
    return crypto_secretbox_easy(dest, src, src_len, nonce, secret_key);
}
