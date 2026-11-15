#include "smali_blobset.h"
#include <stdlib.h>
#include <string.h>

void smali_blobset_init(smali_blobset_t *s) {
    memset(s, 0, sizeof(*s));
    buf_init(&s->store);
}

void smali_blobset_free(smali_blobset_t *s) {
    buf_free(&s->store);
    free(s->items);
    free(s->ht);
    memset(s, 0, sizeof(*s));
}

static uint32_t hash_bytes(const uint8_t *p, uint32_t len) {
    uint32_t h = 2166136261u;                       /* FNV-1a */
    for (uint32_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static int blob_eq(const smali_blobset_t *s, uint32_t id, const uint8_t *data, uint32_t len) {
    const smali_blob_t *it = &s->items[id];
    return it->len == len && memcmp(s->store.buf + it->start, data, len) == 0;
}

static void ht_insert(smali_blobset_t *s, uint32_t id) {
    const smali_blob_t *it = &s->items[id];
    uint32_t mask = s->ht_cap - 1;
    uint32_t h = hash_bytes(s->store.buf + it->start, it->len) & mask;
    while (s->ht[h]) h = (h + 1) & mask;
    s->ht[h] = id + 1;
}

static int ht_grow(smali_blobset_t *s) {
    uint32_t ncap = s->ht_cap ? s->ht_cap * 2 : 64;
    uint32_t *nht = calloc(ncap, sizeof(uint32_t));
    if (!nht) return -1;
    free(s->ht);
    s->ht = nht;
    s->ht_cap = ncap;
    for (uint32_t i = 0; i < s->count; i++) ht_insert(s, i);
    return 0;
}

uint32_t smali_blobset_find(const smali_blobset_t *s, const uint8_t *data, uint32_t len) {
    if (!s->ht_cap) return UINT32_MAX;
    uint32_t mask = s->ht_cap - 1;
    for (uint32_t h = hash_bytes(data, len) & mask; s->ht[h]; h = (h + 1) & mask)
        if (blob_eq(s, s->ht[h] - 1, data, len)) return s->ht[h] - 1;
    return UINT32_MAX;
}

uint32_t smali_blobset_add(smali_blobset_t *s, const uint8_t *data, uint32_t len) {
    uint32_t id = smali_blobset_find(s, data, len);
    if (id != UINT32_MAX) return id;
    if ((s->count + 1) * 2 > s->ht_cap && ht_grow(s) != 0) return UINT32_MAX;
    if (s->count == s->cap) {
        uint32_t ncap = s->cap ? s->cap * 2 : 32;
        smali_blob_t *n = realloc(s->items, ncap * sizeof(*n));
        if (!n) return UINT32_MAX;
        s->items = n;
        s->cap = ncap;
    }
    id = s->count++;
    s->items[id].start = s->store.len;
    s->items[id].len = len;
    s->items[id].off = 0;
    if (len) buf_write(&s->store, data, len);
    ht_insert(s, id);
    return id;
}

/* qsort has no context argument in C11; a merge sort keeps the order stable
 * and needs no global state. */
typedef struct {
    const smali_blobset_t *s;
    int (*cmp)(const smali_blobset_t *, uint32_t, uint32_t, void *);
    void *arg;
} sort_ctx_t;

static void merge_sort(uint32_t *a, uint32_t *tmp, uint32_t n, const sort_ctx_t *c) {
    if (n < 2) return;
    uint32_t h = n / 2;
    merge_sort(a, tmp, h, c);
    merge_sort(a + h, tmp, n - h, c);
    uint32_t i = 0, j = h, k = 0;
    while (i < h && j < n)
        tmp[k++] = c->cmp(c->s, a[j], a[i], c->arg) < 0 ? a[j++] : a[i++];
    while (i < h) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, n * sizeof(uint32_t));
}

uint32_t *smali_blobset_sorted(const smali_blobset_t *s,
                               int (*cmp)(const smali_blobset_t *, uint32_t, uint32_t, void *),
                               void *arg) {
    uint32_t n = s->count;
    uint32_t *ids = malloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t *tmp = malloc((n ? n : 1) * sizeof(uint32_t));
    if (!ids || !tmp) { free(ids); free(tmp); return NULL; }
    for (uint32_t i = 0; i < n; i++) ids[i] = i;
    sort_ctx_t c = { s, cmp, arg };
    merge_sort(ids, tmp, n, &c);
    free(tmp);
    return ids;
}
