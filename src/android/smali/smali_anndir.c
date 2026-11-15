#include "smali_anndir.h"
#include "smali_params.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t class_slots(const smali_class_def_t *c) {
    return 1 + c->static_field_count + c->instance_field_count +
           c->direct_method_count + c->virtual_method_count;
}

static smali_method_def_t *method_at(smali_class_def_t *c, uint32_t k) {
    return k < c->direct_method_count ? &c->direct_methods[k]
                                      : &c->virtual_methods[k - c->direct_method_count];
}

static int has_param_annots(const smali_method_def_t *m) {
    for (uint32_t p = 0; m->param_annots && p < m->param_name_count; p++)
        if (m->param_annot_counts[p] > 0) return 1;
    return 0;
}

/* Set of an annotation list; an empty list still interns the empty set. */
static uint32_t intern(smali_anndir_t *d, smali_ctx_def_t *ctx, smali_annotation_t *a,
                       uint32_t n, int *need_empty) {
    if (n == 0) { *need_empty = 1; return SMALI_NO_SET; }
    return smali_annsec_add_set(&d->sec, ctx, a, n);
}

int smali_anndir_collect(smali_anndir_t *d, smali_ctx_def_t *ctx) {
    memset(d, 0, sizeof(*d));
    smali_annsec_init(&d->sec);
    uint32_t n = ctx->class_count;
    d->base = malloc((n ? n : 1) * sizeof(uint32_t));
    d->dir_off = calloc(n ? n : 1, sizeof(uint32_t));
    if (!d->base || !d->dir_off) return -1;
    for (uint32_t i = 0; i < n; i++) {
        d->base[i] = d->nslots;
        d->nslots += class_slots(&ctx->classes[i]);
        smali_class_def_t *c = &ctx->classes[i];
        for (uint32_t k = 0; k < c->direct_method_count + c->virtual_method_count; k++)
            if (has_param_annots(method_at(c, k)))
                d->nparams += smali_sig_param_count(method_at(c, k)->signature);
    }
    uint32_t ns = d->nslots ? d->nslots : 1;
    d->slots = malloc(ns * sizeof(uint32_t));
    d->pstart = malloc(ns * sizeof(uint32_t));
    d->refoff = calloc(ns, sizeof(uint32_t));
    d->params = malloc((d->nparams ? d->nparams : 1) * sizeof(uint32_t));
    if (!d->slots || !d->pstart || !d->refoff || !d->params) return -1;

    int need_empty = 0;
    uint32_t np = 0;
    for (uint32_t i = 0; i < n; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        uint32_t s = d->base[i];
        d->pstart[s] = SMALI_NO_SET;
        d->slots[s++] = intern(d, ctx, c->annots, c->annot_count, &need_empty);
        for (int inst = 0; inst < 2; inst++) {
            uint32_t fc = inst ? c->instance_field_count : c->static_field_count;
            smali_field_def_t *fa = inst ? c->instance_fields : c->static_fields;
            for (uint32_t j = 0; j < fc; j++, s++) {
                d->pstart[s] = SMALI_NO_SET;
                d->slots[s] = intern(d, ctx, fa[j].annots, fa[j].annot_count, &need_empty);
            }
        }
        for (uint32_t k = 0; k < c->direct_method_count + c->virtual_method_count; k++, s++) {
            smali_method_def_t *m = method_at(c, k);
            d->slots[s] = intern(d, ctx, m->annots, m->annot_count, &need_empty);
            d->pstart[s] = SMALI_NO_SET;
            uint32_t pc = smali_sig_param_count(m->signature);
            if (!has_param_annots(m)) { if (pc) need_empty = 1; continue; }
            d->pstart[s] = np;
            for (uint32_t p = 0; p < pc; p++) {
                uint32_t cnt = p < m->param_name_count ? m->param_annot_counts[p] : 0;
                d->params[np++] = intern(d, ctx, cnt ? m->param_annots[p] : NULL, cnt, &need_empty);
            }
        }
    }
    if (need_empty) smali_annsec_add_set(&d->sec, ctx, NULL, 0);
    return 0;
}

typedef struct { uint32_t idx, off; } idx_off_t;

static int cmp_idx(const void *a, const void *b) {
    uint32_t x = ((const idx_off_t *)a)->idx, y = ((const idx_off_t *)b)->idx;
    return x < y ? -1 : x > y;
}

static uint32_t member_idx(smali_ctx_def_t *ctx, const char *cls, const char *name,
                           const char *sig, int field) {
    char key[4096];
    snprintf(key, sizeof(key), field ? "%s->%s:%s" : "%s->%s%s", cls, name, sig);
    uint32_t idx = smali_pool_find(field ? &ctx->fields : &ctx->methods, key);
    return idx == 0xFFFFFFFF ? 0 : idx;
}

static uint32_t set_off(const smali_anndir_t *d, uint32_t set) {
    return set == SMALI_NO_SET ? 0 : smali_annsec_set_off(&d->sec, set);
}

/* annotation_set_ref_list of each method with parameter annotations, in
 * method_id order per class; identical lists are shared. */
