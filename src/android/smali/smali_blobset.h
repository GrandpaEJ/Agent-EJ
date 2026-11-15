#ifndef SMALI_BLOBSET_H
#define SMALI_BLOBSET_H

#include "smali_buf.h"
#include <stdint.h>

/* Interning set of byte strings.
 *
 * smali.jar shares every data item whose encoding is identical (annotations,
 * annotation sets, static value arrays, debug info). Each item is encoded once
 * into a scratch buffer and added here; an identical item yields the id of
 * the first copy. All bytes live in one growing store, so the set costs one
 * allocation per section instead of one per item. */
typedef struct {
    uint32_t start;   /* offset of the bytes in the store */
    uint32_t len;     /* number of bytes */
    uint32_t off;     /* file offset once written (0 = not written) */
} smali_blob_t;

typedef struct {
    smali_buf_t store;
    smali_blob_t *items;
    uint32_t count, cap;
    uint32_t *ht;      /* open addressing: item id + 1, 0 = free slot */
    uint32_t ht_cap;   /* power of two */
} smali_blobset_t;

void smali_blobset_init(smali_blobset_t *s);
void smali_blobset_free(smali_blobset_t *s);

/* Returns the id of the blob equal to data[0..len), adding it if new. */
uint32_t smali_blobset_add(smali_blobset_t *s, const uint8_t *data, uint32_t len);

/* Returns the id of an equal blob, or UINT32_MAX when there is none. */
uint32_t smali_blobset_find(const smali_blobset_t *s, const uint8_t *data, uint32_t len);

static inline const uint8_t *smali_blob_data(const smali_blobset_t *s, uint32_t id) {
    return s->store.buf + s->items[id].start;
}

/* Returns the ids 0..count-1 sorted with cmp (stable for equal keys). The
 * caller frees the array. */
uint32_t *smali_blobset_sorted(const smali_blobset_t *s,
                               int (*cmp)(const smali_blobset_t *, uint32_t, uint32_t, void *),
                               void *arg);

#endif
