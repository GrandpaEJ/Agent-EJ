#include "format_arsc_write.h"
#include "format_arsc_fmt.h"
#include <stdlib.h>
#include <string.h>

#define RES_STRING_POOL_TYPE 0x0001
#define POOL_HEADER_SIZE 28

/* Number of bytes the length prefix of a pool entry needs. */
static uint32_t prefix_len(uint32_t value) {
    return value > 0x7F ? 2 : 1;
}

/* Size of one string inside the pool data area. */
static uint32_t pool_entry_size(const char *s, int utf8) {
    if (utf8) {
        uint32_t u16len = arsc_utf16_length(s);
        uint32_t u8len = arsc_utf8_length(s);
        return prefix_len(u16len) + prefix_len(u8len) + u8len + 1;
    }
    return 2 + arsc_utf16_length(s) * 2 + 2;
}

/* Writes the length prefix AOSP uses for UTF-8 pool entries. */
static int write_prefix8(arsc_buf *b, uint32_t value) {
    if (value > 0x7F) {
        if (arsc_buf_u8(b, (uint8_t)(((value >> 8) & 0x7F) | 0x80)) != 0) return -1;
    }
    return arsc_buf_u8(b, (uint8_t)(value & 0xFF));
}

/* Writes one string into the pool data area. */
static int write_pool_string(arsc_buf *b, const char *s, int utf8) {
    if (utf8) {
        uint32_t u16len = arsc_utf16_length(s);
        uint32_t u8len = arsc_utf8_length(s);
        if (write_prefix8(b, u16len) != 0) return -1;
        if (write_prefix8(b, u8len) != 0) return -1;
        if (arsc_buf_append(b, s, u8len) != 0) return -1;
        return arsc_buf_u8(b, 0);
    }
    uint32_t len = arsc_utf16_length(s);
    if (arsc_buf_u16(b, (uint16_t)len) != 0) return -1;
    uint16_t *units = malloc((len + 1) * sizeof(uint16_t));
    if (!units) return -1;
    arsc_utf8_to_utf16(s, units, len);
    for (uint32_t i = 0; i < len; i++) {
        if (arsc_buf_u16(b, units[i]) != 0) { free(units); return -1; }
    }
    free(units);
    return arsc_buf_u16(b, 0);
}

int arsc_write_pool(arsc_buf *b, char *const *strings, uint32_t count, int utf8) {
    size_t start = b->len;
    uint32_t offsets_size = count * 4;

    if (arsc_buf_u16(b, RES_STRING_POOL_TYPE) != 0) return -1;
    if (arsc_buf_u16(b, POOL_HEADER_SIZE) != 0) return -1;
    if (arsc_buf_u32(b, 0) != 0) return -1;                       /* size, patched later */
    if (arsc_buf_u32(b, count) != 0) return -1;
    if (arsc_buf_u32(b, 0) != 0) return -1;                       /* styleCount */
    if (arsc_buf_u32(b, utf8 ? (1u << 8) : 0u) != 0) return -1;   /* flags */
    if (arsc_buf_u32(b, POOL_HEADER_SIZE + offsets_size) != 0) return -1; /* stringsStart */
    if (arsc_buf_u32(b, 0) != 0) return -1;                       /* stylesStart */

    uint32_t off = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (arsc_buf_u32(b, off) != 0) return -1;
        off += pool_entry_size(strings[i], utf8);
    }
    for (uint32_t i = 0; i < count; i++) {
        if (write_pool_string(b, strings[i], utf8) != 0) return -1;
    }
    if (arsc_buf_align4(b) != 0) return -1;
    if (arsc_buf_patch32(b, start + 4, (uint32_t)(b->len - start)) != 0) return -1;
    return 0;
}
