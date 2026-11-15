#include "smali_codesec.h"
#include "smali_blobset.h"
#include "smali_code.h"
#include "smali_debug.h"
#include "smali_error.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t method_count(const smali_class_def_t *c) {
    return c->direct_method_count + c->virtual_method_count;
}

static smali_method_def_t *method_at(smali_class_def_t *c, uint32_t k) {
    return k < c->direct_method_count ? &c->direct_methods[k]
                                      : &c->virtual_methods[k - c->direct_method_count];
}

/* Same test as write_code_item: only these methods get a code_item. */
static int has_code(const smali_method_def_t *m) {
    return !(m->access_flags & 0x0500) && m->insns_count > 0 &&
           m->insns_count <= 65536 && m->insns;
}

/* Debug items go first, each shared by every method whose encoding is
 * byte-identical (dexlib2 DebugInfoCache); the code_items follow. */
static void write_debug(smali_codesec_t *cs, smali_ctx_def_t *ctx, smali_buf_t *b,
                        const uint32_t *order, uint32_t **dbg) {
    smali_blobset_t seen;
    smali_blobset_init(&seen);
    smali_buf_t one;
    buf_init(&one);
    cs->debug_first = b->len;
    for (uint32_t o = 0; o < ctx->class_count; o++) {
        smali_class_def_t *c = &ctx->classes[order[o]];
        for (uint32_t k = 0; k < method_count(c); k++) {
            smali_method_def_t *m = method_at(c, k);
            if (!has_code(m)) continue;
            one.len = 0;
            write_debug_info(ctx, &one, m);
            if (one.len == 0) continue;       /* no debug information */
            uint32_t id = smali_blobset_find(&seen, one.buf, one.len);
            if (id == UINT32_MAX) {
                id = smali_blobset_add(&seen, one.buf, one.len);
                if (id == UINT32_MAX) continue;
                seen.items[id].off = b->len;
                buf_write(b, one.buf, one.len);
                cs->debug_count++;
            }
            dbg[order[o]][k] = seen.items[id].off;
        }
    }
    buf_free(&one);
    smali_blobset_free(&seen);
}

int smali_codesec_write(smali_codesec_t *cs, smali_ctx_def_t *ctx, smali_buf_t *b,
                        const uint32_t *order) {
    uint32_t n = ctx->class_count;
    memset(cs, 0, sizeof(*cs));
    cs->code_off = calloc(n ? n : 1, sizeof(uint32_t *));
    uint32_t **dbg = calloc(n ? n : 1, sizeof(uint32_t *));
    if (!cs->code_off || !dbg) { free(dbg); return -1; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t mc = method_count(&ctx->classes[i]);
        cs->code_off[i] = calloc(mc ? mc : 1, sizeof(uint32_t));
        dbg[i] = calloc(mc ? mc : 1, sizeof(uint32_t));
        if (!cs->code_off[i] || !dbg[i]) goto oom;
    }
    write_debug(cs, ctx, b, order, dbg);

    int failed = 0;
    align_4(b);
    for (uint32_t o = 0; o < n; o++) {
        smali_class_def_t *c = &ctx->classes[order[o]];
        if (c->src_path) smali_set_source_name(c->src_path);
        for (uint32_t k = 0; k < method_count(c); k++) {
            uint32_t off = 0;
            if (write_code_item(ctx, b, method_at(c, k), &off) != 0) { failed = 1; continue; }
            cs->code_off[order[o]][k] = off;
            if (!off) continue;
            if (!cs->code_count++) cs->code_first = off;
            uint32_t d = dbg[order[o]][k];
            memcpy(b->buf + off + 8, &d, 4);
        }
    }
    for (uint32_t i = 0; i < n; i++) free(dbg[i]);
    free(dbg);
    return failed ? -1 : 0;
oom:
    for (uint32_t i = 0; i < n; i++) free(dbg[i]);
    free(dbg);
    return -1;
}

static void write_members(smali_buf_t *b, smali_ctx_def_t *ctx, const char *cls,
                          smali_field_def_t *f, uint32_t nf) {
    uint32_t last = 0;
    for (uint32_t j = 0; j < nf; j++) {
        char key[4096];
        snprintf(key, sizeof(key), "%s->%s:%s", cls, f[j].name, f[j].type);
        uint32_t idx = smali_pool_find(&ctx->fields, key);
        buf_write_uleb128(b, idx - last);
        buf_write_uleb128(b, f[j].access_flags);
        last = idx;
    }
}

static void write_methods(smali_buf_t *b, smali_ctx_def_t *ctx, const char *cls,
                          smali_method_def_t *m, uint32_t nm, const uint32_t *code) {
    uint32_t last = 0;
    for (uint32_t j = 0; j < nm; j++) {
        char key[4096];
        snprintf(key, sizeof(key), "%s->%s%s", cls, m[j].name, m[j].signature);
        uint32_t idx = smali_pool_find(&ctx->methods, key);
        buf_write_uleb128(b, idx - last);
        buf_write_uleb128(b, m[j].access_flags);
        buf_write_uleb128(b, code[j]);
        last = idx;
    }
}

int smali_classdata_write(smali_codesec_t *cs, smali_ctx_def_t *ctx, smali_buf_t *b) {
    cs->class_data_off = calloc(ctx->class_count ? ctx->class_count : 1, sizeof(uint32_t));
    if (!cs->class_data_off) return -1;
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        if (!c->static_field_count && !c->instance_field_count && !method_count(c)) continue;
        cs->class_data_off[i] = b->len;
        if (!cs->class_data_count++) cs->class_data_first = b->len;
        buf_write_uleb128(b, c->static_field_count);
        buf_write_uleb128(b, c->instance_field_count);
        buf_write_uleb128(b, c->direct_method_count);
        buf_write_uleb128(b, c->virtual_method_count);
        write_members(b, ctx, c->descriptor, c->static_fields, c->static_field_count);
        write_members(b, ctx, c->descriptor, c->instance_fields, c->instance_field_count);
        write_methods(b, ctx, c->descriptor, c->direct_methods, c->direct_method_count,
                      cs->code_off[i]);
        write_methods(b, ctx, c->descriptor, c->virtual_methods, c->virtual_method_count,
                      cs->code_off[i] + c->direct_method_count);
    }
    return 0;
}

void smali_codesec_free(smali_codesec_t *cs, uint32_t class_count) {
    if (cs->code_off)
        for (uint32_t i = 0; i < class_count; i++) free(cs->code_off[i]);
    free(cs->code_off);
    free(cs->class_data_off);
    memset(cs, 0, sizeof(*cs));
}
