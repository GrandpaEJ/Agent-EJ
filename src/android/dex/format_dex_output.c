#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t slen = strlen(s);
    if (*len + slen + 1 > *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
    return 0;
}

static int append_fmt(char **buf, size_t *len, size_t *cap,
                      const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return -1;
    if (*len + (size_t)needed + 1 > *cap) {
        while (*cap < *len + (size_t)needed + 1)
            *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += needed;
    return 0;
}

char *dex_dump(dex_ctx *ctx) {
    if (!ctx) return NULL;

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    append_fmt(&buf, &len, &cap,
        "DEX: %u strings, %u types, %u protos, %u fields, %u methods, %u classes\n",
        ctx->string_count, ctx->type_count, ctx->proto_count,
        ctx->field_count, ctx->method_count, ctx->class_count);

    for (uint32_t i = 0; i < ctx->class_count; i++) {
        dex_class *c = &ctx->classes[i];
        const char *cn = ctx->sp.strings[ctx->type_ids[c->class_idx]];
        const char *sc = c->superclass_idx != 0xFFFFFFFF
            ? ctx->sp.strings[ctx->type_ids[c->superclass_idx]]
            : NULL;
        append_fmt(&buf, &len, &cap, "  %s", cn);
        if (sc) append_fmt(&buf, &len, &cap, " extends %s", sc);
        append(&buf, &len, &cap, "\n");

        for (int j = 0; j < c->direct_count; j++) {
            dex_method_enc *m = &c->direct[j];
            dex_method_id *mi = &ctx->methods[m->method_idx];
            const char *mn = ctx->sp.strings[mi->name_idx];
            dex_proto *p = &ctx->protos[mi->proto_idx];
            const char *rt = ctx->sp.strings[ctx->type_ids[p->return_type_idx]];
            append_fmt(&buf, &len, &cap, "    %s %s (direct)\n", rt, mn);
        }
        for (int j = 0; j < c->virtual_count; j++) {
            dex_method_enc *m = &c->virtual[j];
            dex_method_id *mi = &ctx->methods[m->method_idx];
            const char *mn = ctx->sp.strings[mi->name_idx];
            dex_proto *p = &ctx->protos[mi->proto_idx];
            const char *rt = ctx->sp.strings[ctx->type_ids[p->return_type_idx]];
            append_fmt(&buf, &len, &cap, "    %s %s (virtual)\n", rt, mn);
        }
    }

    return buf;
}

uint32_t dex_class_count(dex_ctx *ctx) {
    return ctx ? ctx->class_count : 0;
}

uint32_t dex_string_count(dex_ctx *ctx) {
    return ctx ? ctx->string_count : 0;
}

/* String pool entry idx as decoded UTF-8 ("" when missing). */
const char *dex_string_at(dex_ctx *ctx, uint32_t idx) {
    if (!ctx || idx >= ctx->string_count || idx >= (uint32_t)ctx->sp.count || !ctx->sp.strings[idx]) return "";
    return ctx->sp.strings[idx];
}
