#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int smali_uleb(const uint8_t *data, uint32_t *val, const uint8_t **next);

char *dex_to_smali_class(dex_ctx *ctx, uint32_t ci) {
    if (ci >= ctx->class_count) return NULL;
    dex_class *c = &ctx->classes[ci];
    uint32_t ti = ctx->type_ids[c->class_idx];
    const char *cn = ti < (uint32_t)ctx->sp.count ? ctx->sp.strings[ti] : "??";
    const char *sp = NULL;
    if (c->superclass_idx != 0xFFFFFFFF && c->superclass_idx < ctx->type_count) {
        uint32_t si = ctx->type_ids[c->superclass_idx];
        if (si < (uint32_t)ctx->sp.count) sp = ctx->sp.strings[si];
    }
    int has_src = c->source_file_idx != 0xFFFFFFFF
                  && c->source_file_idx < (uint32_t)ctx->sp.count;
    const char *src = has_src ? ctx->sp.strings[c->source_file_idx] : NULL;

    char *c2 = smali_tsm(cn), *s2 = sp ? smali_tsm(sp) : NULL;
    size_t cap = 65536, len = 0; char *buf = malloc(cap);
    if (!buf) { free(c2); free(s2); return NULL; }
    buf[0] = 0;
    smali_sb sb = {&buf, &len, &cap};

    /* class header */
    const char *af = smali_aflags(c->access_flags);
    if (af[0])
        smali_sf(&sb, ".class %s %s\n", af, c2 ?: cn);
    else
        smali_sf(&sb, ".class %s\n", c2 ?: cn);
    if (s2) smali_sf(&sb, ".super %s\n", s2);
    if (src) smali_sf(&sb, ".source \"%s\"\n", src);

    /* interfaces */
    if (c->interfaces_off && c->interfaces_off + 4 <= ctx->size) {
        uint32_t ic; memcpy(&ic, ctx->data + c->interfaces_off, 4);
        if (ic > 0) {
            smali_sa(&sb, "\n# interfaces\n");
            for (uint32_t ii = 0; ii < ic; ii++) {
                if (c->interfaces_off + 4 + ii * 2 + 2 > ctx->size) break;
                uint16_t ti2;
                memcpy(&ti2, ctx->data + c->interfaces_off + 4 + ii * 2, 2);
                if (ti2 < ctx->type_count) {
                    const char *iface = ctx->sp.strings[ctx->type_ids[ti2]];
                    char *i2 = smali_tsm(iface);
                    smali_sf(&sb, ".implements %s\n", i2 ?: iface);
                    free(i2);
                }
            }
        }
    }

        /* class-level annotations */
    smali_write_annotations(&sb, ctx, c, 0, 0);

    /* static fields */
    if (c->static_count > 0) {
        smali_sa(&sb, "\n\n# static fields\n");
        for (int fi = 0; fi < c->static_count; fi++) {
            dex_field_enc *fe = &c->static_fields[fi];
            if (fe->field_idx < ctx->field_count) {
                dex_field *f = &ctx->fields[fe->field_idx];
                char *ft = smali_tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                const char *af = smali_aflags(fe->access_flags);
                static char init_val[65536];
                init_val[0] = 0;
                if (c->static_values_off)
                    dex_static_value(ctx, c->static_values_off, fi, init_val, sizeof(init_val));
                if (af[0])
                    smali_sf(&sb, ".field %s %s:%s%s%s\n",
                        af, ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]],
                        init_val[0] ? " = " : "", init_val[0] ? init_val : "");
                else
                    smali_sf(&sb, ".field %s:%s%s%s\n",
                        ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]],
                        init_val[0] ? " = " : "", init_val[0] ? init_val : "");
                free(ft);
                size_t before_annots = len;
                smali_write_annotations(&sb, ctx, c, 1, fe->field_idx);
                if (len != before_annots) smali_sa(&sb, ".end field\n");
                smali_sa(&sb, "\n");
            }
        }
    }

    /* instance fields */
    if (c->instance_count > 0) {
        smali_sa(&sb, c->static_count > 0 ? "\n# instance fields\n" : "\n\n# instance fields\n");
        for (int fi = 0; fi < c->instance_count; fi++) {
            dex_field_enc *fe = &c->instance_fields[fi];
            if (fe->field_idx < ctx->field_count) {
                dex_field *f = &ctx->fields[fe->field_idx];
                char *ft = smali_tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                const char *af = smali_aflags(fe->access_flags);
                if (af[0])
                    smali_sf(&sb, ".field %s %s:%s\n",
                        af, ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                else
                    smali_sf(&sb, ".field %s:%s\n",
                        ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                free(ft);
                size_t before_annots = len;
                smali_write_annotations(&sb, ctx, c, 1, fe->field_idx);
                if (len != before_annots) smali_sa(&sb, ".end field\n");
                smali_sa(&sb, "\n");
            }
        }
    }

    int had_fields = (c->static_count + c->instance_count) > 0;

    /* direct methods */
    if (c->direct_count > 0) {
        smali_sa(&sb, had_fields ? "\n# direct methods\n" : "\n\n# direct methods\n");
        for (int mi = 0; mi < c->direct_count; mi++) {
            smali_dm(&sb, ctx, &c->direct[mi]);
            if (mi < c->direct_count - 1) smali_sa(&sb, "\n");
        }
    }

    /* virtual methods */
    if (c->virtual_count > 0) {
        int prev = had_fields || c->direct_count > 0;
        smali_sa(&sb, prev ? "\n\n# virtual methods\n" : "\n\n# virtual methods\n");
        for (int mi = 0; mi < c->virtual_count; mi++) {
            smali_dm(&sb, ctx, &c->virtual[mi]);
            if (mi < c->virtual_count - 1) smali_sa(&sb, "\n");
        }
    }

    free(c2); free(s2);
    return buf;
}
