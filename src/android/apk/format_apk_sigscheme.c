/* Which signature schemes an APK carries (the APK info dialog). v1 is a
 * META-INF/ .SF entry; v2/v3/v3.1 and the source stamp are ID-value pairs in
 * the APK Signing Block that sits right before the central directory. Only
 * the file tail and the block's pair headers are read. */
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EOCD_MAX (65535 + 22)

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

static int read_at(FILE *f, long off, void *buf, size_t n) {
    return off >= 0 && fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n ? 0 : -1;
}

/* Central directory offset from the End Of Central Directory record, or -1. */
static long cd_offset(FILE *f) {
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long size = ftell(f);
    long win = size < EOCD_MAX ? size : EOCD_MAX;
    uint8_t *buf = (uint8_t *)malloc((size_t)win);
    long cd = -1;
    if (buf && read_at(f, size - win, buf, (size_t)win) == 0)
        for (long i = win - 22; i >= 0; i--)
            if (le32(buf + i) == 0x06054b50u) { cd = le32(buf + i + 16); break; }
    free(buf);
    return cd;
}

static int block_schemes(FILE *f) {
    long cd = cd_offset(f);
    uint8_t foot[24];
    if (cd < 24 || read_at(f, cd - 24, foot, 24) != 0 || memcmp(foot + 8, "APK Sig Block 42", 16) != 0) return 0;
    uint64_t size = le64(foot);
    long start = cd - (long)size - 8;            /* the leading size field */
    if (size < 24 || start < 0) return 0;
    int bits = 0;
    for (long p = start + 8; p + 12 <= cd - 24;) {
        uint8_t h[12];
        if (read_at(f, p, h, 12) != 0) break;
        uint64_t len = le64(h);
        if (len < 4 || len > (uint64_t)(cd - 24 - p - 8)) break;
        switch (le32(h + 8)) {
        case 0x7109871au: bits |= APK_SIG_V2; break;
        case 0xf05368c0u: bits |= APK_SIG_V3; break;
        case 0x1b93ad61u: bits |= APK_SIG_V31; break;
        case 0x6dff800du: case 0x2b09189eu: bits |= APK_SIG_STAMP; break;
        }
        p += 8 + (long)len;
    }
    return bits;
}

static int has_v1(const char *path) {
    zip_archive *za = zip_open(path);
    if (!za) return 0;
    int v1 = 0, n = zip_get_num_entries(za);
    for (int i = 0; i < n && !v1; i++) {
        const char *e = zip_get_entry_name(za, i);
        size_t l = e ? strlen(e) : 0;
        v1 = l > 12 && strncmp(e, "META-INF/", 9) == 0 && strcmp(e + l - 3, ".SF") == 0;
    }
    zip_close(za);
    return v1;
}

int apk_sig_schemes(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int bits = block_schemes(f);
    fclose(f);
    return bits | (has_v1(path) ? APK_SIG_V1 : 0);
}
