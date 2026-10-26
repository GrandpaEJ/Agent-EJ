#include "smali_writer.h"
#include "smali_code.h"
#include "smali_value.h"
#include "smali_buf.h"
#include "smali_pool.h"
#include "smali_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void write_map_item(smali_buf_t *b, uint16_t type, uint32_t size, uint32_t offset) {
    buf_write_u16(b, type);
    buf_write_u16(b, 0);
    buf_write_u32(b, size);
    buf_write_u32(b, offset);
}

typedef struct {
    uint16_t type;
    uint32_t size;
    uint32_t offset;
} map_item_entry_t;

/* Decode one MUTF-8 sequence at up[j].
 * Returns the number of source bytes consumed and stores the number of
 * UTF-16 code units the sequence encodes to in *units.
 * Truncated or malformed sequences fall back to a single byte so that the
 * size counter and the encoder always agree on how many units a string has
 * (DEX constraint G15 requires utf16_size to match the decoded length). */
static size_t mutf8_decode(const uint8_t *up, size_t raw_len, size_t j, size_t *units) {
    if ((up[j] & 0x80) == 0) {
        *units = 1;
        return 1;
    }
    if ((up[j] & 0xE0) == 0xC0) {
        if (j + 1 < raw_len && (up[j + 1] & 0xC0) == 0x80) {
            *units = 1;
            return 2;
        }
    } else if ((up[j] & 0xF0) == 0xE0) {
        if (j + 2 < raw_len && (up[j + 1] & 0xC0) == 0x80 && (up[j + 2] & 0xC0) == 0x80) {
            *units = 1;
            return 3;
        }
    } else if ((up[j] & 0xF8) == 0xF0) {
        if (j + 3 < raw_len && (up[j + 1] & 0xC0) == 0x80 &&
            (up[j + 2] & 0xC0) == 0x80 && (up[j + 3] & 0xC0) == 0x80) {
            *units = 2; /* supplementary char = 2 UTF-16 code units */
            return 4;
        }
    }
    /* malformed or truncated: emit the byte as-is, one unit */
    *units = 1;
    return 1;
}

static int comp_u32_tl(const void *a, const void *b) {
    uint32_t ua = *(const uint32_t *)a;
    uint32_t ub = *(const uint32_t *)b;
    return (ua > ub) - (ua < ub);
}

static int comp_map_entries(const void *a, const void *b) {
    const map_item_entry_t *ma = (const map_item_entry_t *)a;
    const map_item_entry_t *mb = (const map_item_entry_t *)b;
    if (ma->offset != mb->offset) {
        return (ma->offset < mb->offset) ? -1 : 1;
    }
    return 0;
}

static smali_ctx_def_t *s_write_ctx = NULL;
static const char *s_current_class_desc = NULL;

static int comp_class_fields(const void *a, const void *b) {
    const smali_field_def_t *fa = (const smali_field_def_t *)a;
    const smali_field_def_t *fb = (const smali_field_def_t *)b;
    char keyA[4096], keyB[4096];
    snprintf(keyA, sizeof(keyA), "%s->%s:%s", s_current_class_desc, fa->name, fa->type);
    snprintf(keyB, sizeof(keyB), "%s->%s:%s", s_current_class_desc, fb->name, fb->type);
    uint32_t idxA = smali_pool_find(&s_write_ctx->fields, keyA);
    uint32_t idxB = smali_pool_find(&s_write_ctx->fields, keyB);
    return (idxA < idxB) ? -1 : 1;
}

static int comp_class_methods(const void *a, const void *b) {
    const smali_method_def_t *ma = (const smali_method_def_t *)a;
    const smali_method_def_t *mb = (const smali_method_def_t *)b;
    char keyA[4096], keyB[4096];
    snprintf(keyA, sizeof(keyA), "%s->%s%s", s_current_class_desc, ma->name, ma->signature);
    snprintf(keyB, sizeof(keyB), "%s->%s%s", s_current_class_desc, mb->name, mb->signature);
    uint32_t idxA = smali_pool_find(&s_write_ctx->methods, keyA);
    uint32_t idxB = smali_pool_find(&s_write_ctx->methods, keyB);
    return (idxA < idxB) ? -1 : 1;
}

