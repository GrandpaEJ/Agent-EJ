#include "smali_annsec.h"
#include "smali_evcmp.h"
#include "smali_value.h"
#include <stdlib.h>
#include <string.h>

void smali_annsec_init(smali_annsec_t *s) {
    memset(s, 0, sizeof(*s));
    smali_blobset_init(&s->items);
    smali_blobset_init(&s->sets);
    buf_init(&s->scratch);
}

void smali_annsec_free(smali_annsec_t *s) {
    smali_blobset_free(&s->items);
    smali_blobset_free(&s->sets);
    buf_free(&s->scratch);
    free(s->rank);
    memset(s, 0, sizeof(*s));
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

uint32_t smali_annsec_add_set(smali_annsec_t *s, smali_ctx_def_t *ctx,
                              smali_annotation_t *a, uint32_t n) {
    if (n > MAX_ANNOTS) n = MAX_ANNOTS;
    uint32_t ids[MAX_ANNOTS];
    for (uint32_t i = 0; i < n; i++) {
        s->scratch.len = 0;
        buf_write_u8(&s->scratch, (uint8_t)a[i].visibility);
        write_encoded_annotation(&s->scratch, ctx, &a[i]);
        ids[i] = smali_blobset_add(&s->items, s->scratch.buf, s->scratch.len);
    }
    qsort(ids, n, sizeof(uint32_t), cmp_u32);
    return smali_blobset_add(&s->sets, (const uint8_t *)ids, n * sizeof(uint32_t));
}

/* BaseAnnotation.compareTo: visibility, type, then the elements. */
static int cmp_items(const smali_blobset_t *s, uint32_t a, uint32_t b, void *arg) {
    (void)arg;
    const uint8_t *x = smali_blob_data(s, a), *y = smali_blob_data(s, b);
    if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1;
    return smali_ev_cmp_annotation(x + 1, y + 1);
}

void smali_annsec_write_items(smali_annsec_t *s, smali_buf_t *b) {
    uint32_t n = s->items.count;
    uint32_t *order = smali_blobset_sorted(&s->items, cmp_items, NULL);
    s->rank = malloc((n ? n : 1) * sizeof(uint32_t));
    if (!order || !s->rank) { free(order); return; }
    s->items_off = b->len;
    for (uint32_t k = 0; k < n; k++) {
        smali_blob_t *it = &s->items.items[order[k]];
        s->rank[order[k]] = k;
        it->off = b->len;
        buf_write(b, s->items.store.buf + it->start, it->len);
    }
    free(order);
}

static uint32_t member(const smali_blobset_t *sets, uint32_t set, uint32_t i) {
    uint32_t v;
    memcpy(&v, smali_blob_data(sets, set) + 4 * i, 4);
    return v;
}

/* compareAsSet: both sets in natural annotation order (= item rank), then
 * element by element, a shorter prefix first. */
static int cmp_sets(const smali_blobset_t *sets, uint32_t a, uint32_t b, void *arg) {
    const uint32_t *rank = arg;
    uint32_t na = sets->items[a].len / 4, nb = sets->items[b].len / 4;
    uint32_t ra[MAX_ANNOTS], rb[MAX_ANNOTS];
    for (uint32_t i = 0; i < na; i++) ra[i] = rank[member(sets, a, i)];
    for (uint32_t i = 0; i < nb; i++) rb[i] = rank[member(sets, b, i)];
    qsort(ra, na, sizeof(uint32_t), cmp_u32);
    qsort(rb, nb, sizeof(uint32_t), cmp_u32);
    for (uint32_t i = 0; i < na && i < nb; i++)
        if (ra[i] != rb[i]) return ra[i] < rb[i] ? -1 : 1;
    return na < nb ? -1 : na > nb;
}

/* type_idx of an annotation_item (after its visibility byte). */
static uint32_t item_type(const smali_blobset_t *items, uint32_t id) {
    const uint8_t *p = smali_blob_data(items, id) + 1;
    return smali_read_uleb(&p);
}

void smali_annsec_write_sets(smali_annsec_t *s, smali_buf_t *b) {
    align_4(b);
    s->sets_off = b->len;
    buf_write_u32(b, 0);                  /* API < 17: the shared empty set */
    if (!s->rank) return;
    uint32_t *order = smali_blobset_sorted(&s->sets, cmp_sets, s->rank);
    if (!order) return;
    for (uint32_t k = 0; k < s->sets.count; k++) {
        uint32_t set = order[k], n = s->sets.items[set].len / 4;
        uint32_t m[MAX_ANNOTS];
        /* members are listed by type (BaseAnnotation.BY_TYPE) */
        for (uint32_t i = 0; i < n; i++) m[i] = member(&s->sets, set, i);
        for (uint32_t i = 1; i < n; i++)
            for (uint32_t j = i; j > 0 && item_type(&s->items, m[j]) < item_type(&s->items, m[j - 1]); j--) {
                uint32_t t = m[j]; m[j] = m[j - 1]; m[j - 1] = t;
            }
        align_4(b);
        s->sets.items[set].off = b->len;
        buf_write_u32(b, n);
        for (uint32_t i = 0; i < n; i++) buf_write_u32(b, s->items.items[m[i]].off);
    }
    free(order);
}
