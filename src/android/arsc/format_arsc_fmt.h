#ifndef FORMAT_ARSC_FMT_H
#define FORMAT_ARSC_FMT_H

#include <stddef.h>
#include <stdint.h>

/* Little endian accessors used by the resource table reader and writer. */
static inline uint16_t arsc_f16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t arsc_f32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * Decodes a NUL terminated UTF-8 string into UTF-16 code units.
 * `out` may be NULL, in which case only the number of code units is counted.
 * Surrogate pairs are written as two code units and counted as two.
 */
static inline uint32_t arsc_utf8_to_utf16(const char *s, uint16_t *out, uint32_t cap) {
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)(s ? s : "");
    while (*p) {
        uint32_t cp;
        uint32_t len;
        if (*p < 0x80) {
            cp = *p;
            len = 1;
        } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
            len = 2;
        } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
            len = 3;
        } else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
                   (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                 ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
            len = 4;
        } else {
            cp = 0xFFFD;
            len = 1;
        }
        p += len;
        if (cp >= 0x10000) {
            if (out && n + 2 <= cap) {
                uint32_t v = cp - 0x10000;
                out[n] = (uint16_t)(0xD800 + (v >> 10));
                out[n + 1] = (uint16_t)(0xDC00 + (v & 0x3FF));
            }
            n += 2;
        } else {
            if (out && n < cap) out[n] = (uint16_t)cp;
            n++;
        }
    }
    return n;
}

/* Number of UTF-16 code units the UTF-8 string needs. */
static inline uint32_t arsc_utf16_length(const char *s) {
    return arsc_utf8_to_utf16(s, NULL, 0);
}

/* Number of bytes the UTF-8 string needs. */
static inline uint32_t arsc_utf8_length(const char *s) {
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)(s ? s : "");
    while (*p++) n++;
    return n;
}

/*
 * Decodes UTF-16 code units into a NUL terminated UTF-8 string.
 * `out_sz` counts the terminator. Returns the number of bytes written.
 */
static inline uint32_t arsc_utf16_to_utf8(const uint16_t *units, uint32_t count,
                                          char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    char *o = out;
    uint32_t i = 0;
    while (i < count) {
        uint32_t wc = units[i++];
        if (wc >= 0xD800 && wc <= 0xDBFF && i < count) {
            uint32_t lo = units[i];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                wc = 0x10000 + ((wc - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        char tmp[4];
        uint32_t n = 0;
        if (wc < 0x80) {
            tmp[n++] = (char)wc;
        } else if (wc < 0x800) {
            tmp[n++] = (char)(0xC0 | (wc >> 6));
            tmp[n++] = (char)(0x80 | (wc & 0x3F));
        } else if (wc < 0x10000) {
            tmp[n++] = (char)(0xE0 | (wc >> 12));
            tmp[n++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            tmp[n++] = (char)(0x80 | (wc & 0x3F));
        } else {
            tmp[n++] = (char)(0xF0 | (wc >> 18));
            tmp[n++] = (char)(0x80 | ((wc >> 12) & 0x3F));
            tmp[n++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            tmp[n++] = (char)(0x80 | (wc & 0x3F));
        }
        if ((size_t)(o - out) + n + 1 > out_sz) break;
        for (uint32_t k = 0; k < n; k++) *o++ = tmp[k];
    }
    *o = 0;
    return (uint32_t)(o - out);
}

#endif
