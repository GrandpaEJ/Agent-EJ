#ifndef SMALI_ANNSEC_H
#define SMALI_ANNSEC_H

#include "smali_types.h"
#include "smali_blobset.h"

#define SMALI_NO_SET UINT32_MAX

/* annotation_item and annotation_set_item sections, laid out as smali.jar
 * does: every distinct annotation and every distinct set is written once,
 * each section sorted with dexlib2's natural order, and (for the default API
 * level 15 < 17) the set section opens with a shared empty set that empty
 * parameter slots point at. */
typedef struct {
    smali_blobset_t items;   /* visibility byte + encoded_annotation */
    smali_blobset_t sets;    /* member item ids (u32), ascending */
    uint32_t *rank;          /* item id -> position in the item section */
    uint32_t items_off;      /* first annotation_item */
    uint32_t sets_off;       /* first annotation_set_item (the empty one) */
    smali_buf_t scratch;
} smali_annsec_t;

void smali_annsec_init(smali_annsec_t *s);
void smali_annsec_free(smali_annsec_t *s);

/* Interns the set made of a[0..n); n == 0 interns the empty set (smali.jar
 * interns it for every unannotated class, member and parameter). */
uint32_t smali_annsec_add_set(smali_annsec_t *s, smali_ctx_def_t *ctx,
                              smali_annotation_t *a, uint32_t n);

void smali_annsec_write_items(smali_annsec_t *s, smali_buf_t *b);
void smali_annsec_write_sets(smali_annsec_t *s, smali_buf_t *b);

static inline uint32_t smali_annsec_set_off(const smali_annsec_t *s, uint32_t set) {
    return s->sets.items[set].off;
}
static inline uint32_t smali_annsec_set_size(const smali_annsec_t *s, uint32_t set) {
    return s->sets.items[set].len / 4;
}
/* Number of annotation_set_items written, the leading empty set included. */
static inline uint32_t smali_annsec_set_count(const smali_annsec_t *s) {
    return s->sets.count + 1;
}

#endif
