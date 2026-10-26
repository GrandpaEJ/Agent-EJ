#include "smali_order.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEX_ID_LIMIT 64000   /* 65536 minus headroom for refs only found at write time */

/* Adds the reference types (L..; and [..) in a "(params)ret" signature to a set. */
static void add_sig_types(smali_pool_strings_t *t, const char *sig) {
    for (const char *p = sig; *p; ) {
        if (*p == 'L' || *p == '[') {
            const char *s = p;
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; }
            if (*p) p++;
            char *d = strndup(s, (size_t)(p - s));
            if (d) { smali_pool_add(t, d); free(d); }
        } else p++;
    }
}

static void add_method_refs(smali_ctx_def_t *ctx, smali_class_def_t *c, smali_method_def_t *ma, uint32_t mc,
                            smali_pool_strings_t *m, smali_pool_strings_t *f, smali_pool_strings_t *t) {
    (void)ctx;
    for (uint32_t j = 0; j < mc; j++) {
        char key[4096];
        snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, ma[j].name, ma[j].signature);
        smali_pool_add(m, key);
        add_sig_types(t, ma[j].signature);
        for (uint32_t k = 0; k < ma[j].insns_count; k++) {
            smali_insn_t *in = &ma[j].insns[k];
            if (!in->ref_str) continue;
            if (in->kind == 4 && strstr(in->ref_str, "->")) {
                smali_pool_add(m, in->ref_str);
                add_sig_types(t, strchr(in->ref_str, '(') ? strchr(in->ref_str, '(') : "");
                const char *arrow = strstr(in->ref_str, "->");
                char *cls = strndup(in->ref_str, (size_t)(arrow - in->ref_str));
                if (cls) { smali_pool_add(t, cls); free(cls); }
            } else if (in->kind == 3 && strstr(in->ref_str, "->")) {
                smali_pool_add(f, in->ref_str);
                const char *arrow = strstr(in->ref_str, "->"), *colon = strchr(arrow, ':');
                char *cls = strndup(in->ref_str, (size_t)(arrow - in->ref_str));
                if (cls) { smali_pool_add(t, cls); free(cls); }
                if (colon) add_sig_types(t, colon + 1);
            } else if (in->kind == 2) {
                smali_pool_add(t, in->ref_str);
            }
        }
    }
}

uint32_t smali_next_dex_group(smali_ctx_def_t *ctx, uint32_t start) {
    smali_pool_strings_t m, f, t;
    smali_pool_init(&m); smali_pool_init(&f); smali_pool_init(&t);
    uint32_t end = ctx->class_count;
    for (uint32_t i = start; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        smali_pool_add(&t, c->descriptor);
        if (c->super_class) smali_pool_add(&t, c->super_class);
        for (uint32_t k = 0; k < c->interface_count; k++) smali_pool_add(&t, c->interfaces[k]);
        for (int ft = 0; ft < 2; ft++) {
            smali_field_def_t *fa = ft ? c->instance_fields : c->static_fields;
            uint32_t fc = ft ? c->instance_field_count : c->static_field_count;
            for (uint32_t j = 0; j < fc; j++) {
                char key[4096];
                snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, fa[j].name, fa[j].type);
                smali_pool_add(&f, key);
                add_sig_types(&t, fa[j].type);
            }
        }
        add_method_refs(ctx, c, c->direct_methods, c->direct_method_count, &m, &f, &t);
        add_method_refs(ctx, c, c->virtual_methods, c->virtual_method_count, &m, &f, &t);
        if (i > start && (m.count > DEX_ID_LIMIT || f.count > DEX_ID_LIMIT || t.count > DEX_ID_LIMIT)) {
            end = i;
            break;
        }
    }
    smali_pool_free(&m); smali_pool_free(&f); smali_pool_free(&t);
    return end;
}

void smali_dex_part_name(const char *out_dex, uint32_t part, char *buf, size_t sz) {
    if (part == 0) { snprintf(buf, sz, "%s", out_dex); return; }
    size_t len = strlen(out_dex);
    if (len > 4 && strcmp(out_dex + len - 4, ".dex") == 0) len -= 4;
    size_t digits = 0;
    while (digits < len && out_dex[len - 1 - digits] >= '0' && out_dex[len - 1 - digits] <= '9') digits++;
    uint32_t first = digits ? (uint32_t)atoi(out_dex + len - digits) : 1;
    snprintf(buf, sz, "%.*s%u.dex", (int)(len - digits), out_dex, first + part);
}
