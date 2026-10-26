#include "format_arsc_pool.h"
#include "format_arsc_fmt.h"
#include <stdlib.h>
#include <string.h>

char *arsc_pool_get(const uint8_t *pool, uint32_t index) {
    uint32_t count = arsc_f32(pool + 8);
    if (index >= count) return NULL;
    uint32_t flags = arsc_f32(pool + 16);
    uint32_t strs_start = arsc_f32(pool + 20);
    uint32_t hdr = arsc_f16(pool + 2);
    uint32_t off = arsc_f32(pool + hdr + index * 4);
    const uint8_t *p = pool + strs_start + off;
    if (flags & (1u << 8)) {
        /* UTF-8: one or two bytes of UTF-16 length, one or two bytes of byte
           length, the bytes themselves and a NUL. */
        if (*p & 0x80) p += 2; else p += 1;
        uint32_t blen;
        if (*p & 0x80) {
            blen = ((uint32_t)(p[0] & 0x7F) << 8) | p[1];
            p += 2;
        } else {
            blen = *p;
            p += 1;
        }
        char *s = malloc(blen + 1);
        if (!s) return NULL;
        memcpy(s, p, blen);
        s[blen] = 0;
        return s;
    }
    /* UTF-16: a 16 bit length, the code units and a NUL. */
    uint16_t len = arsc_f16(p);
    p += 2;
    char *s = malloc((size_t)len * 4 + 1);
    if (!s) return NULL;
    uint16_t units[4096];
    if (len > 4096) len = 4096;
    for (uint16_t i = 0; i < len; i++) units[i] = arsc_f16(p + i * 2);
    arsc_utf16_to_utf8(units, len, s, (size_t)len * 4 + 1);
    return s;
}

void arsc_package_name(const uint8_t *pkg, char *out, size_t out_sz) {
    uint16_t units[128];
    for (int i = 0; i < 128; i++) units[i] = arsc_f16(pkg + 12 + i * 2);
    arsc_utf16_to_utf8(units, 128, out, out_sz);
}
