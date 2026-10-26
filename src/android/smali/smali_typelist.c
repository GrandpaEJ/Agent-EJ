#include "smali_typelist.h"
#include "smali_pool.h"
#include <stdlib.h>
#include <string.h>

void smali_typelist_reg_init(smali_typelist_reg_t *reg) {
    memset(reg, 0, sizeof(*reg));
}

void smali_typelist_reg_free(smali_typelist_reg_t *reg) {
    for (uint32_t i = 0; i < reg->count; i++) free(reg->items[i].types);
    free(reg->items);
    memset(reg, 0, sizeof(*reg));
}

/* Compares a candidate list with a list that is already written. */
static int typelist_equal(const smali_typelist_t *item, const uint16_t *types, uint32_t count) {
    if (item->count != count) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (item->types[i] != types[i]) return 0;
    return 1;
}

uint32_t smali_typelist_write(smali_typelist_reg_t *reg, smali_buf_t *buf,
                               const uint16_t *types, uint32_t count) {
    if (count == 0) return 0;

    for (uint32_t i = 0; i < reg->count; i++) {
        if (typelist_equal(&reg->items[i], types, count))
            return reg->items[i].offset;
    }

    align_4(buf);
    uint32_t offset = buf->len;
    buf_write_u32(buf, count);
    for (uint32_t i = 0; i < count; i++)
        buf_write_u16(buf, types[i]);

    if (reg->count >= reg->cap) {
        reg->cap = reg->cap ? reg->cap * 2 : 16;
        reg->items = realloc(reg->items, reg->cap * sizeof(smali_typelist_t));
    }
    smali_typelist_t *item = &reg->items[reg->count++];
    item->offset = offset;
    item->count = count;
    item->types = malloc(count * sizeof(uint16_t));
    memcpy(item->types, types, count * sizeof(uint16_t));
    if (reg->count == 1) reg->first_offset = offset;
    return offset;
}

uint32_t smali_typelist_from_proto(smali_pool_strings_t *types_pool,
                                   const char *sig, uint16_t **out_types) {
    uint32_t cap = 16, count = 0;
    uint16_t *types = malloc(cap * sizeof(uint16_t));
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *type_start = p;
        if (*p == 'L') {
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
            else p++;
        } else {
            p++;
        }
        char *type_str = strndup(type_start, (size_t)(p - type_start));
        if (count >= cap) {
            cap *= 2;
            types = realloc(types, cap * sizeof(uint16_t));
        }
        types[count++] = (uint16_t)smali_pool_find(types_pool, type_str);
        free(type_str);
    }
    *out_types = types;
    return count;
}

uint32_t smali_typelist_from_descriptors(smali_pool_strings_t *types_pool,
                                         char *const *descriptors, uint32_t count,
                                         uint16_t **out_types) {
    uint16_t *types = malloc((count ? count : 1) * sizeof(uint16_t));
    for (uint32_t i = 0; i < count; i++)
        types[i] = (uint16_t)smali_pool_find(types_pool, descriptors[i]);
    *out_types = types;
    return count;
}
