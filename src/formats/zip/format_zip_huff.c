/* Huffman code lengths and canonical codes for the deflate encoder.
 * Lengths come from a plain Huffman tree; when a code would be longer than
 * the limit, the counts are halved (kept non-zero) and the tree rebuilt,
 * which converges in a few rounds and costs almost nothing in ratio. */
#include "format_zip_internal.h"

#include <string.h>

typedef struct {
    uint32_t w;
    int16_t left, right;   /* child node ids, -1 for a leaf */
    int16_t sym;
} hnode;

/* Depth of every leaf under node [id]. */
static void depths(const hnode *t, int id, int d, uint8_t *len, int *max) {
    if (t[id].left < 0) {
        len[t[id].sym] = (uint8_t)(d ? d : 1);
        if (len[t[id].sym] > *max) *max = len[t[id].sym];
        return;
    }
    depths(t, t[id].left, d + 1, len, max);
    depths(t, t[id].right, d + 1, len, max);
}

/* Index of the lightest live node, skipping [skip]. */
static int lightest(const hnode *t, const uint8_t *live, int n, int skip) {
    int best = -1;
    for (int i = 0; i < n; i++)
        if (live[i] && i != skip && (best < 0 || t[i].w < t[best].w)) best = i;
    return best;
}

static int build(const uint32_t *freq, int n, uint8_t *len) {
    hnode t[2 * ZIP_HUFF_MAXSYM];
    uint8_t live[2 * ZIP_HUFF_MAXSYM];
    int nodes = 0, alive = 0;
    memset(len, 0, (size_t)n);
    memset(live, 0, sizeof(live));
    for (int s = 0; s < n; s++) {
        if (!freq[s]) continue;
        t[nodes] = (hnode){freq[s], -1, -1, (int16_t)s};
        live[nodes++] = 1;
        alive++;
    }
    if (alive == 0) return 0;
    if (alive == 1) {                       /* a lone code still needs a partner */
        int s = t[0].sym;
        len[s] = 1;
        len[s == 0 ? 1 : 0] = 1;
        return 1;
    }
    while (alive > 1) {
        int a = lightest(t, live, nodes, -1);
        int b = lightest(t, live, nodes, a);
        live[a] = live[b] = 0;
        t[nodes] = (hnode){t[a].w + t[b].w, (int16_t)a, (int16_t)b, -1};
        live[nodes++] = 1;
        alive--;
    }
    int max = 0;
    depths(t, nodes - 1, 0, len, &max);
    return max;
}

void zip_huff_lengths(const uint32_t *freq, int n, int limit, uint8_t *len) {
    uint32_t f[ZIP_HUFF_MAXSYM];
    memcpy(f, freq, (size_t)n * sizeof(uint32_t));
    while (build(f, n, len) > limit)
        for (int s = 0; s < n; s++)
            if (f[s]) f[s] = (f[s] >> 1) | 1;
}

/* Canonical codes, bit-reversed so the LSB-first writer can emit them as is. */
void zip_huff_codes(const uint8_t *len, int n, uint16_t *code) {
    int count[16] = {0}, next[16] = {0};
    for (int s = 0; s < n; s++) count[len[s]]++;
    count[0] = 0;
    for (int b = 1, c = 0; b < 16; b++) {
        c = (c + count[b - 1]) << 1;
        next[b] = c;
    }
    for (int s = 0; s < n; s++) {
        if (!len[s]) { code[s] = 0; continue; }
        unsigned c = (unsigned)next[len[s]]++, r = 0;
        for (int i = 0; i < len[s]; i++) { r = (r << 1) | (c & 1); c >>= 1; }
        code[s] = (uint16_t)r;
    }
}
