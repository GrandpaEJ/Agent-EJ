/* RFC 1951 deflate encoder: LZ77 over a 32 KB window with hash chains, one
 * dynamic-Huffman block per 32K tokens. Used when an edited entry is written
 * back into an APK, so a saved APK stays close to the original's size. */
#include "format_zip_internal.h"

#include <stdlib.h>
#include <string.h>

#define WSIZE 32768
#define HBITS 15
#define CHAIN 96                    /* match candidates tried per position */
#define BLOCK_TOKENS 32768

typedef struct {
    uint8_t *p;
    size_t len, cap;
    uint64_t bits;
    int nbits;
    int oom;
} bitw;

static void put_bits(bitw *w, uint32_t v, int n) {
    w->bits |= (uint64_t)v << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) {
        if (w->len == w->cap) {
            size_t cap = w->cap ? w->cap * 2 : 4096;
            uint8_t *g = realloc(w->p, cap);
            if (!g) { w->oom = 1; w->len = 0; }
            else { w->p = g; w->cap = cap; }
        }
        if (!w->oom) w->p[w->len++] = (uint8_t)w->bits;
        w->bits >>= 8;
        w->nbits -= 8;
    }
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                   67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t CLORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static int len_code(int len) { int c = 28; while (LBASE[c] > len) c--; return c; }
static int dist_code(int d) { int c = 29; while (DBASE[c] > d) c--; return c; }

/* A token: dist 0 is a literal byte in len, else a match. */
typedef struct { uint16_t len, dist; } token;

/* Code-length codes for the concatenated literal and distance lengths. */
static int rle(const uint8_t *l, int n, uint8_t *sym, uint8_t *extra) {
    int k = 0;
    for (int i = 0; i < n;) {
        int run = 1;
        while (i + run < n && l[i + run] == l[i]) run++;
        if (l[i] == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            sym[k] = r >= 11 ? 18 : 17; extra[k++] = (uint8_t)(r >= 11 ? r - 11 : r - 3);
            i += r;
        } else if (l[i] != 0 && run >= 4) {
            sym[k] = l[i]; extra[k++] = 0;
            int r = run - 1 > 6 ? 6 : run - 1;
            sym[k] = 16; extra[k++] = (uint8_t)(r - 3);
            i += r + 1;
        } else {
            sym[k] = l[i]; extra[k++] = 0;
            i++;
        }
    }
    return k;
}

static void emit_block(bitw *w, const token *t, int nt, int last) {
    uint32_t lf[286] = {0}, df[30] = {0}, cf[19] = {0};
    for (int i = 0; i < nt; i++) {
        if (!t[i].dist) lf[t[i].len]++;
        else { lf[257 + len_code(t[i].len)]++; df[dist_code(t[i].dist)]++; }
    }
    lf[256] = 1;
    uint8_t ll[286], dl[30], cl[19];
    uint16_t lc[286], dc[30], cc[19];
    zip_huff_lengths(lf, 286, 15, ll);
    zip_huff_lengths(df, 30, 15, dl);
    int any = 0;
    for (int i = 0; i < 30; i++) any |= dl[i];
    if (!any) dl[0] = 1;                                 /* no matches: one dummy distance */
    int hlit = 286, hdist = 30;
    while (hlit > 257 && !ll[hlit - 1]) hlit--;
    while (hdist > 1 && !dl[hdist - 1]) hdist--;
    uint8_t both[316], sym[316], extra[316];
    memcpy(both, ll, (size_t)hlit);
    memcpy(both + hlit, dl, (size_t)hdist);
    int ns = rle(both, hlit + hdist, sym, extra);
    for (int i = 0; i < ns; i++) cf[sym[i]]++;
    zip_huff_lengths(cf, 19, 7, cl);
    int hclen = 19;
    while (hclen > 4 && !cl[CLORDER[hclen - 1]]) hclen--;
    zip_huff_codes(ll, 286, lc);
    zip_huff_codes(dl, 30, dc);
    zip_huff_codes(cl, 19, cc);

    put_bits(w, (uint32_t)last, 1);
    put_bits(w, 2, 2);                                   /* dynamic Huffman */
    put_bits(w, (uint32_t)(hlit - 257), 5);
    put_bits(w, (uint32_t)(hdist - 1), 5);
    put_bits(w, (uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; i++) put_bits(w, cl[CLORDER[i]], 3);
    for (int i = 0; i < ns; i++) {
        put_bits(w, cc[sym[i]], cl[sym[i]]);
        if (sym[i] == 16) put_bits(w, extra[i], 2);
        else if (sym[i] == 17) put_bits(w, extra[i], 3);
        else if (sym[i] == 18) put_bits(w, extra[i], 7);
    }
    for (int i = 0; i < nt; i++) {
        if (!t[i].dist) { put_bits(w, lc[t[i].len], ll[t[i].len]); continue; }
        int c = len_code(t[i].len), d = dist_code(t[i].dist);
        put_bits(w, lc[257 + c], ll[257 + c]);
        put_bits(w, (uint32_t)(t[i].len - LBASE[c]), LEXTRA[c]);
        put_bits(w, dc[d], dl[d]);
        put_bits(w, (uint32_t)(t[i].dist - DBASE[d]), DEXTRA[d]);
    }
    put_bits(w, lc[256], ll[256]);
}

static uint32_t hash3(const uint8_t *p) {
    return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & ((1u << HBITS) - 1);
}

uint8_t *zip_deflate(const uint8_t *in, size_t n, size_t *out_n) {
    int32_t *head = malloc(sizeof(int32_t) << HBITS);
    int32_t *prev = malloc(sizeof(int32_t) * WSIZE);
    token *t = malloc(sizeof(token) * BLOCK_TOKENS);
    bitw w = {0};
    if (!head || !prev || !t) goto fail;
    memset(head, 0xff, sizeof(int32_t) << HBITS);
    int nt = 0;
    size_t i = 0;
    while (i < n) {
        int best = 0, bdist = 0;
        if (i + 3 <= n) {
            uint32_t h = hash3(in + i);
            int32_t cand = head[h];
            size_t maxlen = n - i < 258 ? n - i : 258;
            for (int k = 0; k < CHAIN && cand >= 0 && i - (size_t)cand <= WSIZE; k++) {
                const uint8_t *a = in + cand, *b = in + i;
                if (a[best] == b[best]) {
                    size_t l = 0;
                    while (l < maxlen && a[l] == b[l]) l++;
                    if ((int)l > best) { best = (int)l; bdist = (int)(i - (size_t)cand); if (l == maxlen) break; }
                }
                int32_t nx = prev[cand & (WSIZE - 1)];
                if (nx >= cand) break;
                cand = nx;
            }
        }
        size_t step = best >= 3 ? (size_t)best : 1;
        t[nt++] = best >= 3 ? (token){(uint16_t)best, (uint16_t)bdist} : (token){in[i], 0};
        for (size_t j = 0; j < step; j++, i++) {          /* index every position we pass */
            if (i + 3 > n) continue;
            uint32_t h = hash3(in + i);
            prev[i & (WSIZE - 1)] = head[h];
            head[h] = (int32_t)i;
        }
        if (nt == BLOCK_TOKENS) { emit_block(&w, t, nt, i >= n); nt = 0; if (i >= n) goto done; }
    }
    emit_block(&w, t, nt, 1);
done:
    put_bits(&w, 0, 7);                                  /* flush the last byte */
    if (w.oom) goto fail;
    free(head); free(prev); free(t);
    *out_n = w.len;
    return w.p;
fail:
    free(head); free(prev); free(t); free(w.p);
    return NULL;
}
