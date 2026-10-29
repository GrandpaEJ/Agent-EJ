#include "format_dex_internal.h"
#include <string.h>

static void write_encoded_value(smali_sb *s, dex_ctx *ctx,
                                 const uint8_t **pp, const uint8_t *end,
                                 int indent);

/* Parse and write one encoded_annotation */
static int write_encoded_annotation(smali_sb *s, dex_ctx *ctx,
                                     const uint8_t *data, const uint8_t *end,
                                     int visibility, const char *prefix) {
    const uint8_t *p = data;
    uint32_t type_idx; smali_uleb(p, &type_idx, &p);
    uint32_t elem_count; smali_uleb(p, &elem_count, &p);
    const char *vis_str = visibility == 1 ? "runtime"
                        : visibility == 2 ? "system" : "build";

    smali_sf(s, "%s.annotation %s %s\n", prefix, vis_str,
             smali_res(ctx, 2, type_idx));

    for (uint32_t ei = 0; ei < elem_count; ei++) {
        uint32_t name_idx; smali_uleb(p, &name_idx, &p);
        if (p >= end) break;
        int elem_indent = (int)strlen(prefix) + 4;
        smali_sf(s, "%s    %s = ", prefix, smali_res(ctx, 5, name_idx));
        write_encoded_value(s, ctx, &p, end, elem_indent);
        smali_sa(s, "\n");
    }
    smali_sf(s, "%s.end annotation\n", prefix);
    return 0;
}

/* Parse and output one encoded_value. Uses base_indent for multi-line values. */
static void write_encoded_value(smali_sb *s, dex_ctx *ctx,
                                 const uint8_t **pp, const uint8_t *end,
                                 int base_indent) {
    const uint8_t *p = *pp;
    if (p >= end) return;
    int vtype = *p & 0x1F;

    if (vtype == 0x1c) {                         /* ARRAY: uleb size, then values */
        uint32_t n; p++;
        p += dex_read_uleb128(p, &n);
        if (n == 0) { smali_sa(s, "{}"); *pp = p; return; }
        smali_sa(s, "{");
        for (uint32_t ai = 0; ai < n && p < end; ai++) {
            if (ai) smali_sa(s, ",");
            smali_sf(s, "\n%*s", base_indent + 4, "");
            write_encoded_value(s, ctx, &p, end, base_indent + 4);
        }
        smali_sf(s, "\n%*s}", base_indent, "");
        *pp = p;
        return;
    }
    if (vtype == 0x1d) {                         /* nested ANNOTATION: uleb type, uleb count, pairs */
        uint32_t ty, n; p++;
        p += dex_read_uleb128(p, &ty);
        p += dex_read_uleb128(p, &n);
        smali_sf(s, ".subannotation %s\n", smali_res(ctx, 2, ty));
        for (uint32_t si = 0; si < n && p < end; si++) {
            uint32_t name;
            p += dex_read_uleb128(p, &name);
            smali_sf(s, "%*s%s = ", base_indent + 4, "", smali_res(ctx, 5, name));
            write_encoded_value(s, ctx, &p, end, base_indent + 4);
            smali_sa(s, "\n");
        }
        smali_sf(s, "%*s.end subannotation", base_indent, "");
        *pp = p;
        return;
    }
    static char buf[65536];
    if (dex_format_value(&p, end, buf, sizeof(buf), ctx) < 0) { *pp = end; return; }
    smali_sa(s, buf);
    *pp = p;
}

/* Parse an annotation_set_item and write all its annotations */
static int write_annotation_set_ref(smali_sb *s, dex_ctx *ctx,
                                     uint32_t offset, const char *prefix) {
    if (offset == 0 || offset + 4 > ctx->size) return 0;
    uint32_t size; memcpy(&size, ctx->data + offset, 4);
    if (offset + 4 + size * 4 > ctx->size) return 0;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t ann_item_off;
        memcpy(&ann_item_off, ctx->data + offset + 4 + i * 4, 4);
        if (ann_item_off == 0 || ann_item_off + 1 >= ctx->size) continue;
        if (i > 0) smali_sa(s, "\n");
        int visibility = (int)ctx->data[ann_item_off];
        write_encoded_annotation(s, ctx, ctx->data + ann_item_off + 1,
                                  ctx->data + ctx->size, visibility, prefix);
    }
    return 0;
}

/* Parse annotations_directory_item and write annotations matching target */
void smali_write_annotations(smali_sb *s, dex_ctx *ctx, dex_class *c,
                              int target_type, uint32_t target_idx) {
    if (c->annotations_off == 0 || c->annotations_off + 16 > ctx->size) return;
    const uint8_t *p = ctx->data + c->annotations_off;
    uint32_t class_annot_off, fields_sz, methods_sz, params_sz;
    memcpy(&class_annot_off, p, 4); p += 4;
    memcpy(&fields_sz, p, 4); p += 4;
    memcpy(&methods_sz, p, 4); p += 4;
    memcpy(&params_sz, p, 4); p += 4;

    if (target_type == 0 && class_annot_off != 0) {
        smali_sa(s, "\n\n# annotations\n");
        write_annotation_set_ref(s, ctx, class_annot_off, "");
    }

    /* field annotations */
    for (uint32_t i = 0; i < fields_sz; i++) {
        if ((const uint8_t *)(p + 8) > ctx->data + ctx->size) break;
        uint32_t fidx, fannot_off;
        memcpy(&fidx, p, 4); p += 4;
        memcpy(&fannot_off, p, 4); p += 4;
        if (target_type == 1 && fidx == target_idx && fannot_off != 0)
            write_annotation_set_ref(s, ctx, fannot_off, "    ");
    }

    /* method annotations */
    for (uint32_t i = 0; i < methods_sz; i++) {
        if ((const uint8_t *)(p + 8) > ctx->data + ctx->size) break;
        uint32_t midx, mannot_off;
        memcpy(&midx, p, 4); p += 4;
        memcpy(&mannot_off, p, 4); p += 4;
        if (target_type == 2 && midx == target_idx && mannot_off != 0)
            write_annotation_set_ref(s, ctx, mannot_off, "    ");
    }
    (void)params_sz;
}

/* Convenience: write method-level annotations for a given method_idx */
int smali_write_method_annot(smali_sb *s, dex_ctx *ctx, dex_class *c,
                              uint32_t method_idx) {
    smali_write_annotations(s, ctx, c, 2, method_idx);
    return 0;
}
