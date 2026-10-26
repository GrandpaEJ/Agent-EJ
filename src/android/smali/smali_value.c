#include "smali_value.h"
#include "smali_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void write_encoded_value(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_elem_t *el) {
    switch (el->value_type) {
    case VALUE_TYPE_BYTE: {
        int8_t v = (int8_t)el->value_int;
        buf_write_u8(b, (0 << 5) | VALUE_TYPE_BYTE);
        buf_write_u8(b, (uint8_t)v);
        break;
    }
    case VALUE_TYPE_SHORT: {
        int16_t v = (int16_t)el->value_int;
        buf_write_u8(b, (1 << 5) | VALUE_TYPE_SHORT);
        buf_write_u8(b, v & 0xFF);
        buf_write_u8(b, (v >> 8) & 0xFF);
        break;
    }
    case VALUE_TYPE_CHAR: {
        uint16_t v = (uint16_t)el->value_int;
        buf_write_u8(b, (1 << 5) | VALUE_TYPE_CHAR);
        buf_write_u8(b, v & 0xFF);
        buf_write_u8(b, (v >> 8) & 0xFF);
        break;
    }
    case VALUE_TYPE_INT: {
        int32_t v = (int32_t)el->value_int;
        int sz = 4;
        if (v >= -128 && v <= 127) sz = 1;
        else if (v >= -32768 && v <= 32767) sz = 2;
        else if (v >= -8388608 && v <= 8388607) sz = 3;
        buf_write_u8(b, ((sz - 1) << 5) | VALUE_TYPE_INT);
        for (int k = 0; k < sz; k++) buf_write_u8(b, (v >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_LONG: {
        int64_t v = el->value_int;
        int sz = 8;
        if (v >= -128 && v <= 127) sz = 1;
        else if (v >= -32768 && v <= 32767) sz = 2;
        else if (v >= -8388608 && v <= 8388607) sz = 3;
        else if (v >= -2147483648LL && v <= 2147483647LL) sz = 4;
        else if (v >= -549755813888LL && v <= 549755813887LL) sz = 5;
        else if (v >= -140737488355328LL && v <= 140737488355327LL) sz = 6;
        else if (v >= -36028797018963968LL && v <= 36028797018963967LL) sz = 7;
        buf_write_u8(b, ((sz - 1) << 5) | VALUE_TYPE_LONG);
        for (int k = 0; k < sz; k++) buf_write_u8(b, (v >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_FLOAT: {
        float fv = (float)el->value_double;
        uint32_t fbits;
        memcpy(&fbits, &fv, 4);
        buf_write_u8(b, (3 << 5) | VALUE_TYPE_FLOAT);
        for (int k = 0; k < 4; k++) buf_write_u8(b, (fbits >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_DOUBLE: {
        double dv = el->value_double;
        uint64_t dbits;
        memcpy(&dbits, &dv, 8);
        buf_write_u8(b, (7 << 5) | VALUE_TYPE_DOUBLE);
        for (int k = 0; k < 8; k++) buf_write_u8(b, (dbits >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_STRING: {
        uint32_t sidx = el->value_str ? smali_pool_find(&ctx->strings, el->value_str) : 0;
        if (sidx == 0xFFFFFFFF) sidx = 0;
        int nb = 2;
        if (sidx > 0xFFFF) nb = 3;
        buf_write_u8(b, ((nb - 1) << 5) | VALUE_TYPE_STRING);
        for (int k = 0; k < nb; k++) buf_write_u8(b, (sidx >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_TYPE: {
        uint32_t tidx = el->value_str ? smali_pool_find(&ctx->types, el->value_str) : 0;
        if (tidx == 0xFFFFFFFF) tidx = 0;
        int nb = 2;
        if (tidx > 0xFFFF) nb = 3;
        buf_write_u8(b, ((nb - 1) << 5) | VALUE_TYPE_TYPE);
        for (int k = 0; k < nb; k++) buf_write_u8(b, (tidx >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_ENUM: {
        uint32_t fidx = el->value_str ? smali_pool_find(&ctx->fields, el->value_str) : 0;
        if (fidx == 0xFFFFFFFF) {
            fprintf(stderr, "WARNING: unresolved enum reference '%s'\n",
                    el->value_str ? el->value_str : "(null)");
            fidx = 0;
        }
        int nb = 2;
        if (fidx > 0xFFFF) nb = 3;
        buf_write_u8(b, ((nb - 1) << 5) | VALUE_TYPE_ENUM);
        for (int k = 0; k < nb; k++) buf_write_u8(b, (fidx >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_FIELD: {
        uint32_t fidx = el->value_str ? smali_pool_find(&ctx->fields, el->value_str) : 0;
        if (fidx == 0xFFFFFFFF) {
            fprintf(stderr, "WARNING: unresolved field reference '%s'\n",
                    el->value_str ? el->value_str : "(null)");
            fidx = 0;
        }
        int nb = 2;
        if (fidx > 0xFFFF) nb = 3;
        buf_write_u8(b, ((nb - 1) << 5) | VALUE_TYPE_FIELD);
        for (int k = 0; k < nb; k++) buf_write_u8(b, (fidx >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_METHOD: {
        uint32_t midx = el->value_str ? smali_pool_find(&ctx->methods, el->value_str) : 0;
        if (midx == 0xFFFFFFFF) midx = 0;
        int nb = 2;
        if (midx > 0xFFFF) nb = 3;
        buf_write_u8(b, ((nb - 1) << 5) | VALUE_TYPE_METHOD);
        for (int k = 0; k < nb; k++) buf_write_u8(b, (midx >> (k * 8)) & 0xFF);
        break;
    }
    case VALUE_TYPE_NULL:
        buf_write_u8(b, VALUE_TYPE_NULL);
        break;
    case VALUE_TYPE_BOOL:
        buf_write_u8(b, VALUE_TYPE_BOOL | (el->value_int ? (1 << 5) : 0));
        break;
    case VALUE_TYPE_ARRAY: {
        buf_write_u8(b, VALUE_TYPE_ARRAY);
        buf_write_uleb128(b, el->array_count);
        smali_annot_val_t *node = el->arr_head;
        while (node) {
            write_encoded_value(b, ctx, &node->elem);
            node = node->next;
        }
        break;
    }
    case VALUE_TYPE_ANNOT: {
        buf_write_u8(b, VALUE_TYPE_ANNOT);
        uint32_t sub_type_idx = el->annot_type ? smali_pool_find(&ctx->types, el->annot_type) : 0;
        if (sub_type_idx == 0xFFFFFFFF) sub_type_idx = 0;
        buf_write_uleb128(b, sub_type_idx);
        buf_write_uleb128(b, (uint32_t)el->annot_elem_count);
        for (int i = 0; i < el->annot_elem_count; i++) {
            uint32_t nidx = el->sub_elems[i].name ? smali_pool_find(&ctx->strings, el->sub_elems[i].name) : 0;
            if (nidx == 0xFFFFFFFF) nidx = 0;
            buf_write_uleb128(b, nidx);
            write_encoded_value(b, ctx, &el->sub_elems[i]);
        }
        break;
    }
    default:
        buf_write_u8(b, (0 << 5) | VALUE_TYPE_INT);
        buf_write_u8(b, 0);
        break;
    }
}

uint32_t write_encoded_annotation(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_t *a) {
    uint32_t off = b->len;
    uint32_t type_idx = a->type ? smali_pool_find(&ctx->types, a->type) : 0;
    if (type_idx == 0xFFFFFFFF) type_idx = 0;
    buf_write_uleb128(b, type_idx);
    buf_write_uleb128(b, a->elem_count);
    for (uint32_t i = 0; i < a->elem_count && i < MAX_ANNOT_ELEMS; i++) {
        uint32_t nidx = a->elems[i].name ? smali_pool_find(&ctx->strings, a->elems[i].name) : 0;
        if (nidx == 0xFFFFFFFF) nidx = 0;
        buf_write_uleb128(b, nidx);
        write_encoded_value(b, ctx, &a->elems[i]);
    }
    return off;
}
