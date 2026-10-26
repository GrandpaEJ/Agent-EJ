#include "format_arsc_write.h"
#include "format_arsc_fmt.h"
#include <string.h>

#define RES_TABLE_TYPE_TYPE      0x0201
#define RES_TABLE_TYPE_SPEC_TYPE 0x0202

/* Bytes one entry occupies in the entry data area. */
static uint32_t entry_size(const arsc_model_entry *e) {
    if (!e->present) return 0;
    if (e->flags & ARSC_ENTRY_COMPLEX) return 16 + e->map_count * 12;
    return 16;
}

/* Writes one ResTable_map_entry with its map items. */
static int write_complex_entry(arsc_buf *b, const arsc_model_entry *e) {
    if (arsc_buf_u16(b, 16) != 0) return -1;
    if (arsc_buf_u16(b, (uint16_t)(e->flags | ARSC_ENTRY_COMPLEX)) != 0) return -1;
    if (arsc_buf_u32(b, e->key) != 0) return -1;
    if (arsc_buf_u32(b, e->parent) != 0) return -1;
    if (arsc_buf_u32(b, e->map_count) != 0) return -1;
    for (uint32_t i = 0; i < e->map_count; i++) {
        if (arsc_buf_u32(b, e->map[i].name) != 0) return -1;
        if (arsc_buf_u16(b, e->map[i].size ? e->map[i].size : 8) != 0) return -1;
        if (arsc_buf_u8(b, e->map[i].res0) != 0) return -1;
        if (arsc_buf_u8(b, e->map[i].data_type) != 0) return -1;
        if (arsc_buf_u32(b, e->map[i].data) != 0) return -1;
    }
    return 0;
}

/* Writes one ResTable_entry followed by its Res_value. */
static int write_simple_entry(arsc_buf *b, const arsc_model_entry *e) {
    if (arsc_buf_u16(b, 8) != 0) return -1;
    if (arsc_buf_u16(b, (uint16_t)(e->flags & ~ARSC_ENTRY_COMPLEX)) != 0) return -1;
    if (arsc_buf_u32(b, e->key) != 0) return -1;
    if (arsc_buf_u16(b, 8) != 0) return -1;
    if (arsc_buf_u8(b, 0) != 0) return -1;
    if (arsc_buf_u8(b, e->data_type) != 0) return -1;
    if (arsc_buf_u32(b, e->data) != 0) return -1;
    return 0;
}

int arsc_write_type_spec(arsc_buf *b, const arsc_model_type *t) {
    size_t start = b->len;
    if (arsc_buf_u16(b, RES_TABLE_TYPE_SPEC_TYPE) != 0) return -1;
    if (arsc_buf_u16(b, 16) != 0) return -1;
    if (arsc_buf_u32(b, 0) != 0) return -1;
    if (arsc_buf_u8(b, t->id) != 0) return -1;
    if (arsc_buf_u8(b, t->res0) != 0) return -1;
    if (arsc_buf_u16(b, t->res1) != 0) return -1;
    if (arsc_buf_u32(b, t->entry_count) != 0) return -1;
    for (uint32_t i = 0; i < t->entry_count; i++) {
        uint32_t fl = t->spec_flags ? t->spec_flags[i] : 0;
        if (arsc_buf_u32(b, fl) != 0) return -1;
    }
    if (arsc_buf_patch32(b, start + 4, (uint32_t)(b->len - start)) != 0) return -1;
    return 0;
}

int arsc_write_type(arsc_buf *b, const arsc_model_type *t, const arsc_model_variant *v) {
    size_t start = b->len;
    uint32_t header_size = 20 + (v->config_size ? v->config_size : 0);
    uint32_t entries_start = header_size + t->entry_count * 4;

    if (arsc_buf_u16(b, RES_TABLE_TYPE_TYPE) != 0) return -1;
    if (arsc_buf_u16(b, (uint16_t)header_size) != 0) return -1;
    if (arsc_buf_u32(b, 0) != 0) return -1;
    if (arsc_buf_u8(b, t->id) != 0) return -1;
    if (arsc_buf_u8(b, v->flags) != 0) return -1;
    if (arsc_buf_u16(b, v->reserved) != 0) return -1;
    if (arsc_buf_u32(b, t->entry_count) != 0) return -1;
    if (arsc_buf_u32(b, entries_start) != 0) return -1;
    if (v->config_size) {
        if (arsc_buf_append(b, v->config, v->config_size) != 0) return -1;
    }

    /* The offsets are relative to the start of the entry data area. */
    uint32_t off = 0;
    for (uint32_t i = 0; i < t->entry_count; i++) {
        uint32_t size = (i < v->entry_count) ? entry_size(&v->entries[i]) : 0;
        if (arsc_buf_u32(b, size ? off : 0xFFFFFFFFu) != 0) return -1;
        off += size;
    }
    for (uint32_t i = 0; i < t->entry_count && i < v->entry_count; i++) {
        const arsc_model_entry *e = &v->entries[i];
        if (!e->present) continue;
        if (e->flags & ARSC_ENTRY_COMPLEX) {
            if (write_complex_entry(b, e) != 0) return -1;
        } else {
            if (write_simple_entry(b, e) != 0) return -1;
        }
    }
    if (arsc_buf_patch32(b, start + 4, (uint32_t)(b->len - start)) != 0) return -1;
    return 0;
}
