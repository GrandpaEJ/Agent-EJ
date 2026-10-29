/* Folds a split APK's resources.arsc into a base table (native anti-split).
 *
 * bundletool splits all share the base package's resource IDs: every split
 * carries only the configurations (densities, locales, ABIs) it provides, as
 * extra ResTable_type chunks over the same type/entry layout. Merging is thus
 * a union of variants (configurations) per type, plus the string remapping
 * that follows from each table owning its own string pools:
 *   - a non-complex STRING entry's `data` indexes the GLOBAL value pool,
 *   - a complex entry's map items may likewise carry a STRING value,
 *   - every entry's `key` indexes the package KEY pool.
 * Indices are remapped lazily (assigned on first reference) so no duplicate
 * strings are appended for values the split never actually points at. */
#include "format_arsc_model.h"
#include <stdlib.h>
#include <string.h>

/* A per-split lazy index map: -1 until a source index is first referenced,
 * then the index it was assigned in the destination pool. */
typedef struct { int32_t *to; uint32_t n; } idx_map;

static int map_init(idx_map *m, uint32_t n) {
    m->n = n;
    m->to = n ? malloc(n * sizeof(int32_t)) : NULL;
    if (n && !m->to) return -1;
    for (uint32_t i = 0; i < n; i++) m->to[i] = -1;
    return 0;
}
static void map_free(idx_map *m) { free(m->to); m->to = NULL; m->n = 0; }

/* Resolves a split global-value index to its index in the base value pool,
 * appending the string the first time it is seen. */
static uint32_t remap_value(arsc_model *base, const arsc_model *split,
                            idx_map *vm, uint32_t v) {
    if (v >= vm->n) return 0;
    if (vm->to[v] < 0)
        vm->to[v] = (int32_t)arsc_model_add_value(base, arsc_model_value(split, v));
    return (uint32_t)vm->to[v];
}

/* Resolves a split key index to the base package key pool. Keys line up by
 * index across splits (shared layout), so the same-index name is tried first
 * before falling back to appending. */
static uint32_t remap_key(arsc_model_pkg *bp, const arsc_model_pkg *sp,
                          idx_map *km, uint32_t k) {
    if (k >= km->n) return 0;
    if (km->to[k] < 0) {
        const char *ks = arsc_pkg_key(sp, k);
        const char *bk = arsc_pkg_key(bp, k);
        if (ks && bk && strcmp(ks, bk) == 0) km->to[k] = (int32_t)k;
        else km->to[k] = (int32_t)arsc_pkg_add_key(bp, ks ? ks : "");
    }
    return (uint32_t)km->to[k];
}

/* True when two variants describe the same configuration. */
static int cfg_equal(const arsc_model_variant *a, const arsc_model_variant *b) {
    uint8_t n = a->config_size > b->config_size ? a->config_size : b->config_size;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t ca = i < a->config_size ? a->config[i] : 0;
        uint8_t cb = i < b->config_size ? b->config[i] : 0;
        if (ca != cb) return 0;
    }
    return 1;
}

/* Deep-copies one present source entry into `dst`, remapping its strings. */
static int copy_entry(arsc_model *base, const arsc_model *split,
                      arsc_model_pkg *bp, const arsc_model_pkg *sp,
                      idx_map *vm, idx_map *km,
                      arsc_model_entry *dst, const arsc_model_entry *src) {
    dst->present = 1;
    dst->flags = src->flags;
    dst->parent = src->parent;
    dst->key = remap_key(bp, sp, km, src->key);
    if (src->flags & ARSC_ENTRY_COMPLEX) {
        dst->map_count = src->map_count;
        free(dst->map);
        dst->map = NULL;
        if (src->map_count) {
            dst->map = calloc(src->map_count, sizeof(arsc_map_item));
            if (!dst->map) { dst->present = 0; dst->map_count = 0; return -1; }
            for (uint32_t j = 0; j < src->map_count; j++) {
                dst->map[j] = src->map[j];
                if (src->map[j].data_type == ARSC_TYPE_STRING)
                    dst->map[j].data = remap_value(base, split, vm, src->map[j].data);
            }
        }
    } else {
        dst->data_type = src->data_type;
        dst->data = (src->data_type == ARSC_TYPE_STRING)
                        ? remap_value(base, split, vm, src->data)
                        : src->data;
    }
    return 0;
}

