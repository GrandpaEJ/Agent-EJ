#include "smali_writer.h"
#include "smali_anndir.h"
#include "smali_blobset.h"
#include "smali_codesec.h"
#include "smali_evcmp.h"
#include "smali_ids.h"
#include "smali_pool.h"
#include "smali_staticval.h"
#include "smali_typelist.h"
#include "smali_validate.h"
#include "smali_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* The data section is laid out exactly as smali.jar's DexWriter does:
 *   string_data, type_list, encoded_array, annotation, annotation_set,
 *   annotation_set_ref_list, annotations_directory, debug_info, code,
 *   class_data, map_list
 * with every interned section sorted and shared as dexlib2 does, so the same
 * sources assemble to a byte-identical DEX. */

static smali_ctx_def_t *s_sort_ctx;
static const char *s_sort_desc;

static uint32_t member_key_idx(const char *name, const char *type, int field) {
    char key[4096];
    snprintf(key, sizeof(key), field ? "%s->%s:%s" : "%s->%s%s", s_sort_desc, name, type);
    return smali_pool_find(field ? &s_sort_ctx->fields : &s_sort_ctx->methods, key);
}

static int cmp_fields(const void *a, const void *b) {
    const smali_field_def_t *x = a, *y = b;
    return member_key_idx(x->name, x->type, 1) < member_key_idx(y->name, y->type, 1) ? -1 : 1;
}

static int cmp_methods(const void *a, const void *b) {
    const smali_method_def_t *x = a, *y = b;
    return member_key_idx(x->name, x->signature, 0) < member_key_idx(y->name, y->signature, 0) ? -1 : 1;
}

/* class_data lists every member in field_id/method_id order. */
static void sort_members(smali_ctx_def_t *ctx) {
    s_sort_ctx = ctx;
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        s_sort_desc = c->descriptor;
        if (c->static_field_count > 1)
            qsort(c->static_fields, c->static_field_count, sizeof(*c->static_fields), cmp_fields);
        if (c->instance_field_count > 1)
            qsort(c->instance_fields, c->instance_field_count, sizeof(*c->instance_fields), cmp_fields);
        if (c->direct_method_count > 1)
            qsort(c->direct_methods, c->direct_method_count, sizeof(*c->direct_methods), cmp_methods);
        if (c->virtual_method_count > 1)
            qsort(c->virtual_methods, c->virtual_method_count, sizeof(*c->virtual_methods), cmp_methods);
    }
    s_sort_ctx = NULL;
    s_sort_desc = NULL;
}

/* Class indices by type: the order smali.jar walks classes for every
 * per-class data section except class_defs/class_data. */
static uint32_t *type_order(smali_ctx_def_t *ctx) {
    uint32_t n = ctx->class_count;
    uint32_t *o = malloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t *key = malloc((n ? n : 1) * sizeof(uint32_t));
    if (!o || !key) { free(o); free(key); return NULL; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t t = smali_pool_find(&ctx->types, ctx->classes[i].descriptor);
        key[i] = t;
        uint32_t j = i;
        for (; j > 0 && key[o[j - 1]] > t; j--) o[j] = o[j - 1];
        o[j] = i;
    }
    free(key);
    return o;
}

static void collect_typelists(smali_ctx_def_t *ctx, smali_typelist_reg_t *tl,
                              uint32_t *proto_ids, uint32_t *iface_ids) {
    for (uint32_t i = 0; i < ctx->protos.count; i++) {
        uint16_t *types = NULL;
        uint32_t n = smali_typelist_from_proto(&ctx->types, ctx->protos.strings[i], &types);
        proto_ids[i] = smali_typelist_add(tl, types, n);
        free(types);
    }
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        uint16_t *types = NULL;
        uint32_t n = smali_typelist_from_descriptors(&ctx->types, c->interfaces, c->interface_count, &types);
        iface_ids[i] = smali_typelist_add(tl, types, n);
        free(types);
    }
}

