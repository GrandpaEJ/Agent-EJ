#include "format_dex_internal.h"
#include <stdlib.h>
#include <string.h>

static void r32(const uint8_t *p, uint32_t *v) {
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void r16(const uint8_t *p, uint16_t *v) {
    *v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int dex_read_uleb128(const uint8_t *data, uint32_t *val) {
    *val = 0; int shift = 0, i = 0;
    while (1) {
        *val |= (uint32_t)(data[i] & 0x7F) << shift;
        if (!(data[i++] & 0x80)) break;
        shift += 7;
    }
    return i;
}

static char *read_dex_string(const uint8_t *data, size_t size, uint32_t off) {
    if (off >= size) return NULL;
    uint32_t len;
    int adv = dex_read_uleb128(data + off, &len);
    off += adv;
    if (off + len > size) return NULL;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, data + off, len);
    s[len] = '\0';
    return s;
}

void dex_free(dex_ctx *ctx);

static int parse_dex_strings(dex_ctx *ctx) {
    ctx->sp.strings = malloc(ctx->string_count * sizeof(char *));
    if (!ctx->sp.strings) return -1;
    ctx->sp.count = (int)ctx->string_count;
    ctx->sp.cap = (int)ctx->string_count;
    for (uint32_t i = 0; i < ctx->string_count; i++) {
        ctx->sp.strings[i] = read_dex_string(ctx->data, ctx->size, ctx->string_ids[i]);
        if (!ctx->sp.strings[i]) {
            ctx->sp.strings[i] = strdup("");
            if (!ctx->sp.strings[i]) return -1;
        }
    }
    return 0;
}

static int parse_dex_types(dex_ctx *ctx) {
    ctx->type_ids = malloc(ctx->type_count * sizeof(int));
    if (!ctx->type_ids) return -1;
    for (uint32_t i = 0; i < ctx->type_count; i++) {
        uint32_t idx;
        r32(ctx->data + ctx->type_ids_off + i * 4, &idx);
        ctx->type_ids[i] = (int)idx;
    }
    return 0;
}

static int parse_dex_protos(dex_ctx *ctx) {
    if (ctx->proto_count == 0) return 0;
    ctx->protos = malloc(ctx->proto_count * sizeof(dex_proto));
    if (!ctx->protos) return -1;
    for (uint32_t i = 0; i < ctx->proto_count; i++) {
        uint32_t po = ctx->proto_ids_off + i * 12;
        r32(ctx->data + po, &ctx->protos[i].shorty_idx);
        r32(ctx->data + po + 4, &ctx->protos[i].return_type_idx);
        r32(ctx->data + po + 8, &ctx->protos[i].params_off);
    }
    return 0;
}

static int parse_dex_fields(dex_ctx *ctx) {
    if (ctx->field_count == 0) return 0;
    ctx->fields = malloc(ctx->field_count * sizeof(dex_field));
    if (!ctx->fields) return -1;
    for (uint32_t i = 0; i < ctx->field_count; i++) {
        uint32_t fo = ctx->field_ids_off + i * 8;
        uint16_t c, t; uint32_t n;
        r16(ctx->data + fo, &c);
        r16(ctx->data + fo + 2, &t);
        r32(ctx->data + fo + 4, &n);
        ctx->fields[i].class_idx = c;
        ctx->fields[i].type_idx = t;
        ctx->fields[i].name_idx = n;
    }
    return 0;
}

static int parse_dex_methods(dex_ctx *ctx) {
    if (ctx->method_count == 0) return 0;
    ctx->methods = malloc(ctx->method_count * sizeof(dex_method_id));
    if (!ctx->methods) return -1;
    for (uint32_t i = 0; i < ctx->method_count; i++) {
        uint32_t mo = ctx->method_ids_off + i * 8;
        uint16_t c, p; uint32_t n;
        r16(ctx->data + mo, &c);
        r16(ctx->data + mo + 2, &p);
        r32(ctx->data + mo + 4, &n);
        ctx->methods[i].class_idx = c;
        ctx->methods[i].proto_idx = p;
        ctx->methods[i].name_idx = n;
    }
    return 0;
}

static int parse_class_data(dex_ctx *ctx, dex_class *cls) {
    cls->direct = NULL; cls->virtual = NULL;
    cls->static_fields = NULL; cls->instance_fields = NULL;
    if (cls->class_data_off == 0) return 0;

    const uint8_t *p = ctx->data + cls->class_data_off;
    const uint8_t *end = ctx->data + ctx->size;
    if (p >= end) return 0;

    uint32_t sf, inf, dm, vm;
    p += dex_read_uleb128(p, &sf);
    p += dex_read_uleb128(p, &inf);
    p += dex_read_uleb128(p, &dm);
    p += dex_read_uleb128(p, &vm);
    cls->static_count = (int)sf;
    cls->instance_count = (int)inf;
    cls->direct_count = (int)dm;
    cls->virtual_count = (int)vm;

    if (sf > 0) {
        cls->static_fields = malloc(sf * sizeof(dex_field_enc));
        if (!cls->static_fields) return -1;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < sf; i++) {
            p += dex_read_uleb128(p, &cls->static_fields[i].field_idx);
            p += dex_read_uleb128(p, &cls->static_fields[i].access_flags);
            cls->static_fields[i].field_idx += prev;
            prev = cls->static_fields[i].field_idx;
        }
    }
    if (inf > 0) {
        cls->instance_fields = malloc(inf * sizeof(dex_field_enc));
        if (!cls->instance_fields) return -1;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < inf; i++) {
            p += dex_read_uleb128(p, &cls->instance_fields[i].field_idx);
            p += dex_read_uleb128(p, &cls->instance_fields[i].access_flags);
            cls->instance_fields[i].field_idx += prev;
            prev = cls->instance_fields[i].field_idx;
        }
    }
    if (dm > 0) {
        cls->direct = malloc(dm * sizeof(dex_method_enc));
        if (!cls->direct) return -1;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < dm; i++) {
            p += dex_read_uleb128(p, &cls->direct[i].method_idx);
            p += dex_read_uleb128(p, &cls->direct[i].access_flags);
            p += dex_read_uleb128(p, &cls->direct[i].code_off);
            cls->direct[i].method_idx += prev;
            prev = cls->direct[i].method_idx;
        }
    }
    if (vm > 0) {
        cls->virtual = malloc(vm * sizeof(dex_method_enc));
        if (!cls->virtual) return -1;
        uint32_t prev = 0;
        for (uint32_t i = 0; i < vm; i++) {
            p += dex_read_uleb128(p, &cls->virtual[i].method_idx);
            p += dex_read_uleb128(p, &cls->virtual[i].access_flags);
            p += dex_read_uleb128(p, &cls->virtual[i].code_off);
            cls->virtual[i].method_idx += prev;
            prev = cls->virtual[i].method_idx;
        }
    }
    return 0;
}