static void write_refs(smali_anndir_t *d, smali_ctx_def_t *ctx, smali_buf_t *b,
                       const uint32_t *order, idx_off_t *tmp) {
    smali_blobset_t seen;
    smali_blobset_init(&seen);
    smali_buf_t list;
    buf_init(&list);
    align_4(b);
    d->ref_first = b->len;
    for (uint32_t o = 0; o < ctx->class_count; o++) {
        smali_class_def_t *c = &ctx->classes[order[o]];
        uint32_t ms = d->base[order[o]] + 1 + c->static_field_count + c->instance_field_count;
        uint32_t nm = 0;
        for (uint32_t k = 0; k < c->direct_method_count + c->virtual_method_count; k++) {
            if (d->pstart[ms + k] == SMALI_NO_SET) continue;
            smali_method_def_t *m = method_at(c, k);
            tmp[nm].idx = member_idx(ctx, c->descriptor, m->name, m->signature, 0);
            tmp[nm++].off = ms + k;
        }
        qsort(tmp, nm, sizeof(*tmp), cmp_idx);
        for (uint32_t k = 0; k < nm; k++) {
            uint32_t slot = tmp[k].off;
            smali_method_def_t *m = method_at(c, slot - ms);
            uint32_t pc = smali_sig_param_count(m->signature);
            list.len = 0;
            for (uint32_t p = 0; p < pc; p++) {
                uint32_t set = d->params[d->pstart[slot] + p];
                int empty = set == SMALI_NO_SET || smali_annsec_set_size(&d->sec, set) == 0;
                buf_write_u32(&list, empty ? d->sec.sets_off : set_off(d, set));
            }
            uint32_t id = smali_blobset_find(&seen, list.buf, list.len);
            if (id != UINT32_MAX) { d->refoff[slot] = seen.items[id].off; continue; }
            id = smali_blobset_add(&seen, list.buf, list.len);
            align_4(b);
            d->refoff[slot] = seen.items[id].off = b->len;
            d->ref_count++;
            buf_write_u32(b, pc);
            buf_write(b, list.buf, list.len);
        }
    }
    buf_free(&list);
    smali_blobset_free(&seen);
}

static uint32_t gather(smali_anndir_t *d, smali_ctx_def_t *ctx, smali_class_def_t *c,
                       uint32_t s, int what, idx_off_t *out) {
    uint32_t n = 0;
    uint32_t nf = c->static_field_count + c->instance_field_count;
    uint32_t nmeth = c->direct_method_count + c->virtual_method_count;
    for (uint32_t k = 0; k < (what == 0 ? nf : nmeth); k++) {
        uint32_t slot = s + 1 + (what == 0 ? k : nf + k);
        uint32_t off = what == 2 ? d->refoff[slot] : set_off(d, d->slots[slot]);
        if (!off) continue;
        if (what == 0) {
            smali_field_def_t *f = k < c->static_field_count ? &c->static_fields[k]
                                 : &c->instance_fields[k - c->static_field_count];
            out[n].idx = member_idx(ctx, c->descriptor, f->name, f->type, 1);
        } else {
            smali_method_def_t *m = method_at(c, k);
            out[n].idx = member_idx(ctx, c->descriptor, m->name, m->signature, 0);
        }
        out[n++].off = off;
    }
    qsort(out, n, sizeof(*out), cmp_idx);
    return n;
}

void smali_anndir_write(smali_anndir_t *d, smali_ctx_def_t *ctx, smali_buf_t *b,
                        const uint32_t *order) {
    uint32_t most = 1;
    for (uint32_t i = 0; i < ctx->class_count; i++)
        if (class_slots(&ctx->classes[i]) > most) most = class_slots(&ctx->classes[i]);
    idx_off_t *lists[3];
    for (int w = 0; w < 3; w++) lists[w] = malloc(most * sizeof(idx_off_t));
    uint32_t *class_only = calloc(d->sec.sets.count ? d->sec.sets.count : 1, sizeof(uint32_t));
    if (!lists[0] || !lists[1] || !lists[2] || !class_only) goto out;

    write_refs(d, ctx, b, order, lists[0]);
    align_4(b);
    d->dir_first = b->len;
    for (uint32_t o = 0; o < ctx->class_count; o++) {
        uint32_t ci = order[o], s = d->base[ci];
        smali_class_def_t *c = &ctx->classes[ci];
        uint32_t cnt[3];
        for (int w = 0; w < 3; w++) cnt[w] = gather(d, ctx, c, s, w, lists[w]);
        uint32_t cls = d->slots[s];
        if (!cnt[0] && !cnt[1] && !cnt[2]) {
            if (cls == SMALI_NO_SET) continue;
            /* directories holding only class annotations are shared */
            if (class_only[cls]) { d->dir_off[ci] = class_only[cls]; continue; }
            class_only[cls] = b->len;
        }
        d->dir_off[ci] = b->len;
        d->dir_count++;
        buf_write_u32(b, set_off(d, cls));
        for (int w = 0; w < 3; w++) buf_write_u32(b, cnt[w]);
        for (int w = 0; w < 3; w++)
            for (uint32_t k = 0; k < cnt[w]; k++) {
                buf_write_u32(b, lists[w][k].idx);
                buf_write_u32(b, lists[w][k].off);
            }
    }
out:
    for (int w = 0; w < 3; w++) free(lists[w]);
    free(class_only);
}

void smali_anndir_free(smali_anndir_t *d) {
    smali_annsec_free(&d->sec);
    free(d->base); free(d->slots); free(d->pstart); free(d->refoff);
    free(d->params); free(d->dir_off);
    memset(d, 0, sizeof(*d));
}
