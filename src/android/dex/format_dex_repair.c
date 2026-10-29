/* Repairs common "anti-dex" tampering — structural lies planted in a DEX image
 * to crash or confuse decompilers (jadx, baksmali) while ART ignores them.
 *
 * Only safe, always-correct header normalisations are applied; nothing that
 * could lose real data:
 *   - header_size forced to the canonical 0x70 (fake large values break jadx);
 *   - endian_tag forced to little-endian 0x12345678;
 *   - file_size corrected to the true image length;
 *   - checksum (Adler-32 over bytes after it) recomputed;
 *   - signature (SHA-1 over bytes after it) recomputed;
 *   - an unsupported version digit in the magic (e.g. a fake 036/041) rewritten
 *     to 035, the universally accepted baseline.
 * The section tables themselves are left untouched: de-inflating a lied-about
 * *_ids_size could drop genuine entries, so that is out of scope here. */
#include "formats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void sha1(const void *data, size_t len, uint8_t *out);
extern uint32_t adler32(const uint8_t *data, size_t len);

#define DEX_HEADER_SIZE 0x70u
#define DEX_ENDIAN_TAG  0x12345678u

static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void w32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

/* Appends one line to the change log, never overflowing it. */
static void note(char *log, size_t cap, const char *msg) {
    if (!log || !cap) return;
    size_t len = strlen(log);
    if (len + 1 >= cap) return;
    snprintf(log + len, cap - len, "%s%s", len ? "; " : "", msg);
}

uint8_t *dex_repair(const uint8_t *in, size_t size, size_t *out_size,
                    char *changes, size_t changes_cap) {
    if (!in || size < DEX_HEADER_SIZE || !out_size) return NULL;
    if (changes && changes_cap) changes[0] = '\0';

    /* Must look like a DEX magic "dex\n", otherwise there is nothing to do. */
    if (in[0] != 'd' || in[1] != 'e' || in[2] != 'x' || in[3] != '\n')
        return NULL;

    uint8_t *out = malloc(size);
    if (!out) return NULL;
    memcpy(out, in, size);

    /* Version digits live at bytes 4..6, then a NUL at byte 7. ART accepts
     * 035/037/038/039/040; a value outside that set is a decompiler trap. */
    int ver = 0, digits_ok = 1;
    for (int i = 4; i < 7; i++) {
        if (out[i] < '0' || out[i] > '9') { digits_ok = 0; break; }
        ver = ver * 10 + (out[i] - '0');
    }
    if (out[7] != 0x00) { out[7] = 0x00; note(changes, changes_cap, "magic NUL restored"); }
    if (!digits_ok ||
        (ver != 35 && ver != 37 && ver != 38 && ver != 39 && ver != 40)) {
        out[4] = '0'; out[5] = '3'; out[6] = '5';
        note(changes, changes_cap, "magic version reset to 035");
    }

    if (r32(out + 0x24) != DEX_HEADER_SIZE) {
        w32(out + 0x24, DEX_HEADER_SIZE);
        note(changes, changes_cap, "header_size reset to 0x70");
    }
    if (r32(out + 0x28) != DEX_ENDIAN_TAG) {
        w32(out + 0x28, DEX_ENDIAN_TAG);
        note(changes, changes_cap, "endian_tag reset to little-endian");
    }
    if (r32(out + 0x20) != (uint32_t)size) {
        w32(out + 0x20, (uint32_t)size);
        note(changes, changes_cap, "file_size corrected");
    }

    /* signature = SHA-1 over everything after the 20-byte signature field
     * (i.e. from byte 0x20 to EOF); recompute and compare. */
    uint8_t sig[20];
    sha1(out + 0x20, size - 0x20, sig);
    if (memcmp(out + 0x0C, sig, 20) != 0) {
        memcpy(out + 0x0C, sig, 20);
        note(changes, changes_cap, "signature recomputed");
    }

    /* checksum = Adler-32 over everything after the 4-byte checksum field
     * (from byte 0x0C to EOF). Must follow the signature fix above. */
    uint32_t sum = adler32(out + 0x0C, size - 0x0C);
    if (r32(out + 0x08) != sum) {
        w32(out + 0x08, sum);
        note(changes, changes_cap, "checksum recomputed");
    }

    if (changes && changes_cap && changes[0] == '\0')
        note(changes, changes_cap, "no tampering found");
    *out_size = size;
    return out;
}
