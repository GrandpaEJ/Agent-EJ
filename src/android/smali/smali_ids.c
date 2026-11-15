#include "smali_ids.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void smali_id_layout(const smali_ctx_def_t *ctx, smali_id_layout_t *l) {
    l->string_ids = 0x70;
    l->type_ids   = l->string_ids + ctx->strings.count * 4;
    l->proto_ids  = l->type_ids + ctx->types.count * 4;
    l->field_ids  = l->proto_ids + ctx->protos.count * 12;
    l->method_ids = l->field_ids + ctx->fields.count * 8;
    l->class_defs = l->method_ids + ctx->methods.count * 8;
    l->data       = l->class_defs + ctx->class_count * 32;
}

static void put_u32(smali_buf_t *b, uint32_t at, uint32_t v) { memcpy(b->buf + at, &v, 4); }
static void put_u16(smali_buf_t *b, uint32_t at, uint16_t v) { memcpy(b->buf + at, &v, 2); }

/* One MUTF-8 sequence at up[j]: returns its byte length and stores the
 * number of UTF-16 units it encodes. A malformed or truncated sequence
 * counts as a single byte so the size and the bytes always agree (G15). */
static size_t mutf8_decode(const uint8_t *up, size_t len, size_t j, size_t *units) {
    *units = 1;
    if ((up[j] & 0x80) == 0) return 1;
    if ((up[j] & 0xE0) == 0xC0) {
        if (j + 1 < len && (up[j + 1] & 0xC0) == 0x80) return 2;
    } else if ((up[j] & 0xF0) == 0xE0) {
        if (j + 2 < len && (up[j + 1] & 0xC0) == 0x80 && (up[j + 2] & 0xC0) == 0x80) return 3;
    } else if ((up[j] & 0xF8) == 0xF0) {
        if (j + 3 < len && (up[j + 1] & 0xC0) == 0x80 &&
            (up[j + 2] & 0xC0) == 0x80 && (up[j + 3] & 0xC0) == 0x80) {
            *units = 2;                  /* supplementary: a surrogate pair */
            return 4;
        }
    }
    return 1;
}

static void write_surrogate(smali_buf_t *b, uint16_t u) {
    buf_write_u8(b, 0xE0 | ((u >> 12) & 0x0F));
    buf_write_u8(b, 0x80 | ((u >> 6) & 0x3F));
    buf_write_u8(b, 0x80 | (u & 0x3F));
}

static void write_one_string(smali_buf_t *b, const char *str) {
    const uint8_t *up = (const uint8_t *)str;
    size_t len = strlen(str), utf16 = 0, units;
    for (size_t j = 0; j < len; j += mutf8_decode(up, len, j, &units)) {
        mutf8_decode(up, len, j, &units);
        utf16 += units;
    }
    buf_write_uleb128(b, (uint32_t)utf16);
    for (size_t j = 0; j < len; ) {
        size_t n = mutf8_decode(up, len, j, &units);
        if (n == 4) {                    /* 4-byte UTF-8 -> two 3-byte surrogates */
            uint32_t cp = ((up[j] & 0x07) << 18) | ((up[j + 1] & 0x3F) << 12) |
                          ((up[j + 2] & 0x3F) << 6) | (up[j + 3] & 0x3F);
            uint32_t u = cp - 0x10000;
            write_surrogate(b, (uint16_t)((u >> 10) + 0xD800));
            write_surrogate(b, (uint16_t)((u & 0x3FF) + 0xDC00));
        } else {
            buf_write(b, up + j, (uint32_t)n);
        }
        j += n;
    }
    buf_write_u8(b, 0);
}

uint32_t smali_write_string_data(smali_ctx_def_t *ctx, smali_buf_t *b, const smali_id_layout_t *l) {
    uint32_t first = b->len;
    for (uint32_t i = 0; i < ctx->strings.count; i++) {
        put_u32(b, l->string_ids + i * 4, b->len);
        write_one_string(b, ctx->strings.strings[i]);
    }
    return first;
}

/* "(ILjava/lang/String;)V" -> "VIL" */
static void make_shorty(const char *sig, char *shorty, size_t cap) {
    size_t n = 0;
    const char *close = strchr(sig, ')');
    char ret = close ? close[1] : 'V';
    shorty[n++] = (ret == 'L' || ret == '[') ? 'L' : ret;
    for (const char *p = sig + (*sig == '('); *p && *p != ')' && n + 1 < cap; ) {
        if (*p == 'L' || *p == '[') {
            shorty[n++] = 'L';
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; }
            if (*p) p++;
        } else {
            shorty[n++] = *p++;
        }
    }
    shorty[n] = '\0';
}

void smali_write_ids(smali_ctx_def_t *ctx, smali_buf_t *b, const smali_id_layout_t *l,
                     const uint32_t *proto_params) {
    for (uint32_t i = 0; i < ctx->types.count; i++)
        put_u32(b, l->type_ids + i * 4, smali_pool_find(&ctx->strings, ctx->types.strings[i]));
    for (uint32_t i = 0; i < ctx->protos.count; i++) {
        const char *sig = ctx->protos.strings[i];
        const char *close = strchr(sig, ')');
        char shorty[512];
        make_shorty(sig, shorty, sizeof(shorty));
        uint32_t at = l->proto_ids + i * 12;
        put_u32(b, at, smali_pool_find(&ctx->strings, shorty));
        put_u32(b, at + 4, smali_pool_find(&ctx->types, close ? close + 1 : "V"));
        put_u32(b, at + 8, proto_params[i]);
    }
    for (int meth = 0; meth < 2; meth++) {
        smali_pool_strings_t *pool = meth ? &ctx->methods : &ctx->fields;
        uint32_t base = meth ? l->method_ids : l->field_ids;
        for (uint32_t i = 0; i < pool->count; i++) {
            /* Malformed keys ("??", "Lcls;->name" without a type) can come from
             * unresolvable refs; the writer must not read the missing parts. */
            const char *key = pool->strings[i];
            const char *arrow = key ? strstr(key, "->") : NULL;
            const char *sep = key ? strchr(key, meth ? '(' : ':') : NULL;
            if (!arrow || !sep || sep <= arrow + 2) {
                fprintf(stderr, "WARNING: skipping malformed %s key '%s'\n",
                        meth ? "method" : "field", key ? key : "(null)");
                continue;
            }
            char *cls = strndup(key, (size_t)(arrow - key));
            char *name = strndup(arrow + 2, (size_t)(sep - (arrow + 2)));
            uint32_t at = base + i * 8;
            put_u16(b, at, (uint16_t)smali_pool_find(&ctx->types, cls));
            put_u16(b, at + 2, (uint16_t)(meth ? smali_pool_find(&ctx->protos, sep)
                                               : smali_pool_find(&ctx->types, sep + 1)));
            put_u32(b, at + 4, smali_pool_find(&ctx->strings, name));
            free(cls); free(name);
        }
    }
}
