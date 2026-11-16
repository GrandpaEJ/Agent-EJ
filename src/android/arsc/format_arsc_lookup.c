/* Resource id <-> "type/name" over every config chunk of a type (aapt2 puts
 * many locales/densities in separate chunks; an entry can be absent from the
 * first). Handles sparse and 16-bit offset tables and compact entries. */
#include "format_arsc_internal.h"
#include <stdio.h>
#include <string.h>

#define TYPE_FLAG_SPARSE   0x01
#define TYPE_FLAG_OFFSET16 0x02
#define ENTRY_FLAG_COMPACT 0x0008

/* Stored entries of one type chunk: n-th slot → (entry index, entry pointer). */
static uint32_t chunk_slots(const uint8_t *c) { return arsc_r32(c + 12); }

static const uint8_t *chunk_slot(const uint8_t *c, uint32_t n, uint32_t *idx) {
    const uint8_t *offs = c + arsc_r16(c + 2);
    const uint8_t *base = c + arsc_r32(c + 16);
    uint8_t flags = c[9];
    if (flags & TYPE_FLAG_SPARSE) {
        *idx = arsc_r16(offs + n * 4);
        return base + (uint32_t)arsc_r16(offs + n * 4 + 2) * 4;
    }
    *idx = n;
    if (flags & TYPE_FLAG_OFFSET16) {
        uint16_t o = arsc_r16(offs + n * 2);
        return o == 0xFFFF ? NULL : base + (uint32_t)o * 4;
    }
    uint32_t o = arsc_r32(offs + n * 4);
    return o == 0xFFFFFFFF ? NULL : base + o;
}

static uint32_t entry_key(const uint8_t *e) {
    return (arsc_r16(e + 2) & ENTRY_FLAG_COMPACT) ? arsc_r16(e) : arsc_r32(e + 4);
}

/* Walks the type chunks of package p with type id tid; stops when fn returns nonzero. */
typedef int (*chunk_fn)(const uint8_t *chunk, void *arg);
static void each_chunk(arsc_ctx *ctx, int p, uint8_t tid, chunk_fn fn, void *arg) {
    uint32_t pkg = ctx->packages[p].pkg_off;
    uint32_t off = pkg + arsc_r16(ctx->data + pkg + 2);
    uint32_t end = pkg + arsc_r32(ctx->data + pkg + 4);
    while (off + 20 <= end) {
        uint16_t t = arsc_r16(ctx->data + off);
        uint32_t cs = arsc_r32(ctx->data + off + 4);
        if (cs < 8 || off + cs > end) break;
        if (t == RES_TABLE_TYPE_TYPE && ctx->data[off + 8] == tid && fn(ctx->data + off, arg)) return;
        off += cs;
    }
}

struct find_key { uint32_t idx, key; int found; };
static int by_index(const uint8_t *c, void *arg) {
    struct find_key *f = arg;
    uint32_t n = chunk_slots(c), idx;
    uint8_t sparse = c[9] & TYPE_FLAG_SPARSE;
    if (!sparse && f->idx >= n) return 0;
    for (uint32_t i = sparse ? 0 : f->idx; i < (sparse ? n : f->idx + 1); i++) {
        const uint8_t *e = chunk_slot(c, i, &idx);
        if (e && idx == f->idx) { f->key = entry_key(e); f->found = 1; return 1; }
    }
    return 0;
}

struct find_name { const uint8_t *keys; const char *name; uint32_t idx; int found; };
static int by_name(const uint8_t *c, void *arg) {
    struct find_name *f = arg;
    uint32_t n = chunk_slots(c), idx;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = chunk_slot(c, i, &idx);
        if (!e) continue;
        const char *k = arsc_sp_string(f->keys, entry_key(e));
        if (k && strcmp(k, f->name) == 0) { f->idx = idx; f->found = 1; return 1; }
    }
    return 0;
}

static int pkg_of(arsc_ctx *ctx, uint8_t id) {
    for (int p = 0; p < ctx->package_count; p++) if (ctx->packages[p].id == id) return p;
    return -1;
}

uint32_t arsc_reverse_lookup(arsc_ctx *ctx, const char *type_name, const char *key_name) {
    if (!ctx || !type_name || !key_name) return 0xFFFFFFFF;
    for (int p = 0; p < ctx->package_count; p++) {
        for (int t = 0; t < ctx->packages[p].type_count; t++) {
            const char *tn = ctx->packages[p].types[t].name;
            if (!tn || strcmp(tn, type_name) != 0) continue;
            struct find_name f = { ctx->packages[p].key_pool, key_name, 0, 0 };
            each_chunk(ctx, p, ctx->packages[p].types[t].id, by_name, &f);
            if (f.found) return ((uint32_t)ctx->packages[p].id << 24) | ((uint32_t)ctx->packages[p].types[t].id << 16) | f.idx;
        }
    }
    return 0xFFFFFFFF;
}

const char *arsc_lookup_id(arsc_ctx *ctx, uint32_t res_id) {
    if (!ctx) return NULL;
    int p = pkg_of(ctx, (uint8_t)(res_id >> 24));
    uint8_t tid = (res_id >> 16) & 0xFF;
    if (p < 0 || tid == 0) return NULL;
    struct find_key f = { res_id & 0xFFFF, 0, 0 };
    each_chunk(ctx, p, tid, by_index, &f);
    if (!f.found) return NULL;
    static char buf[256];
    char key[200];
    const char *k = arsc_sp_string(ctx->packages[p].key_pool, f.key);
    if (!k) return NULL;
    snprintf(key, sizeof(key), "%s", k); /* arsc_sp_string reuses one buffer */
    const char *tn = arsc_sp_string(ctx->packages[p].type_pool, tid - 1u);
    if (!tn) return NULL;
    snprintf(buf, sizeof(buf), "%s/%s", tn, key);
    /* Obfuscated tables give many entries the same key (every string
     * "HEHE_BOI"); a name that resolves to another entry would rebind the
     * reference, so callers get NULL and print the raw id instead. */
    char type[56];
    snprintf(type, sizeof(type), "%s", tn);
    if (arsc_reverse_lookup(ctx, type, key) != res_id) return NULL;
    return buf;
}
