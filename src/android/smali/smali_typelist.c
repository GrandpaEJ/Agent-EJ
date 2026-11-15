#include "smali_typelist.h"
#include "smali_pool.h"
#include <stdlib.h>
#include <string.h>

void smali_typelist_reg_init(smali_typelist_reg_t *reg) {
    smali_blobset_init(&reg->lists);
    reg->first_offset = 0;
}

void smali_typelist_reg_free(smali_typelist_reg_t *reg) {
    smali_blobset_free(&reg->lists);
}

uint32_t smali_typelist_add(smali_typelist_reg_t *reg, const uint16_t *types, uint32_t count) {
    if (count == 0) return UINT32_MAX;
    return smali_blobset_add(&reg->lists, (const uint8_t *)types, count * sizeof(uint16_t));
}

static uint16_t type_at(const smali_blobset_t *s, uint32_t id, uint32_t i) {
    uint16_t t;
    memcpy(&t, smali_blob_data(s, id) + i * sizeof(uint16_t), sizeof(t));
    return t;
}

/* dexlib2 compareAsIterable over the type names; type_ids are in name order. */
static int cmp_lists(const smali_blobset_t *s, uint32_t a, uint32_t b, void *arg) {
    (void)arg;
    uint32_t na = s->items[a].len / 2, nb = s->items[b].len / 2;
    for (uint32_t i = 0; i < na && i < nb; i++) {
        uint16_t x = type_at(s, a, i), y = type_at(s, b, i);
        if (x != y) return x < y ? -1 : 1;
    }
    return na < nb ? -1 : na > nb;
}

void smali_typelist_flush(smali_typelist_reg_t *reg, smali_buf_t *buf) {
    smali_blobset_t *s = &reg->lists;
    uint32_t *order = smali_blobset_sorted(s, cmp_lists, NULL);
    if (!order) return;
    align_4(buf);
    for (uint32_t k = 0; k < s->count; k++) {
        uint32_t id = order[k], n = s->items[id].len / 2;
        align_4(buf);
        s->items[id].off = buf->len;
        if (k == 0) reg->first_offset = buf->len;
        buf_write_u32(buf, n);
        for (uint32_t i = 0; i < n; i++) buf_write_u16(buf, type_at(s, id, i));
    }
    free(order);
}

uint32_t smali_typelist_offset(const smali_typelist_reg_t *reg, uint32_t id) {
    return id == UINT32_MAX ? 0 : reg->lists.items[id].off;
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