static int cmp_arrays(const smali_blobset_t *s, uint32_t a, uint32_t b, void *arg) {
    (void)arg;
    return smali_ev_cmp_array_item(smali_blob_data(s, a), smali_blob_data(s, b));
}

/* static_values: interned per content, written sorted (no alignment). */
static void write_static_values(smali_ctx_def_t *ctx, smali_buf_t *b, uint32_t *off,
                                uint32_t *first, uint32_t *count) {
    smali_blobset_t arrays;
    smali_blobset_init(&arrays);
    smali_buf_t one;
    buf_init(&one);
    uint32_t *ids = malloc((ctx->class_count ? ctx->class_count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; ids && i < ctx->class_count; i++) {
        one.len = 0;
        ids[i] = smali_static_values(ctx, &ctx->classes[i], &one)
               ? smali_blobset_add(&arrays, one.buf, one.len) : UINT32_MAX;
    }
    uint32_t *order = ids ? smali_blobset_sorted(&arrays, cmp_arrays, NULL) : NULL;
    *first = b->len;
    *count = order ? arrays.count : 0;
    for (uint32_t k = 0; order && k < arrays.count; k++) {
        smali_blob_t *it = &arrays.items[order[k]];
        it->off = b->len;
        buf_write(b, arrays.store.buf + it->start, it->len);
    }
    for (uint32_t i = 0; ids && i < ctx->class_count; i++)
        off[i] = ids[i] == UINT32_MAX ? 0 : arrays.items[ids[i]].off;
    free(order); free(ids);
    buf_free(&one);
    smali_blobset_free(&arrays);
}

typedef struct { uint16_t type; uint32_t size, offset; } map_entry_t;

static void map_add(map_entry_t *e, uint32_t *n, uint16_t type, uint32_t size, uint32_t off) {
    if (size) e[(*n)++] = (map_entry_t){ type, size, off };
}

static int write_file(smali_buf_t *b, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t w = fwrite(b->buf, 1, b->len, fp);
    return (fclose(fp) == 0 && w == b->len) ? 0 : -1;
}

int write_assembled_dex(smali_ctx_def_t *ctx, const char *out_dex) {
    /* A pool regression would otherwise surface only as
     * INSTALL_FAILED_INVALID_APK on the device (issue #38). */
    if (smali_validate_pool(ctx) != 0) return -1;
    sort_members(ctx);

    uint32_t nc = ctx->class_count;
    int rc = -1;
    smali_id_layout_t l;
    smali_id_layout(ctx, &l);
    smali_buf_t b; buf_init(&b);
    while (b.cap < l.data) b.cap *= 2;
    b.buf = realloc(b.buf, b.cap);
    memset(b.buf, 0, l.data);
    b.len = l.data;

    smali_typelist_reg_t tl; smali_typelist_reg_init(&tl);
    smali_anndir_t ad; memset(&ad, 0, sizeof(ad));
    smali_codesec_t cs; memset(&cs, 0, sizeof(cs));
    uint32_t *proto_tl = malloc((ctx->protos.count + 1) * sizeof(uint32_t));
    uint32_t *iface_tl = malloc((nc + 1) * sizeof(uint32_t));
    uint32_t *static_off = calloc(nc + 1, sizeof(uint32_t));
    uint32_t *order = type_order(ctx);
    if (!proto_tl || !iface_tl || !static_off || !order) goto out;

    uint32_t str_first = smali_write_string_data(ctx, &b, &l);

    collect_typelists(ctx, &tl, proto_tl, iface_tl);
    smali_typelist_flush(&tl, &b);
    for (uint32_t i = 0; i < ctx->protos.count; i++) proto_tl[i] = smali_typelist_offset(&tl, proto_tl[i]);

    uint32_t arr_first, arr_count;
    write_static_values(ctx, &b, static_off, &arr_first, &arr_count);

    if (smali_anndir_collect(&ad, ctx) != 0) goto out;
    smali_annsec_write_items(&ad.sec, &b);
    smali_annsec_write_sets(&ad.sec, &b);
    smali_anndir_write(&ad, ctx, &b, order);

    if (smali_codesec_write(&cs, ctx, &b, order) != 0) goto out;
    if (smali_classdata_write(&cs, ctx, &b) != 0) goto out;

    smali_write_ids(ctx, &b, &l, proto_tl);
    for (uint32_t i = 0; i < nc; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        uint32_t def[8] = {
            smali_pool_find(&ctx->types, c->descriptor), c->access_flags,
            c->super_class ? smali_pool_find(&ctx->types, c->super_class) : 0xFFFFFFFF,
            smali_typelist_offset(&tl, iface_tl[i]),
            c->source_file ? smali_pool_find(&ctx->strings, c->source_file) : 0xFFFFFFFF,
            ad.dir_off[i], cs.class_data_off[i], static_off[i] };
        memcpy(b.buf + l.class_defs + i * 32, def, sizeof(def));
    }

    align_4(&b);
    uint32_t map_off = b.len, n = 0;
    map_entry_t e[20];
    map_add(e, &n, 0x0000, 1, 0);
    map_add(e, &n, 0x0001, ctx->strings.count, l.string_ids);
    map_add(e, &n, 0x0002, ctx->types.count, l.type_ids);
    map_add(e, &n, 0x0003, ctx->protos.count, l.proto_ids);
    map_add(e, &n, 0x0004, ctx->fields.count, l.field_ids);
    map_add(e, &n, 0x0005, ctx->methods.count, l.method_ids);
    map_add(e, &n, 0x0006, nc, l.class_defs);
    map_add(e, &n, 0x2002, ctx->strings.count, str_first);
    map_add(e, &n, 0x1001, smali_typelist_count(&tl), tl.first_offset);
    map_add(e, &n, 0x2005, arr_count, arr_first);
    map_add(e, &n, 0x2004, ad.sec.items.count, ad.sec.items_off);
    map_add(e, &n, 0x1003, smali_annsec_set_count(&ad.sec), ad.sec.sets_off);
    map_add(e, &n, 0x1002, ad.ref_count, ad.ref_first);
    map_add(e, &n, 0x2006, ad.dir_count, ad.dir_first);
    map_add(e, &n, 0x2003, cs.debug_count, cs.debug_first);
    map_add(e, &n, 0x2001, cs.code_count, cs.code_first);
    map_add(e, &n, 0x2000, cs.class_data_count, cs.class_data_first);
    map_add(e, &n, 0x1000, 1, map_off);
    buf_write_u32(&b, n);
    for (uint32_t i = 0; i < n; i++) {
        buf_write_u16(&b, e[i].type);
        buf_write_u16(&b, 0);
        buf_write_u32(&b, e[i].size);
        buf_write_u32(&b, e[i].offset);
    }

    /* G7: an empty section has both its size and offset zero. */
    uint32_t hdr[17] = {
        0, 0, map_off,
        ctx->strings.count, ctx->strings.count ? l.string_ids : 0,
        ctx->types.count, ctx->types.count ? l.type_ids : 0,
        ctx->protos.count, ctx->protos.count ? l.proto_ids : 0,
        ctx->fields.count, ctx->fields.count ? l.field_ids : 0,
        ctx->methods.count, ctx->methods.count ? l.method_ids : 0,
        nc, nc ? l.class_defs : 0,
        b.len - l.data, l.data };
    memcpy(b.buf, "dex\n035\0", 8);
    uint32_t size_hdr[3] = { b.len, 0x70, 0x12345678 };
    memcpy(b.buf + 32, size_hdr, sizeof(size_hdr));
    memcpy(b.buf + 44, hdr, sizeof(hdr));
    smali_sha1(b.buf + 32, b.len - 32, b.buf + 12);
    uint32_t checksum = adler32(b.buf + 12, b.len - 12);
    memcpy(b.buf + 8, &checksum, 4);
    rc = write_file(&b, out_dex);

out:
    smali_codesec_free(&cs, nc);
    smali_anndir_free(&ad);
    smali_typelist_reg_free(&tl);
    free(proto_tl); free(iface_tl); free(static_off); free(order);
    buf_free(&b);
    return rc;
}
