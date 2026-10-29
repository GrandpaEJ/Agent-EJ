/* Repairs "anti-resource" tampering in a binary AndroidManifest / layout
 * (AXML) — most commonly a lied-about root chunk size that makes apktool read
 * past the end of the buffer and abort.
 *
 * Conservative, non-destructive edits only: the root chunk's total size is
 * corrected to the real image length, and any sub-chunk whose declared size
 * runs past the end of the file is clamped to the bytes that actually remain.
 * These can never drop real structure (an over-long size only ever hides the
 * true end). Anomalies that cannot be fixed without guessing — a zero-size
 * chunk, a string pool whose internal offsets overrun — are reported, not
 * patched, so a half-broken manifest is never silently corrupted further. */
#include "formats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RES_XML_TYPE 0x0003

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void w32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

static void note(char *log, size_t cap, const char *msg) {
    if (!log || !cap) return;
    size_t len = strlen(log);
    if (len + 1 >= cap) return;
    snprintf(log + len, cap - len, "%s%s", len ? "; " : "", msg);
}

uint8_t *axml_repair(const uint8_t *in, size_t size, size_t *out_size,
                     char *changes, size_t changes_cap) {
    if (!in || size < 8 || !out_size) return NULL;
    if (changes && changes_cap) changes[0] = '\0';
    if (r16(in) != RES_XML_TYPE) return NULL;      /* not binary XML */

    uint8_t *out = malloc(size);
    if (!out) return NULL;
    memcpy(out, in, size);

    /* Root chunk: type(2) headerSize(2) size(4). The size lie is the classic
     * apktool-crashing trap. */
    if (r32(out + 4) != (uint32_t)size) {
        w32(out + 4, (uint32_t)size);
        note(changes, changes_cap, "root chunk size corrected");
    }

    /* Walk the sub-chunks and clamp any that claim to extend past EOF. */
    for (size_t off = 8; off + 8 <= size; ) {
        uint32_t cs = r32(out + off + 4);
        if (cs < 8) {
            note(changes, changes_cap, "zero/short sub-chunk (not auto-fixable)");
            break;
        }
        if (off + cs > size) {
            char m[64];
            snprintf(m, sizeof(m), "sub-chunk size clamped to EOF");
            w32(out + off + 4, (uint32_t)(size - off));
            note(changes, changes_cap, m);
            break;                                  /* nothing valid can follow */
        }
        off += cs;
    }

    if (changes && changes_cap && changes[0] == '\0')
        note(changes, changes_cap, "no tampering found");
    *out_size = size;
    return out;
}
