#include "smali_internal.h"
#include <stdint.h>
#include <string.h>

uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void sha1_block(uint32_t h[5], const uint8_t *blk) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    for (int i = 16; i < 80; i++) {
        uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (x << 1) | (x >> 31);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

/* Full blocks are hashed in place; only the tail (the 0x80 terminator and
 * the 64-bit length need 9 bytes, so one or two blocks) is copied. */
void smali_sha1(const uint8_t *data, size_t len, uint8_t *out) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    size_t full = len & ~(size_t)63;
    for (size_t off = 0; off < full; off += 64) sha1_block(h, data + off);
    uint8_t tail[128] = {0};
    size_t rem = len - full, tlen = rem + 9 <= 64 ? 64 : 128;
    memcpy(tail, data + full, rem);
    tail[rem] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tlen - 8 + i] = (uint8_t)(bits >> (56 - i * 8));
    for (size_t off = 0; off < tlen; off += 64) sha1_block(h, tail + off);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h[i] >> (24 - j * 8));
}