int write_assembled_dex(smali_ctx_def_t *ctx, const char *out_dex) {
    s_write_ctx = ctx;
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        s_current_class_desc = c->descriptor;
        if (c->static_field_count > 1) {
            qsort(c->static_fields, c->static_field_count, sizeof(smali_field_def_t), comp_class_fields);
        }
        if (c->instance_field_count > 1) {
            qsort(c->instance_fields, c->instance_field_count, sizeof(smali_field_def_t), comp_class_fields);
        }
        if (c->direct_method_count > 1) {
            qsort(c->direct_methods, c->direct_method_count, sizeof(smali_method_def_t), comp_class_methods);
        }
        if (c->virtual_method_count > 1) {
            qsort(c->virtual_methods, c->virtual_method_count, sizeof(smali_method_def_t), comp_class_methods);
        }
    }
    s_write_ctx = NULL;
    s_current_class_desc = NULL;

    smali_buf_t b; buf_init(&b);

    uint32_t string_count = ctx->strings.count;
    uint32_t type_count = ctx->types.count;
    uint32_t proto_count = ctx->protos.count;
    uint32_t field_count = ctx->fields.count;
    uint32_t method_count = ctx->methods.count;
    uint32_t class_count = ctx->class_count;

    uint32_t header_size = 0x70;
    uint32_t string_ids_off = header_size;
    uint32_t type_ids_off = string_ids_off + string_count * 4;
    uint32_t proto_ids_off = type_ids_off + type_count * 4;
    uint32_t field_ids_off = proto_ids_off + proto_count * 12;
    uint32_t method_ids_off = field_ids_off + field_count * 8;
    uint32_t class_defs_off = method_ids_off + method_count * 8;
    uint32_t data_off = class_defs_off + class_count * 32;

    /* DEX constraint G7: for every section the size and offset header fields
     * must be both zero or both non-zero. The layout offsets above still chain
     * so the sections stay contiguous, but the values reported in the header
     * and the map list must be zero when the section is empty. */
    uint32_t hdr_string_ids_off = string_count ? string_ids_off : 0;
    uint32_t hdr_type_ids_off   = type_count ? type_ids_off : 0;
    uint32_t hdr_proto_ids_off  = proto_count ? proto_ids_off : 0;
    uint32_t hdr_field_ids_off  = field_count ? field_ids_off : 0;
    uint32_t hdr_method_ids_off = method_count ? method_ids_off : 0;
    uint32_t hdr_class_defs_off = class_count ? class_defs_off : 0;

    if (data_off > b.cap) {
        while (data_off > b.cap) b.cap *= 2;
        b.buf = realloc(b.buf, b.cap);
    }
    b.len = data_off;
    memset(b.buf, 0, b.cap);

    uint32_t *string_offsets = malloc(string_count * 4);
    for (uint32_t i = 0; i < string_count; i++) {
        string_offsets[i] = b.len;
        const char *str = ctx->strings.strings[i];
        const uint8_t *up = (const uint8_t *)str;
        size_t raw_len = strlen(str);
        // DEX spec: utf16_size is the number of UTF-16 code units, not MUTF-8 byte length
        size_t utf16_size = 0;
        for (size_t j = 0; j < raw_len; ) {
            size_t units = 0;
            j += mutf8_decode(up, raw_len, j, &units);
            utf16_size += units;
        }
        buf_write_uleb128(&b, utf16_size);
        for (size_t j = 0; j < raw_len; ) {
            size_t units = 0;
            size_t n = mutf8_decode(up, raw_len, j, &units);
            if (n == 1 && (up[j] & 0x80)) {
                /* malformed or truncated sequence: keep the raw byte */
                buf_write_u8(&b, up[j]);
            } else if (n == 2) {
                buf_write_u8(&b, up[j]);
                buf_write_u8(&b, up[j + 1]);
            } else if (n == 3) {
                buf_write_u8(&b, up[j]);
                buf_write_u8(&b, up[j + 1]);
                buf_write_u8(&b, up[j + 2]);
            } else if (n == 4) {
                // 4-byte UTF-8 needs to be converted to surrogate pairs (2x 3-byte MUTF-8)
                uint32_t codepoint = ((up[j] & 0x07) << 18) | ((up[j + 1] & 0x3F) << 12) | ((up[j + 2] & 0x3F) << 6) | (up[j + 3] & 0x3F);
                uint32_t u = codepoint - 0x10000;
                uint16_t high = (u >> 10) + 0xD800;
                uint16_t low = (u & 0x3FF) + 0xDC00;
                
                // High surrogate to 3-byte MUTF-8
                buf_write_u8(&b, 0xE0 | ((high >> 12) & 0x0F));
                buf_write_u8(&b, 0x80 | ((high >> 6) & 0x3F));
                buf_write_u8(&b, 0x80 | (high & 0x3F));
                
                // Low surrogate to 3-byte MUTF-8
                buf_write_u8(&b, 0xE0 | ((low >> 12) & 0x0F));
                buf_write_u8(&b, 0x80 | ((low >> 6) & 0x3F));
                buf_write_u8(&b, 0x80 | (low & 0x3F));
            } else {
                buf_write_u8(&b, up[j]);
            }
            j += n;
        }
        // DEX spec: string_data_item must be null-terminated
        buf_write_u8(&b, 0);
    }
    align_4(&b);

    uint32_t *proto_param_offsets = malloc(proto_count * 4);
    for (uint32_t i = 0; i < proto_count; i++) {
        const char *sig = ctx->protos.strings[i];
        uint32_t pcount = 0;
        uint32_t ptypes[256];
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
            } else { p++; }
            char *type_str = strndup(type_start, p - type_start);
            if (pcount < 256) ptypes[pcount] = smali_pool_find(&ctx->types, type_str);
            pcount++;
            free(type_str);
        }
        if (pcount > 0) {
            uint32_t existing_offset = 0;
            for (uint32_t prev = 0; prev < i; prev++) {
                if (proto_param_offsets[prev] != 0) {
                    uint32_t off = proto_param_offsets[prev];
                    uint32_t prev_count = *(uint32_t*)(b.buf + off);
                    if (prev_count == pcount) {
                        int match = 1;
                        uint16_t *prev_types = (uint16_t*)(b.buf + off + 4);
                        for (uint32_t k = 0; k < pcount; k++) {
                            if (prev_types[k] != ptypes[k]) {
                                match = 0;
                                break;
                            }
                        }
                        if (match) {
                            existing_offset = off;
                            break;
                        }
                    }
                }
            }

            if (existing_offset != 0) {
                proto_param_offsets[i] = existing_offset;
            } else {
                align_4(&b);
                proto_param_offsets[i] = b.len;
                buf_write_u32(&b, pcount);
                for (uint32_t j = 0; j < pcount; j++) {
                    buf_write_u16(&b, ptypes[j]);
                }
            }
        } else {
            proto_param_offsets[i] = 0;
        }
    }
    align_4(&b);

    uint32_t *interfaces_offsets = malloc(class_count * 4);
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        if (c->interface_count > 0) {
            align_4(&b);
            interfaces_offsets[i] = b.len;
            buf_write_u32(&b, c->interface_count);
            for (uint32_t j = 0; j < c->interface_count; j++) {
                uint32_t type_idx = smali_pool_find(&ctx->types, c->interfaces[j]);
                buf_write_u16(&b, (uint16_t)type_idx);
            }
        } else {
            interfaces_offsets[i] = 0;
        }
    }
    align_4(&b);

    uint32_t **direct_code_offsets = malloc(class_count * sizeof(uint32_t *));
    uint32_t **virtual_code_offsets = malloc(class_count * sizeof(uint32_t *));
    uint32_t **direct_code_ends = malloc(class_count * sizeof(uint32_t *));
    uint32_t **virtual_code_ends = malloc(class_count * sizeof(uint32_t *));
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        direct_code_offsets[i] = c->direct_method_count ? malloc(c->direct_method_count * sizeof(uint32_t)) : NULL;
        virtual_code_offsets[i] = c->virtual_method_count ? malloc(c->virtual_method_count * sizeof(uint32_t)) : NULL;
        direct_code_ends[i] = c->direct_method_count ? malloc(c->direct_method_count * sizeof(uint32_t)) : NULL;
        virtual_code_ends[i] = c->virtual_method_count ? malloc(c->virtual_method_count * sizeof(uint32_t)) : NULL;
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            if (c->direct_methods[j].access_flags & 0x0500) {
                direct_code_offsets[i][j] = 0;
                direct_code_ends[i][j] = b.len;
            } else if (write_code_item(ctx, &b, &c->direct_methods[j], &direct_code_offsets[i][j]) != 0) {
                direct_code_ends[i][j] = b.len;
                goto code_write_fail;
            } else {
                direct_code_ends[i][j] = b.len;
            }
        }
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            if (c->virtual_methods[j].access_flags & 0x0500) {
                virtual_code_offsets[i][j] = 0;
                virtual_code_ends[i][j] = b.len;
            } else if (write_code_item(ctx, &b, &c->virtual_methods[j], &virtual_code_offsets[i][j]) != 0) {
                virtual_code_ends[i][j] = b.len;
                goto code_write_fail;
            } else {
                virtual_code_ends[i][j] = b.len;
            }
        }
    }
    uint32_t code_section_end = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c2 = &ctx->classes[i];
        for (uint32_t j = 0; j < c2->direct_method_count; j++)
            if (direct_code_ends[i] && direct_code_ends[i][j] > code_section_end) code_section_end = direct_code_ends[i][j];
        for (uint32_t j = 0; j < c2->virtual_method_count; j++)
            if (virtual_code_ends[i] && virtual_code_ends[i][j] > code_section_end) code_section_end = virtual_code_ends[i][j];
    }
    align_4(&b);
    if (code_section_end > 0 && b.len > code_section_end) {
        memset(b.buf + code_section_end, 0, b.len - code_section_end);
    }
    align_4(&b);

    uint32_t *class_data_offsets = malloc(class_count * 4);
    align_4(&b);
    if (b.len < b.cap)
        memset(b.buf + b.len, 0, b.cap - b.len);

    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        class_data_offsets[i] = b.len;
        buf_write_uleb128(&b, c->static_field_count);
        buf_write_uleb128(&b, c->instance_field_count);
        buf_write_uleb128(&b, c->direct_method_count);
        buf_write_uleb128(&b, c->virtual_method_count);

        uint32_t last_idx = 0;
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            char key[4096]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
            uint32_t idx = smali_pool_find(&ctx->fields, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->static_fields[j].access_flags);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            char key[4096]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
            uint32_t idx = smali_pool_find(&ctx->fields, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->instance_fields[j].access_flags);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            char key[4096]; snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
            uint32_t idx = smali_pool_find(&ctx->methods, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->direct_methods[j].access_flags);
            buf_write_uleb128(&b, direct_code_offsets[i][j]);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            char key[4096]; snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
            uint32_t idx = smali_pool_find(&ctx->methods, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->virtual_methods[j].access_flags);
            buf_write_uleb128(&b, virtual_code_offsets[i][j]);
            last_idx = idx;
        }
    }
    align_4(&b);

    uint32_t *static_values_offsets = malloc(class_count * 4);
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        int32_t last_init_idx = -1;
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].has_init_value) {
                last_init_idx = (int32_t)j;
            }
        }
        if (last_init_idx != -1) {
            static_values_offsets[i] = b.len;
            buf_write_uleb128(&b, last_init_idx + 1);
            for (int32_t j = 0; j <= last_init_idx; j++) {
                if (c->static_fields[j].has_init_value) {
                    smali_field_def_t *f = &c->static_fields[j];
                    switch (f->value_type) {
                    case VALUE_TYPE_NULL:
                        buf_write_u8(&b, 0x1e); break;
                    case VALUE_TYPE_BOOL:
                        buf_write_u8(&b, 0x1f | (f->value_int ? 0x20 : 0x00)); break;
                    case VALUE_TYPE_BYTE:
                    case VALUE_TYPE_SHORT:
                    case VALUE_TYPE_CHAR:
                    case VALUE_TYPE_INT:
                    case VALUE_TYPE_LONG:
                    case VALUE_TYPE_FLOAT:
                    case VALUE_TYPE_DOUBLE: {
                        char t = c->static_fields[j].type[0];

                        if (t == 'L' || t == '[') {
                            buf_write_u8(&b, 0x1E);
                            break;
                        }

                        if (t == 'Z') {
                            int v = f->value_int;
                            buf_write_u8(&b, (v ? 1 : 0) << 5 | 0x1F);
                            break;
                        }

                        if (t == 'F') {
                            float fv = (float)f->value_double;
                            uint32_t fbits; memcpy(&fbits, &fv, 4);
                            int size = 4;
                            buf_write_u8(&b, ((size - 1) << 5) | 0x10);
                            for (int k = 0; k < size; k++)
                                buf_write_u8(&b, (fbits >> (k * 8)) & 0xFF);
                            break;
                        }

                        if (t == 'D') {
                            double dv = f->value_double;
                            uint64_t dbits; memcpy(&dbits, &dv, 8);
                            int size = 8;
                            buf_write_u8(&b, ((size - 1) << 5) | 0x11);
                            for (int k = 0; k < size; k++)
                                buf_write_u8(&b, (dbits >> (k * 8)) & 0xFF);
                            break;
                        }

                        int64_t v = (f->value_type == VALUE_TYPE_LONG) ? f->value_int : (int32_t)f->value_int;
                        uint8_t type_tag = 0x04;
                        int size = 4;

                        if (t == 'B') { type_tag = 0x00; size = 1; }
                        else if (t == 'S') { type_tag = 0x02; size = 2; }
                        else if (t == 'C') { type_tag = 0x03; size = 2; }
                        else if (t == 'J') {
                            type_tag = 0x06; size = 8;
                            if (v >= -128 && v <= 127) size = 1;
                            else if (v >= -32768 && v <= 32767) size = 2;
                            else if (v >= -8388608 && v <= 8388607) size = 3;
                            else if (v >= -2147483648LL && v <= 2147483647LL) size = 4;
                            else if (v >= -549755813888LL && v <= 549755813887LL) size = 5;
                            else if (v >= -140737488355328LL && v <= 140737488355327LL) size = 6;
                            else if (v >= -36028797018963968LL && v <= 36028797018963967LL) size = 7;
                        } else {
                            if (v >= -128 && v <= 127) size = 1;
                            else if (v >= -32768 && v <= 32767) size = 2;
                            else if (v >= -8388608 && v <= 8388607) size = 3;
                        }

                        buf_write_u8(&b, ((size - 1) << 5) | type_tag);
                        for (int k = 0; k < size; k++)
                            buf_write_u8(&b, (v >> (k * 8)) & 0xFF);
                        break;
                    }

                    case VALUE_TYPE_STRING: {
                        uint32_t sidx = smali_pool_find(&ctx->strings, f->value_str);
                        if (sidx == 0xFFFFFFFF) sidx = 0;
                        int size = 4;
                        if (sidx <= 0xFF) size = 1;
                        else if (sidx <= 0xFFFF) size = 2;
                        else if (sidx <= 0xFFFFFF) size = 3;
                        buf_write_u8(&b, ((size - 1) << 5) | 0x17);
                        for (int k = 0; k < size; k++)
                            buf_write_u8(&b, (sidx >> (k * 8)) & 0xFF);
                        break;
                    }
                    case VALUE_TYPE_ENUM: {
                        uint32_t fidx = smali_pool_find(&ctx->fields, f->value_str);
                        if (fidx == 0xFFFFFFFF) fidx = 0;
                        int size = 4;
                        if (fidx <= 0xFF) size = 1;
                        else if (fidx <= 0xFFFF) size = 2;
                        else if (fidx <= 0xFFFFFF) size = 3;
                        buf_write_u8(&b, ((size - 1) << 5) | 0x1B);
                        for (int k = 0; k < size; k++)
                            buf_write_u8(&b, (fidx >> (k * 8)) & 0xFF);
                        break;
                    }
                    case VALUE_TYPE_TYPE: {
                        uint32_t tidx = smali_pool_find(&ctx->types, f->value_str);
                        if (tidx == 0xFFFFFFFF) tidx = 0;
                        int size = 4;
                        if (tidx <= 0xFF) size = 1;
                        else if (tidx <= 0xFFFF) size = 2;
                        else if (tidx <= 0xFFFFFF) size = 3;
                        buf_write_u8(&b, ((size - 1) << 5) | 0x18);
                        for (int k = 0; k < size; k++)
                            buf_write_u8(&b, (tidx >> (k * 8)) & 0xFF);
                        break;
                    }
                    default:
                        buf_write_u8(&b, 0x04);
                        buf_write_u8(&b, 0x00);
                        break;
                    }
                } else {
                    char t = c->static_fields[j].type[0];
                    if (t == 'L' || t == '[') {
                        buf_write_u8(&b, 0x1E);
                    } else if (t == 'Z') {
                        buf_write_u8(&b, 0x1F);
                    } else if (t == 'J') {
                        buf_write_u8(&b, 0x06); buf_write_u8(&b, 0x00);
                    } else if (t == 'F') {
                        buf_write_u8(&b, 0x10); buf_write_u8(&b, 0x00);
                    } else if (t == 'D') {
                        buf_write_u8(&b, 0x11); buf_write_u8(&b, 0x00);
                    } else if (t == 'B') {
                        buf_write_u8(&b, 0x00); buf_write_u8(&b, 0x00);
                    } else if (t == 'C') {
                        buf_write_u8(&b, 0x03); buf_write_u8(&b, 0x00);
                    } else if (t == 'S') {
                        buf_write_u8(&b, 0x02); buf_write_u8(&b, 0x00);
                    } else {
                        buf_write_u8(&b, 0x04); buf_write_u8(&b, 0x00);
                    }
                }
            }
        } else {
            static_values_offsets[i] = 0;
        }
    }
    align_4(&b);

    for (uint32_t i = 0; i < string_count; i++) {
        *(uint32_t *)(b.buf + string_ids_off + i * 4) = string_offsets[i];
    }
    for (uint32_t i = 0; i < type_count; i++) {
        *(uint32_t *)(b.buf + type_ids_off + i * 4) = smali_pool_find(&ctx->strings, ctx->types.strings[i]);
    }
    for (uint32_t i = 0; i < proto_count; i++) {
        const char *sig = ctx->protos.strings[i];
        char shorty[512];
        int s_idx = 0;
        const char *close_paren = strchr(sig, ')');
        if (close_paren) {
            char ret_char = close_paren[1];
            shorty[s_idx++] = (ret_char == 'L' || ret_char == '[') ? 'L' : ret_char;
        } else {
            shorty[s_idx++] = 'V';
        }

        const char *p = sig;
        if (*p == '(') p++;
        while (p && *p && *p != ')') {
            if (*p == 'L') {
                shorty[s_idx++] = 'L';
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else if (*p == '[') {
                shorty[s_idx++] = 'L';
                while (*p == '[') p++;
                if (*p == 'L') {
                    while (*p && *p != ';') p++;
                    if (*p) p++;
                } else {
                    p++;
                }
            } else {
                shorty[s_idx++] = *p;
                p++;
            }
        }
        shorty[s_idx] = '\0';

        uint32_t proto_off = proto_ids_off + i * 12;
        *(uint32_t *)(b.buf + proto_off) = smali_pool_find(&ctx->strings, shorty);
        *(uint32_t *)(b.buf + proto_off + 4) = smali_pool_find(&ctx->types, close_paren ? close_paren + 1 : "V");
        *(uint32_t *)(b.buf + proto_off + 8) = proto_param_offsets[i];
    }
    for (uint32_t i = 0; i < field_count; i++) {
        const char *key = ctx->fields.strings[i];
        /* Malformed keys ("??", "Lcls;->name" without a type) reach here from
         * annotation values and unresolvable refs. The comparator tolerates
         * them, so the writer must not dereference the missing parts. */
        char *arrow = key ? strstr(key, "->") : NULL;
        char *colon = key ? strchr(key, ':') : NULL;
        if (!arrow || !colon || colon <= arrow + 2) {
            fprintf(stderr, "WARNING: skipping malformed field key '%s'\n", key ? key : "(null)");
            continue;
        }
        char *class_part = strndup(key, arrow - key);
        char *name_part = strndup(arrow + 2, colon - (arrow + 2));
        uint32_t field_off = field_ids_off + i * 8;
        *(uint16_t *)(b.buf + field_off) = smali_pool_find(&ctx->types, class_part);
        *(uint16_t *)(b.buf + field_off + 2) = smali_pool_find(&ctx->types, colon + 1);
        *(uint32_t *)(b.buf + field_off + 4) = smali_pool_find(&ctx->strings, name_part);
        free(class_part); free(name_part);
    }
    for (uint32_t i = 0; i < method_count; i++) {
        const char *key = ctx->methods.strings[i];
        char *arrow = key ? strstr(key, "->") : NULL;
        char *paren = key ? strchr(key, '(') : NULL;
        if (!arrow || !paren || paren <= arrow + 2) {
            fprintf(stderr, "WARNING: skipping malformed method key '%s'\n", key ? key : "(null)");
            continue;
        }
        char *class_part = strndup(key, arrow - key);
        char *name_part = strndup(arrow + 2, paren - (arrow + 2));
        uint32_t method_off = method_ids_off + i * 8;
        *(uint16_t *)(b.buf + method_off) = smali_pool_find(&ctx->types, class_part);
        *(uint16_t *)(b.buf + method_off + 2) = smali_pool_find(&ctx->protos, paren);
        *(uint32_t *)(b.buf + method_off + 4) = smali_pool_find(&ctx->strings, name_part);
        free(class_part); free(name_part);
    }
    align_4(&b);

    // Write annotation data: track offsets for map list
    uint32_t *annot_offsets = calloc(class_count, 4);
    uint32_t first_annot_dir_off = 0, annot_dir_count = 0;
    uint32_t first_annot_set_off = 0, annot_set_count = 0;
    uint32_t first_annot_item_off = 0, annot_item_count = 0;
    uint32_t first_annot_set_ref_list_off = 0, annot_set_ref_list_count = 0;

    uint32_t **class_annot_item_offs = calloc(class_count, sizeof(uint32_t*));
    uint32_t *class_set_offs = calloc(class_count, sizeof(uint32_t));

    uint32_t ***meth_d_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **meth_d_set_offs = calloc(class_count, sizeof(uint32_t*));
    
    uint32_t ***meth_v_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **meth_v_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ***field_s_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **field_s_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ***field_i_annot_item_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **field_i_set_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ****param_d_annot_item_offs = calloc(class_count, sizeof(uint32_t***));
    uint32_t ***param_d_set_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **param_d_ref_list_offs = calloc(class_count, sizeof(uint32_t*));

    uint32_t ****param_v_annot_item_offs = calloc(class_count, sizeof(uint32_t***));
    uint32_t ***param_v_set_offs = calloc(class_count, sizeof(uint32_t**));
    uint32_t **param_v_ref_list_offs = calloc(class_count, sizeof(uint32_t*));

    // Pass 1: Write all annotation_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        
        if (c->annot_count > 0 && c->annots[0].type) {
            uint32_t ic = c->annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->annot_count;
            class_annot_item_offs[i] = calloc(ic, sizeof(uint32_t));
            for (uint32_t ai = 0; ai < ic; ai++) {
                class_annot_item_offs[i][ai] = b.len;
                annot_item_count++;
                if (first_annot_item_off == 0) first_annot_item_off = b.len;
                buf_write_u8(&b, c->annots[ai].visibility);
                write_encoded_annotation(&b, ctx, &c->annots[ai]);
            }
        }
        
        field_s_annot_item_offs[i] = calloc(c->static_field_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].annot_count > 0) {
                uint32_t ac = c->static_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->static_fields[j].annot_count;
                field_s_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    field_s_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, c->static_fields[j].annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &c->static_fields[j].annots[ai]);
                }
            }
        }

        field_i_annot_item_offs[i] = calloc(c->instance_field_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (c->instance_fields[j].annot_count > 0) {
                uint32_t ac = c->instance_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->instance_fields[j].annot_count;
                field_i_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    field_i_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, c->instance_fields[j].annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &c->instance_fields[j].annots[ai]);
                }
            }
        }

        meth_d_annot_item_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t*));
        param_d_annot_item_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t**));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            smali_method_def_t *m = &c->direct_methods[j];
            if (m->annot_count > 0) {
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                meth_d_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    meth_d_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, m->annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &m->annots[ai]);
                }
            }
            if (m->param_name_count > 0 && m->param_annots) {
                param_d_annot_item_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t*));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (m->param_annot_counts && m->param_annot_counts[pidx] > 0) {
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        param_d_annot_item_offs[i][j][pidx] = calloc(ac, sizeof(uint32_t));
                        for (uint32_t ai = 0; ai < ac; ai++) {
                            param_d_annot_item_offs[i][j][pidx][ai] = b.len;
                            annot_item_count++;
                            if (first_annot_item_off == 0) first_annot_item_off = b.len;
                            buf_write_u8(&b, m->param_annots[pidx][ai].visibility);
                            write_encoded_annotation(&b, ctx, &m->param_annots[pidx][ai]);
                        }
                    }
                }
            }
        }

        meth_v_annot_item_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t*));
        param_v_annot_item_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t**));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            smali_method_def_t *m = &c->virtual_methods[j];
            if (m->annot_count > 0) {
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                meth_v_annot_item_offs[i][j] = calloc(ac, sizeof(uint32_t));
                for (uint32_t ai = 0; ai < ac; ai++) {
                    meth_v_annot_item_offs[i][j][ai] = b.len;
                    annot_item_count++;
                    if (first_annot_item_off == 0) first_annot_item_off = b.len;
                    buf_write_u8(&b, m->annots[ai].visibility);
                    write_encoded_annotation(&b, ctx, &m->annots[ai]);
                }
            }
            if (m->param_name_count > 0 && m->param_annots) {
                param_v_annot_item_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t*));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (m->param_annot_counts && m->param_annot_counts[pidx] > 0) {
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        param_v_annot_item_offs[i][j][pidx] = calloc(ac, sizeof(uint32_t));
                        for (uint32_t ai = 0; ai < ac; ai++) {
                            param_v_annot_item_offs[i][j][pidx][ai] = b.len;
                            annot_item_count++;
                            if (first_annot_item_off == 0) first_annot_item_off = b.len;
                            buf_write_u8(&b, m->param_annots[pidx][ai].visibility);
                            write_encoded_annotation(&b, ctx, &m->param_annots[pidx][ai]);
                        }
                    }
                }
            }
        }
    }
    
    // Pass 2: Write all annotation_set_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        
        if (class_annot_item_offs[i]) {
            align_4(&b);
            class_set_offs[i] = b.len;
            annot_set_count++;
            if (first_annot_set_off == 0) first_annot_set_off = b.len;
            uint32_t ic = c->annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->annot_count;
            buf_write_u32(&b, ic);
            for (uint32_t ai = 0; ai < ic; ai++) buf_write_u32(&b, class_annot_item_offs[i][ai]);
        }
        
        field_s_set_offs[i] = calloc(c->static_field_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (field_s_annot_item_offs[i][j]) {
                align_4(&b);
                field_s_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = c->static_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->static_fields[j].annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, field_s_annot_item_offs[i][j][ai]);
            }
        }

        field_i_set_offs[i] = calloc(c->instance_field_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (field_i_annot_item_offs[i][j]) {
                align_4(&b);
                field_i_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = c->instance_fields[j].annot_count > MAX_ANNOTS ? MAX_ANNOTS : c->instance_fields[j].annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, field_i_annot_item_offs[i][j][ai]);
            }
        }

        meth_d_set_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t));
        param_d_set_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            smali_method_def_t *m = &c->direct_methods[j];
            if (meth_d_annot_item_offs[i][j]) {
                align_4(&b);
                meth_d_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, meth_d_annot_item_offs[i][j][ai]);
            }
            if (param_d_annot_item_offs[i][j]) {
                param_d_set_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (param_d_annot_item_offs[i][j][pidx]) {
                        align_4(&b);
                        param_d_set_offs[i][j][pidx] = b.len;
                        annot_set_count++;
                        if (first_annot_set_off == 0) first_annot_set_off = b.len;
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        buf_write_u32(&b, ac);
                        for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, param_d_annot_item_offs[i][j][pidx][ai]);
                    }
                }
            }
        }

        meth_v_set_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t));
        param_v_set_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t*));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            smali_method_def_t *m = &c->virtual_methods[j];
            if (meth_v_annot_item_offs[i][j]) {
                align_4(&b);
                meth_v_set_offs[i][j] = b.len;
                annot_set_count++;
                if (first_annot_set_off == 0) first_annot_set_off = b.len;
                uint32_t ac = m->annot_count > MAX_ANNOTS ? MAX_ANNOTS : m->annot_count;
                buf_write_u32(&b, ac);
                for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, meth_v_annot_item_offs[i][j][ai]);
            }
            if (param_v_annot_item_offs[i][j]) {
                param_v_set_offs[i][j] = calloc(m->param_name_count, sizeof(uint32_t));
                for (uint32_t pidx = 0; pidx < m->param_name_count; pidx++) {
                    if (param_v_annot_item_offs[i][j][pidx]) {
                        align_4(&b);
                        param_v_set_offs[i][j][pidx] = b.len;
                        annot_set_count++;
                        if (first_annot_set_off == 0) first_annot_set_off = b.len;
                        uint32_t ac = m->param_annot_counts[pidx] > MAX_ANNOTS ? MAX_ANNOTS : m->param_annot_counts[pidx];
                        buf_write_u32(&b, ac);
                        for (uint32_t ai = 0; ai < ac; ai++) buf_write_u32(&b, param_v_annot_item_offs[i][j][pidx][ai]);
                    }
                }
            }
        }
    }

    // Pass 2.5: Write annotation_set_ref_list for parameter annotations
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        param_d_ref_list_offs[i] = calloc(c->direct_method_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            if (param_d_annot_item_offs[i][j]) {
                int has_param_annot = 0;
                for (uint32_t pidx = 0; pidx < c->direct_methods[j].param_name_count; pidx++) {
                    if (param_d_set_offs[i][j][pidx]) has_param_annot = 1;
                }
                if (has_param_annot) {
                    align_4(&b);
                    param_d_ref_list_offs[i][j] = b.len;
                    annot_set_ref_list_count++;
                    if (first_annot_set_ref_list_off == 0) first_annot_set_ref_list_off = b.len;
                    buf_write_u32(&b, c->direct_methods[j].param_name_count);
                    for (uint32_t pidx = 0; pidx < c->direct_methods[j].param_name_count; pidx++) {
                        buf_write_u32(&b, param_d_set_offs[i][j][pidx]);
                    }
                }
            }
        }
        param_v_ref_list_offs[i] = calloc(c->virtual_method_count, sizeof(uint32_t));
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            if (param_v_annot_item_offs[i][j]) {
                int has_param_annot = 0;
                for (uint32_t pidx = 0; pidx < c->virtual_methods[j].param_name_count; pidx++) {
                    if (param_v_set_offs[i][j][pidx]) has_param_annot = 1;
                }
                if (has_param_annot) {
                    align_4(&b);
                    param_v_ref_list_offs[i][j] = b.len;
                    annot_set_ref_list_count++;
                    if (first_annot_set_ref_list_off == 0) first_annot_set_ref_list_off = b.len;
                    buf_write_u32(&b, c->virtual_methods[j].param_name_count);
                    for (uint32_t pidx = 0; pidx < c->virtual_methods[j].param_name_count; pidx++) {
                        buf_write_u32(&b, param_v_set_offs[i][j][pidx]);
                    }
                }
            }
        }
    }
    
    // Pass 3: Write all annotations_directory_item
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        int has_any = 0;
        if (class_annot_item_offs[i]) has_any = 1;
        
        uint32_t field_annot_count = 0;
        for (uint32_t j = 0; j < c->static_field_count; j++) if (field_s_set_offs[i][j]) field_annot_count++;
        for (uint32_t j = 0; j < c->instance_field_count; j++) if (field_i_set_offs[i][j]) field_annot_count++;
        if (field_annot_count > 0) has_any = 1;

        uint32_t meth_annot_count = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) if (meth_d_set_offs[i][j]) meth_annot_count++;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) if (meth_v_set_offs[i][j]) meth_annot_count++;
        if (meth_annot_count > 0) has_any = 1;

        uint32_t param_annot_count = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) if (param_d_ref_list_offs[i][j]) param_annot_count++;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) if (param_v_ref_list_offs[i][j]) param_annot_count++;
        if (param_annot_count > 0) has_any = 1;
        
        if (!has_any) continue;
        
        align_4(&b);
        annot_offsets[i] = b.len;
        annot_dir_count++;
        if (first_annot_dir_off == 0) first_annot_dir_off = b.len;
        
        buf_write_u32(&b, class_set_offs[i]);
        buf_write_u32(&b, field_annot_count);
        buf_write_u32(&b, meth_annot_count);
        buf_write_u32(&b, param_annot_count);
        
        if (field_annot_count > 0) {
            for (uint32_t j = 0; j < c->static_field_count; j++) {
                if (field_s_set_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
                    uint32_t fidx = smali_pool_find(&ctx->fields, mkey);
                    buf_write_u32(&b, fidx == 0xFFFFFFFF ? 0 : fidx);
                    buf_write_u32(&b, field_s_set_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->instance_field_count; j++) {
                if (field_i_set_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
                    uint32_t fidx = smali_pool_find(&ctx->fields, mkey);
                    buf_write_u32(&b, fidx == 0xFFFFFFFF ? 0 : fidx);
                    buf_write_u32(&b, field_i_set_offs[i][j]);
                }
            }
        }
        
        if (meth_annot_count > 0) {
            for (uint32_t j = 0; j < c->direct_method_count; j++) {
                if (meth_d_set_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, meth_d_set_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->virtual_method_count; j++) {
                if (meth_v_set_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, meth_v_set_offs[i][j]);
                }
            }
        }
        
        if (param_annot_count > 0) {
            for (uint32_t j = 0; j < c->direct_method_count; j++) {
                if (param_d_ref_list_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, param_d_ref_list_offs[i][j]);
                }
            }
            for (uint32_t j = 0; j < c->virtual_method_count; j++) {
                if (param_v_ref_list_offs[i][j]) {
                    char mkey[4096];
                    snprintf(mkey, sizeof(mkey), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
                    uint32_t midx = smali_pool_find(&ctx->methods, mkey);
                    buf_write_u32(&b, midx == 0xFFFFFFFF ? 0 : midx);
                    buf_write_u32(&b, param_v_ref_list_offs[i][j]);
                }
            }
        }
    }
    
    // Cleanup memory
    for (uint32_t i = 0; i < class_count; i++) {
        if (class_annot_item_offs[i]) free(class_annot_item_offs[i]);
        
        if (meth_d_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) if (meth_d_annot_item_offs[i][j]) free(meth_d_annot_item_offs[i][j]);
            free(meth_d_annot_item_offs[i]);
        }
        if (meth_v_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) if (meth_v_annot_item_offs[i][j]) free(meth_v_annot_item_offs[i][j]);
            free(meth_v_annot_item_offs[i]);
        }
        if (meth_d_set_offs[i]) free(meth_d_set_offs[i]);
        if (meth_v_set_offs[i]) free(meth_v_set_offs[i]);

        if (field_s_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].static_field_count; j++) if (field_s_annot_item_offs[i][j]) free(field_s_annot_item_offs[i][j]);
            free(field_s_annot_item_offs[i]);
        }
        if (field_i_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].instance_field_count; j++) if (field_i_annot_item_offs[i][j]) free(field_i_annot_item_offs[i][j]);
            free(field_i_annot_item_offs[i]);
        }
        if (field_s_set_offs[i]) free(field_s_set_offs[i]);
        if (field_i_set_offs[i]) free(field_i_set_offs[i]);

        if (param_d_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) {
                if (param_d_annot_item_offs[i][j]) {
                    for (uint32_t pidx = 0; pidx < ctx->classes[i].direct_methods[j].param_name_count; pidx++) {
                        if (param_d_annot_item_offs[i][j][pidx]) free(param_d_annot_item_offs[i][j][pidx]);
                    }
                    free(param_d_annot_item_offs[i][j]);
                }
            }
            free(param_d_annot_item_offs[i]);
        }
        if (param_v_annot_item_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) {
                if (param_v_annot_item_offs[i][j]) {
                    for (uint32_t pidx = 0; pidx < ctx->classes[i].virtual_methods[j].param_name_count; pidx++) {
                        if (param_v_annot_item_offs[i][j][pidx]) free(param_v_annot_item_offs[i][j][pidx]);
                    }
                    free(param_v_annot_item_offs[i][j]);
                }
            }
            free(param_v_annot_item_offs[i]);
        }
        if (param_d_set_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].direct_method_count; j++) if (param_d_set_offs[i][j]) free(param_d_set_offs[i][j]);
            free(param_d_set_offs[i]);
        }
        if (param_v_set_offs[i]) {
            for (uint32_t j = 0; j < ctx->classes[i].virtual_method_count; j++) if (param_v_set_offs[i][j]) free(param_v_set_offs[i][j]);
            free(param_v_set_offs[i]);
        }
        if (param_d_ref_list_offs[i]) free(param_d_ref_list_offs[i]);
        if (param_v_ref_list_offs[i]) free(param_v_ref_list_offs[i]);
    }
    free(class_annot_item_offs);
    free(class_set_offs);
    free(meth_d_annot_item_offs);
    free(meth_v_annot_item_offs);
    free(meth_d_set_offs);
    free(meth_v_set_offs);
    free(field_s_annot_item_offs);
    free(field_i_annot_item_offs);
    free(field_s_set_offs);
    free(field_i_set_offs);
    free(param_d_annot_item_offs);
    free(param_v_annot_item_offs);
    free(param_d_set_offs);
    free(param_v_set_offs);
    free(param_d_ref_list_offs);
    free(param_v_ref_list_offs);

    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        uint32_t def_off = class_defs_off + i * 32;

        uint32_t cls_idx = smali_pool_find(&ctx->types, c->descriptor);
        if (cls_idx == 0xFFFFFFFF) {
            fprintf(stderr, "WARNING: Class descriptor '%s' not found in types pool!\n", c->descriptor);
        }
        uint32_t super_idx = c->super_class ? smali_pool_find(&ctx->types, c->super_class) : 0xFFFFFFFF;
        if (c->super_class && super_idx == 0xFFFFFFFF) {
            fprintf(stderr, "WARNING: Superclass '%s' of '%s' not found in types pool!\n", c->super_class, c->descriptor);
        }
        uint32_t src_idx = c->source_file ? smali_pool_find(&ctx->strings, c->source_file) : 0xFFFFFFFF;
        if (c->source_file && src_idx == 0xFFFFFFFF) {
            fprintf(stderr, "WARNING: Source file '%s' of '%s' not found in strings pool!\n", c->source_file, c->descriptor);
        }

        *(uint32_t *)(b.buf + def_off) = cls_idx;
        *(uint32_t *)(b.buf + def_off + 4) = c->access_flags;
        *(uint32_t *)(b.buf + def_off + 8) = super_idx;
        *(uint32_t *)(b.buf + def_off + 12) = interfaces_offsets[i];
        *(uint32_t *)(b.buf + def_off + 16) = src_idx;
        *(uint32_t *)(b.buf + def_off + 20) = annot_offsets[i];
        *(uint32_t *)(b.buf + def_off + 24) = class_data_offsets[i];
        *(uint32_t *)(b.buf + def_off + 28) = static_values_offsets[i];
    }

    align_4(&b);
    uint32_t map_off = b.len;

    map_item_entry_t entries[20];
    uint32_t entry_count = 0;

    entries[entry_count++] = (map_item_entry_t){0x0000, 1, 0};

    if (string_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0001, string_count, string_ids_off};
    }
    if (type_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0002, type_count, type_ids_off};
    }
    if (proto_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0003, proto_count, proto_ids_off};
    }
    if (field_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0004, field_count, field_ids_off};
    }
    if (method_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0005, method_count, method_ids_off};
    }
    if (class_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0006, class_count, class_defs_off};
    }

    if (string_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2002, string_count, string_offsets[0]};
    }

    uint32_t first_type_list_off = 0;
    uint32_t type_list_count = 0;
    uint32_t *all_tl_offs = malloc((proto_count + class_count) * sizeof(uint32_t));
    uint32_t tl_offs_count = 0;
    for (uint32_t i = 0; i < proto_count; i++) {
        if (proto_param_offsets[i] != 0) {
            all_tl_offs[tl_offs_count++] = proto_param_offsets[i];
        }
    }
    for (uint32_t i = 0; i < class_count; i++) {
        if (interfaces_offsets[i] != 0) {
            all_tl_offs[tl_offs_count++] = interfaces_offsets[i];
        }
    }
    if (tl_offs_count > 0) {
        qsort(all_tl_offs, tl_offs_count, sizeof(uint32_t), comp_u32_tl);
        first_type_list_off = all_tl_offs[0];
        type_list_count = 1;
        for (uint32_t i = 1; i < tl_offs_count; i++) {
            if (all_tl_offs[i] != all_tl_offs[i - 1]) {
                type_list_count++;
            }
        }
    }
    free(all_tl_offs);
    if (type_list_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x1001, type_list_count, first_type_list_off};
    }

    uint32_t first_code_off = 0;
    uint32_t total_code_count = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            if (direct_code_offsets[i][j] != 0) {
                if (first_code_off == 0 || direct_code_offsets[i][j] < first_code_off) {
                    first_code_off = direct_code_offsets[i][j];
                }
                total_code_count++;
            }
        }
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            if (virtual_code_offsets[i][j] != 0) {
                if (first_code_off == 0 || virtual_code_offsets[i][j] < first_code_off) {
                    first_code_off = virtual_code_offsets[i][j];
                }
                total_code_count++;
            }
        }
    }
    if (total_code_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2001, total_code_count, first_code_off};
    }

    if (class_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2000, class_count, class_data_offsets[0]};
    }

    uint32_t first_static_val_off = 0;
    uint32_t static_val_count = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        if (static_values_offsets[i] != 0) {
            if (first_static_val_off == 0 || static_values_offsets[i] < first_static_val_off) {
                first_static_val_off = static_values_offsets[i];
            }
            static_val_count++;
        }
    }
    if (static_val_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2005, static_val_count, first_static_val_off};
    }

    // Annotation sections in the map list
    if (annot_dir_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2006, annot_dir_count, first_annot_dir_off};
    }
    if (annot_set_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x1003, annot_set_count, first_annot_set_off};
    }
    if (annot_item_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2004, annot_item_count, first_annot_item_off};
    }

    entries[entry_count++] = (map_item_entry_t){0x1000, 1, map_off};

    qsort(entries, entry_count, sizeof(map_item_entry_t), comp_map_entries);

    buf_write_u32(&b, entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        write_map_item(&b, entries[i].type, entries[i].size, entries[i].offset);
    }

    memcpy(b.buf, "dex\n035\0", 8);
    *(uint32_t *)(b.buf + 32) = b.len;
    *(uint32_t *)(b.buf + 36) = header_size;
    *(uint32_t *)(b.buf + 40) = 0x12345678;

    uint32_t header_offsets[14] = {
        string_count, hdr_string_ids_off,
        type_count, hdr_type_ids_off,
        proto_count, hdr_proto_ids_off,
        field_count, hdr_field_ids_off,
        method_count, hdr_method_ids_off,
        class_count, hdr_class_defs_off,
        b.len - data_off, data_off
    };
    memcpy(b.buf + 56, header_offsets, sizeof(header_offsets));
    *(uint32_t *)(b.buf + 52) = map_off;

    smali_sha1(b.buf + 32, b.len - 32, b.buf + 12);
    uint32_t checksum = adler32(b.buf + 12, b.len - 12);
    *(uint32_t *)(b.buf + 8) = checksum;

    FILE *fp = fopen(out_dex, "wb");
    if (!fp) return -1;
    fwrite(b.buf, 1, b.len, fp);
    fclose(fp);
    buf_free(&b);
    free(string_offsets); free(proto_param_offsets); free(interfaces_offsets); free(static_values_offsets); free(annot_offsets);
    for (uint32_t i = 0; i < class_count; i++) {
        if (direct_code_offsets[i]) free(direct_code_offsets[i]);
        if (virtual_code_offsets[i]) free(virtual_code_offsets[i]);
        if (direct_code_ends[i]) free(direct_code_ends[i]);
        if (virtual_code_ends[i]) free(virtual_code_ends[i]);
    }
    free(direct_code_offsets); free(virtual_code_offsets);
    free(direct_code_ends); free(virtual_code_ends); free(class_data_offsets);
    return fp ? 0 : -1;

code_write_fail:;
    /* An instruction failed to encode: discard the half-written DEX. Only the
     * arrays allocated up to this point are released; the class-data, static
     * value and annotation arrays are not allocated yet. */
    for (uint32_t i = 0; i < class_count; i++) {
        free(direct_code_offsets[i]);
        free(virtual_code_offsets[i]);
        free(direct_code_ends[i]);
        free(virtual_code_ends[i]);
    }
    free(direct_code_offsets); free(virtual_code_offsets);
    free(direct_code_ends); free(virtual_code_ends);
    free(string_offsets); free(proto_param_offsets); free(interfaces_offsets);
    buf_free(&b);
    return -1;
}