dex_ctx *dex_parse(const uint8_t *data, size_t size) {
    if (!data || size < 0x70) return NULL;
    uint32_t m0, m1;
    r32(data, &m0); r32(data + 4, &m1);
    if (m0 != 0x0A786564) return NULL;
    /* accept dex 035..039+ : bytes 4..7 are '0','3','<digit>','\0' */
    if ((m1 & 0x0000FFFFu) != 0x00003330u) return NULL;
    if (((m1 >> 24) & 0xFFu) != 0x00u) return NULL;
    {
        uint32_t dver = (m1 >> 16) & 0xFFu;
        if (dver < '0' || dver > '9') return NULL;
    }

    dex_ctx *ctx = calloc(1, sizeof(dex_ctx));
    if (!ctx) return NULL;
    ctx->data = data;
    ctx->size = size;

    uint32_t siz, sio, tiz, tio, piz, pio;
    uint32_t fiz, fio, miz, mio, ciz, cio;
    r32(data + 56, &siz); r32(data + 60, &sio);
    r32(data + 64, &tiz); r32(data + 68, &tio);
    r32(data + 72, &piz); r32(data + 76, &pio);
    r32(data + 80, &fiz); r32(data + 84, &fio);
    r32(data + 88, &miz); r32(data + 92, &mio);
    r32(data + 96, &ciz); r32(data + 100, &cio);

    ctx->string_count = siz;
    ctx->string_ids_off = sio;
    ctx->type_count = tiz;
    ctx->type_ids_off = tio;
    ctx->proto_count = piz;
    ctx->proto_ids_off = pio;
    ctx->field_count = fiz;
    ctx->field_ids_off = fio;
    ctx->method_count = miz;
    ctx->method_ids_off = mio;
    ctx->class_count = ciz;
    ctx->class_defs_off = cio;

    if (siz > 0 && sio + siz * 4 <= size) {
        ctx->string_ids = malloc(siz * sizeof(uint32_t));
        if (!ctx->string_ids) { free(ctx); return NULL; }
        for (uint32_t i = 0; i < siz; i++)
            r32(data + sio + i * 4, &ctx->string_ids[i]);
    }

    if (parse_dex_strings(ctx)) { dex_free(ctx); return NULL; }
    if (parse_dex_types(ctx))   { dex_free(ctx); return NULL; }
    if (parse_dex_protos(ctx))  { dex_free(ctx); return NULL; }
    if (parse_dex_fields(ctx))  { dex_free(ctx); return NULL; }
    if (parse_dex_methods(ctx)) { dex_free(ctx); return NULL; }

    if (ciz > 0) {
        ctx->classes = calloc(ciz, sizeof(dex_class));
        if (!ctx->classes) { dex_free(ctx); return NULL; }
        for (uint32_t i = 0; i < ciz; i++) {
            uint32_t co = cio + i * 0x20;
            r32(data + co, &ctx->classes[i].class_idx);
            r32(data + co + 4, &ctx->classes[i].access_flags);
            r32(data + co + 8, &ctx->classes[i].superclass_idx);
            r32(data + co + 12, &ctx->classes[i].interfaces_off);
            r32(data + co + 16, &ctx->classes[i].source_file_idx);
            r32(data + co + 20, &ctx->classes[i].annotations_off);
            r32(data + co + 24, &ctx->classes[i].class_data_off);
            r32(data + co + 28, &ctx->classes[i].static_values_off);
            if (parse_class_data(ctx, &ctx->classes[i]))
                { dex_free(ctx); return NULL; }
        }
    }

    return ctx;
}

void dex_free(dex_ctx *ctx) {
    if (!ctx) return;
    free(ctx->string_ids);
    free(ctx->type_ids);
    free(ctx->protos);
    free(ctx->fields);
    free(ctx->methods);
    for (int i = 0; i < ctx->sp.count; i++)
        free(ctx->sp.strings[i]);
    free(ctx->sp.strings);
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        free(ctx->classes[i].direct);
        free(ctx->classes[i].virtual);
        free(ctx->classes[i].static_fields);
        free(ctx->classes[i].instance_fields);
    }
    free(ctx->classes);
    free(ctx);
}
