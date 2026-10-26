#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* encoded_value type codes (DEX spec) */
enum { V_BYTE = 0x00, V_SHORT = 0x02, V_CHAR = 0x03, V_INT = 0x04, V_LONG = 0x06,
       V_FLOAT = 0x10, V_DOUBLE = 0x11, V_METHOD_TYPE = 0x15, V_METHOD_HANDLE = 0x16,
       V_STRING = 0x17, V_TYPE = 0x18, V_FIELD = 0x19, V_METHOD = 0x1a, V_ENUM = 0x1b,
       V_ARRAY = 0x1c, V_ANNOTATION = 0x1d, V_NULL = 0x1e, V_BOOL = 0x1f };

/* Advances *pp past one encoded_value. Returns -1 on truncated data. */
int dex_skip_value(const uint8_t **pp, const uint8_t *end) {
    const uint8_t *p = *pp;
    if (p >= end) return -1;
    uint8_t header = *p++;
    int type = header & 0x1f, arg = header >> 5;
    if (type == V_NULL || type == V_BOOL) { *pp = p; return 0; }
    if (type == V_ARRAY || type == V_ANNOTATION) {
        uint32_t n, name;
        if (p >= end) return -1;
        p += dex_read_uleb128(p, &n);          /* array size, or annotation type_idx */
        if (type == V_ANNOTATION) {
            if (p >= end) return -1;
            p += dex_read_uleb128(p, &n);      /* element count */
        }
        for (uint32_t i = 0; i < n; i++) {
            if (type == V_ANNOTATION) {
                if (p >= end) return -1;
                p += dex_read_uleb128(p, &name);
            }
            if (dex_skip_value(&p, end) < 0) return -1;
        }
        *pp = p;
        return 0;
    }
    if (p + arg + 1 > end) return -1;
    *pp = p + arg + 1;
    return 0;
}

static void put_hex(char *buf, size_t sz, int64_t v, const char *suffix) {
    if (v < 0) snprintf(buf, sz, "-0x%llx%s", (unsigned long long)(0 - (uint64_t)v), suffix);
    else snprintf(buf, sz, "0x%llx%s", (unsigned long long)v, suffix);
}

/* Formats one encoded_value the way baksmali writes it. Empty output = unsupported kind. */
int dex_format_value(const uint8_t **pp, const uint8_t *end, char *buf, size_t sz, dex_ctx *ctx) {
    const uint8_t *p = *pp;
    buf[0] = 0;
    if (p >= end) return -1;
    uint8_t header = *p++;
    int type = header & 0x1f, arg = (header >> 5) + 1;
    if (type == V_NULL) { *pp = p; snprintf(buf, sz, "null"); return 0; }
    if (type == V_BOOL) { *pp = p; snprintf(buf, sz, (header >> 5) ? "true" : "false"); return 0; }
    if (type == V_ARRAY || type == V_ANNOTATION) { *pp = p - 1; return dex_skip_value(pp, end); }
    if (p + arg > end) return -1;
    uint64_t raw = 0;
    for (int i = 0; i < arg; i++) raw |= (uint64_t)p[i] << (i * 8);
    *pp = p + arg;

    int64_t sv = (int64_t)raw;                  /* sign-extended: byte/short/int/long */
    if (arg < 8 && (raw >> (arg * 8 - 1)) & 1) sv = (int64_t)(raw | (~0ULL << (arg * 8)));
    switch (type) {
    case V_BYTE:  put_hex(buf, sz, (int8_t)sv, "t"); break;
    case V_SHORT: put_hex(buf, sz, (int16_t)sv, "s"); break;
    case V_CHAR: {
        unsigned c = (unsigned)(raw & 0xFFFF);
        if (c == '\'' || c == '\\') snprintf(buf, sz, "'\\%c'", (int)c);
        else if (c >= 0x20 && c <= 0x7e) snprintf(buf, sz, "'%c'", (int)c);
        else snprintf(buf, sz, "'\\u%04x'", c);
        break; }
    case V_INT:   put_hex(buf, sz, (int32_t)sv, ""); break;
    case V_LONG:  put_hex(buf, sz, sv, "L"); break;
    case V_FLOAT: {                              /* right-zero-extended */
        uint32_t bits = (uint32_t)(raw << ((4 - arg) * 8)); float f; memcpy(&f, &bits, 4);
        if (isnan(f) || isinf(f)) snprintf(buf, sz, "%sf", isnan(f) ? "nan" : (f < 0 ? "-inf" : "inf"));
        else { snprintf(buf, sz, "%.9g", f); if (!strpbrk(buf, ".e")) strcat(buf, ".0"); strcat(buf, "f"); }
        break; }
    case V_DOUBLE: {
        uint64_t bits = raw << ((8 - arg) * 8); double d; memcpy(&d, &bits, 8);
        if (isnan(d) || isinf(d)) snprintf(buf, sz, "%sd", isnan(d) ? "nan" : (d < 0 ? "-inf" : "inf"));
        else { snprintf(buf, sz, "%.17g", d); if (!strpbrk(buf, ".e")) strcat(buf, ".0"); strcat(buf, "d"); }
        break; }
    case V_STRING:
        if (raw < ctx->string_count) snprintf(buf, sz, "\"%s\"", smali_res(ctx, 1, (uint32_t)raw));
        break;
    case V_TYPE:
        if (raw < ctx->type_count) { char *t = smali_tsm(smali_res(ctx, 2, (uint32_t)raw)); snprintf(buf, sz, "%s", t ? t : ""); free(t); }
        break;
    case V_FIELD:  if (raw < ctx->field_count)  snprintf(buf, sz, "%s", smali_res(ctx, 3, (uint32_t)raw)); break;
    case V_METHOD: if (raw < ctx->method_count) snprintf(buf, sz, "%s", smali_res(ctx, 4, (uint32_t)raw)); break;
    case V_ENUM:   if (raw < ctx->field_count)  snprintf(buf, sz, ".enum %s", smali_res(ctx, 3, (uint32_t)raw)); break;
    default: break;
    }
    return 0;
}

/* Writes the initial value of the idx-th static field (from static_values_off) into buf. */
void dex_static_value(dex_ctx *ctx, uint32_t values_off, int idx, char *buf, size_t sz) {
    buf[0] = 0;
    const uint8_t *end = ctx->data + ctx->size, *p = ctx->data + values_off;
    uint32_t n;
    if (p >= end) return;
    p += dex_read_uleb128(p, &n);
    if (idx < 0 || (uint32_t)idx >= n) return;
    for (int i = 0; i < idx; i++)
        if (dex_skip_value(&p, end) < 0) return;
    dex_format_value(&p, end, buf, sz, ctx);
}
