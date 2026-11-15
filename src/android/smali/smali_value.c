#include "smali_value.h"
#include "smali_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The encodings below are dexlib2's (DexDataWriter): the payload is the
 * shortest that still represents the value, so the bytes match smali.jar. */

static void write_header(smali_buf_t *b, int type, int nbytes) {
    buf_write_u8(b, (uint8_t)(((nbytes - 1) << 5) | type));
}

/* Signed value in the fewest bytes that keep its sign. */
static void write_sint(smali_buf_t *b, int type, int64_t v) {
    uint8_t tmp[8];
    int n = 0;
    if (v >= 0) { while (v > 0x7f && n < 7) { tmp[n++] = (uint8_t)v; v >>= 8; } }
    else        { while (v < -0x80 && n < 7) { tmp[n++] = (uint8_t)v; v >>= 8; } }
    tmp[n++] = (uint8_t)v;
    write_header(b, type, n);
    buf_write(b, tmp, (uint32_t)n);
}

/* Unsigned value (char, pool index) in the fewest bytes, at least one. */
static void write_uint(smali_buf_t *b, int type, uint32_t v) {
    uint8_t tmp[4];
    int n = 0;
    while (v > 0xff) { tmp[n++] = (uint8_t)v; v >>= 8; }
    tmp[n++] = (uint8_t)v;
    write_header(b, type, n);
    buf_write(b, tmp, (uint32_t)n);
}

/* Float/double bits with the low-order zero bytes dropped (at least one). */
static void write_right_ext(smali_buf_t *b, int type, uint64_t bits, int width) {
    int skip = 0;
    while (skip < width - 1 && ((bits >> (8 * skip)) & 0xff) == 0) skip++;
    write_header(b, type, width - skip);
    for (int k = skip; k < width; k++) buf_write_u8(b, (uint8_t)(bits >> (8 * k)));
}

static uint32_t find_or_zero(smali_pool_strings_t *pool, const char *s, const char *what) {
    uint32_t idx = s ? smali_pool_find(pool, s) : 0xFFFFFFFF;
    if (idx == 0xFFFFFFFF) {
        if (what) fprintf(stderr, "WARNING: unresolved %s reference '%s'\n", what, s ? s : "(null)");
        return 0;
    }
    return idx;
}

typedef struct { uint32_t name; smali_annotation_elem_t *el; } named_elem_t;

static int cmp_named(const void *a, const void *b) {
    uint32_t x = ((const named_elem_t *)a)->name, y = ((const named_elem_t *)b)->name;
    return x < y ? -1 : x > y;
}

/* Elements of an annotation are written sorted by name (dexlib2 BY_NAME). */
static void write_elements(smali_buf_t *b, smali_ctx_def_t *ctx,
                           smali_annotation_elem_t *els, uint32_t n) {
    buf_write_uleb128(b, n);
    if (!n) return;
    named_elem_t *v = malloc(n * sizeof(*v));
    if (!v) return;
    for (uint32_t i = 0; i < n; i++) {
        v[i].name = find_or_zero(&ctx->strings, els[i].name, NULL);
        v[i].el = &els[i];
    }
    qsort(v, n, sizeof(*v), cmp_named);
    for (uint32_t i = 0; i < n; i++) {
        buf_write_uleb128(b, v[i].name);
        write_encoded_value(b, ctx, v[i].el);
    }
    free(v);
}

void write_encoded_value(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_elem_t *el) {
    int t = el->value_type;
    switch (t) {
    case VALUE_TYPE_BYTE:  write_sint(b, t, (int8_t)el->value_int); break;
    case VALUE_TYPE_SHORT: write_sint(b, t, (int16_t)el->value_int); break;
    case VALUE_TYPE_CHAR:  write_uint(b, t, (uint16_t)el->value_int); break;
    case VALUE_TYPE_INT:   write_sint(b, t, (int32_t)el->value_int); break;
    case VALUE_TYPE_LONG:  write_sint(b, t, el->value_int); break;
    case VALUE_TYPE_FLOAT: {
        float fv = (float)el->value_double;
        uint32_t bits;
        memcpy(&bits, &fv, 4);
        write_right_ext(b, t, bits, 4);
        break;
    }
    case VALUE_TYPE_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, &el->value_double, 8);
        write_right_ext(b, t, bits, 8);
        break;
    }
    case VALUE_TYPE_STRING: write_uint(b, t, find_or_zero(&ctx->strings, el->value_str, NULL)); break;
    case VALUE_TYPE_TYPE:   write_uint(b, t, find_or_zero(&ctx->types, el->value_str, NULL)); break;
    case VALUE_TYPE_ENUM:   write_uint(b, t, find_or_zero(&ctx->fields, el->value_str, "enum")); break;
    case VALUE_TYPE_FIELD:  write_uint(b, t, find_or_zero(&ctx->fields, el->value_str, "field")); break;
    case VALUE_TYPE_METHOD: write_uint(b, t, find_or_zero(&ctx->methods, el->value_str, NULL)); break;
    case VALUE_TYPE_NULL:
        buf_write_u8(b, VALUE_TYPE_NULL);
        break;
    case VALUE_TYPE_BOOL:
        buf_write_u8(b, VALUE_TYPE_BOOL | (el->value_int ? (1 << 5) : 0));
        break;
    case VALUE_TYPE_ARRAY: {
        buf_write_u8(b, VALUE_TYPE_ARRAY);
        buf_write_uleb128(b, el->array_count);
        for (smali_annot_val_t *node = el->arr_head; node; node = node->next)
            write_encoded_value(b, ctx, &node->elem);
        break;
    }
    case VALUE_TYPE_ANNOT:
        buf_write_u8(b, VALUE_TYPE_ANNOT);
        buf_write_uleb128(b, find_or_zero(&ctx->types, el->annot_type, NULL));
        write_elements(b, ctx, el->sub_elems,
                       el->annot_elem_count > 0 ? (uint32_t)el->annot_elem_count : 0);
        break;
    default:
        write_sint(b, VALUE_TYPE_INT, 0);
        break;
    }
}

uint32_t write_encoded_annotation(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_t *a) {
    uint32_t off = b->len;
    buf_write_uleb128(b, find_or_zero(&ctx->types, a->type, NULL));
    write_elements(b, ctx, a->elems, a->elem_count < MAX_ANNOT_ELEMS ? a->elem_count : MAX_ANNOT_ELEMS);
    return off;
}