/* Grows a base type (and all its existing variants) to `ec` entries. */
static int grow_type(arsc_model_type *t, uint32_t ec) {
    if (ec <= t->entry_count) return 0;
    uint32_t *nf = realloc(t->spec_flags, ec * sizeof(uint32_t));
    if (!nf) return -1;
    memset(nf + t->entry_count, 0, (ec - t->entry_count) * sizeof(uint32_t));
    t->spec_flags = nf;
    for (uint32_t v = 0; v < t->variant_count; v++) {
        arsc_model_variant *va = &t->variants[v];
        arsc_model_entry *ne = realloc(va->entries, ec * sizeof(arsc_model_entry));
        if (!ne) return -1;
        memset(ne + va->entry_count, 0, (ec - va->entry_count) * sizeof(arsc_model_entry));
        va->entries = ne;
        va->entry_count = ec;
    }
    t->entry_count = ec;
    return 0;
}

/* Merges one split type into the matching (or newly created) base type. */
static int merge_type(arsc_model *base, const arsc_model *split,
                      arsc_model_pkg *bp, const arsc_model_pkg *sp,
                      const arsc_model_type *st, idx_map *vm, idx_map *km,
                      int *added) {
    arsc_model_type *bt = arsc_pkg_find_type(bp, st->id);
    if (!bt) {
        bt = arsc_pkg_add_type(bp, st->id, st->entry_count);
        if (!bt) return -1;
        bt->res0 = st->res0;
        bt->res1 = st->res1;
        /* Carry the type name if the base package never declared this type. */
        const char *tn = arsc_pkg_type_string(sp, st->id - 1);
        while (bp->type_string_count < (uint32_t)st->id)
            arsc_pkg_add_type_string(bp, bp->type_string_count == (uint32_t)(st->id - 1) && tn ? tn : "");
    }
    if (grow_type(bt, st->entry_count) != 0) return -1;
    for (uint32_t i = 0; i < st->entry_count && i < bt->entry_count; i++)
        bt->spec_flags[i] |= st->spec_flags[i];

    for (uint32_t v = 0; v < st->variant_count; v++) {
        const arsc_model_variant *sv = &st->variants[v];
        arsc_model_variant *bv = NULL;
        for (uint32_t b = 0; b < bt->variant_count; b++)
            if (cfg_equal(&bt->variants[b], sv)) { bv = &bt->variants[b]; break; }
        if (!bv) {
            bv = arsc_type_add_variant(bt, bt->entry_count, sv->config,
                                       sv->config_size, sv->flags);
            if (!bv) return -1;
            bv->reserved = sv->reserved;
            (*added)++;
        }
        for (uint32_t i = 0; i < sv->entry_count && i < bv->entry_count; i++) {
            if (!sv->entries[i].present || bv->entries[i].present) continue;
            if (copy_entry(base, split, bp, sp, vm, km,
                           &bv->entries[i], &sv->entries[i]) != 0)
                return -1;
        }
    }
    return 0;
}

/* Folds every package/type/variant of `split` into `base`. Returns the number
 * of configurations added, or -1 on allocation failure. */
int arsc_model_merge(arsc_model *base, const arsc_model *split) {
    if (!base || !split) return -1;
    int added = 0;
    for (uint32_t p = 0; p < split->pkg_count; p++) {
        const arsc_model_pkg *sp = &split->pkgs[p];
        arsc_model_pkg *bp = NULL;
        for (uint32_t b = 0; b < base->pkg_count; b++)
            if (base->pkgs[b].id == sp->id) { bp = &base->pkgs[b]; break; }
        if (!bp) bp = arsc_model_add_pkg(base, sp->id, sp->name);
        if (!bp) return -1;

        idx_map vm, km;
        if (map_init(&vm, split->value_count) != 0) return -1;
        if (map_init(&km, sp->key_string_count) != 0) { map_free(&vm); return -1; }
        int rc = 0;
        for (uint32_t t = 0; t < sp->type_count && rc == 0; t++)
            rc = merge_type(base, split, bp, sp, &sp->types[t], &vm, &km, &added);
        map_free(&vm);
        map_free(&km);
        if (rc != 0) return -1;
    }
    return added;
}
