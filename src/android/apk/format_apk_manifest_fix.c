/* Neutralises the split requirements in a base APK's binary AndroidManifest so
 * the merged, standalone APK installs without its configuration splits.
 *
 * Only two attributes gate installation by PackageManager:
 *   - requiredSplitTypes (a string listing the split types the base demands) —
 *     cleared to the empty string, exactly as a split-less build emits it;
 *   - isSplitRequired (a boolean) — forced false.
 * The edits are made in place on a copy of the chunk and never change its size
 * (only string-pool indices and a typed value are rewritten), so the manifest's
 * structure and every other attribute are left byte-for-byte untouched. The
 * com.android.vending.splits* metadata is read by the Play Store, not by
 * install, so it is left alone. */
#include "formats.h"
#include <stdlib.h>
#include <string.h>

#define XML_STRING_POOL   0x0001
#define XML_START_ELEMENT 0x0102
#define XML_TYPE_STRING   0x03
#define XML_TYPE_BOOLEAN  0x12

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}
static void w16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void w32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

/* Decodes string pool entry `idx` into `out` (UTF-8 result, best effort). */
static int pool_str(const uint8_t *pool, uint32_t pool_sz, uint32_t idx,
                    char *out, size_t cap) {
    if (pool_sz < 28) return -1;
    uint32_t count = r32(pool + 8);
    uint32_t flags = r32(pool + 16);
    uint32_t strings_start = r32(pool + 20);
    int utf8 = (flags & (1u << 8)) != 0;
    if (idx >= count) return -1;
    uint32_t off = r32(pool + 28 + idx * 4);
    uint32_t base = strings_start + off;
    if (base + 2 > pool_sz) return -1;
    const uint8_t *s = pool + base;
    size_t n;
    if (utf8) {
        /* [char-len][byte-len] each 1 or 2 bytes; byte-len governs the data. */
        s += (s[0] & 0x80) ? 2 : 1;          /* skip char count */
        n = (*s & 0x80) ? (((size_t)(s[0] & 0x7F) << 8) | s[1]) : *s;
        s += (*s & 0x80) ? 2 : 1;            /* skip byte count */
        if (base + (size_t)(s - (pool + base)) + n > pool_sz) return -1;
        if (n >= cap) n = cap - 1;
        memcpy(out, s, n);
        out[n] = 0;
    } else {
        size_t len = r16(s);
        s += (len & 0x8000) ? 4 : 2;         /* long form uses two u16 */
        if (len & 0x8000) len = ((len & 0x7FFF) << 16) | r16(pool + base + 2);
        size_t j = 0;
        for (size_t i = 0; i < len && j + 1 < cap; i++) out[j++] = (char)s[i * 2];
        out[j] = 0;
    }
    return 0;
}

/* Finds the index of the empty string in the pool, or -1. */
static int32_t pool_empty_index(const uint8_t *pool, uint32_t pool_sz) {
    uint32_t count = r32(pool + 8);
    char buf[8];
    for (uint32_t i = 0; i < count; i++)
        if (pool_str(pool, pool_sz, i, buf, sizeof(buf)) == 0 && buf[0] == 0)
            return (int32_t)i;
    return -1;
}

uint8_t *apk_fix_split_manifest(const uint8_t *axml, size_t size, size_t *out_size) {
    if (!axml || size < 8 || r16(axml) != 0x0003) return NULL;
    uint8_t *out = malloc(size);
    if (!out) return NULL;
    memcpy(out, axml, size);

    /* Locate the string pool (first chunk after the 8-byte XML header). */
    const uint8_t *pool = NULL;
    uint32_t pool_sz = 0;
    for (size_t off = 8; off + 8 <= size; ) {
        uint16_t ct = r16(out + off);
        uint32_t cs = r32(out + off + 4);
        if (cs < 8 || off + cs > size) break;
        if (ct == XML_STRING_POOL) { pool = out + off; pool_sz = cs; break; }
        off += cs;
    }
    if (!pool) { *out_size = size; return out; }
    int32_t empty = pool_empty_index(pool, pool_sz);

    /* Walk the element chunks and patch the two split attributes. */
    char name[64];
    for (size_t off = 8; off + 8 <= size; ) {
        uint16_t ct = r16(out + off);
        uint32_t cs = r32(out + off + 4);
        if (cs < 8 || off + cs > size) break;
        if (ct == XML_START_ELEMENT && cs >= 36) {
            /* ResXMLTree_node: header(8) line(4) comment(4); then attrExt at
             * +16: ns(4) name(4) attributeStart(2) attributeSize(2) count(2). */
            uint8_t *el = out + off;
            uint16_t attr_start = r16(el + 24);
            uint16_t attr_size = r16(el + 26);
            uint16_t attr_count = r16(el + 28);
            if (!attr_size) attr_size = 20;
            for (uint16_t a = 0; a < attr_count; a++) {
                size_t ao = off + 16 + attr_start + (size_t)a * attr_size;
                if (ao + 20 > off + cs) break;
                uint8_t *at = out + ao;
                uint32_t nm = r32(at + 4);
                if (pool_str(pool, pool_sz, nm, name, sizeof(name)) != 0) continue;
                if (strcmp(name, "requiredSplitTypes") == 0 && empty >= 0) {
                    w32(at + 8, (uint32_t)empty);          /* rawValue */
                    w16(at + 12, 8);                       /* value size */
                    at[15] = XML_TYPE_STRING;              /* dataType */
                    w32(at + 16, (uint32_t)empty);         /* data */
                } else if (strcmp(name, "isSplitRequired") == 0) {
                    w32(at + 8, 0xFFFFFFFFu);              /* rawValue: none */
                    w16(at + 12, 8);
                    at[15] = XML_TYPE_BOOLEAN;
                    w32(at + 16, 0);                       /* false */
                }
            }
        }
        off += cs;
    }
    *out_size = size;
    return out;
}
