#include "formats.h"
#include <stdlib.h>
#include <string.h>

// RFC 1951 Deflate inflater.
// Uses direct lookup at exact reversed-code positions.

#define MAX_BITS 15
#define TB_SIZE (1 << MAX_BITS)

typedef struct {
    int16_t sym;
    int16_t len;
} huff_entry_t;

typedef struct {
    huff_entry_t table[TB_SIZE];
} huff_t;

static void huff_build(huff_t *h, const int *lens, int count) {
    for (int i = 0; i < TB_SIZE; i++)
        h->table[i].sym = -1;

    int bl_count[MAX_BITS + 1] = {0};
    for (int i = 0; i < count; i++)
        if (lens[i] > 0) bl_count[lens[i]]++;

    int code = 0, next_code[MAX_BITS + 1] = {0};
    for (int b = 1; b <= MAX_BITS; b++) {
        code = (code + bl_count[b - 1]) << 1;
        next_code[b] = code;
    }

    for (int i = 0; i < count; i++) {
        int len = lens[i];
        if (len == 0) continue;
        int code_val = next_code[len]++;
        h->table[code_val].sym = i;
        h->table[code_val].len = len;
    }
}

typedef struct {
    const uint8_t *start;
    const uint8_t *in;
    size_t in_len;
    uint8_t *out;
    size_t out_len;
    size_t out_cap;
    int bit_buf;
    int bit_cnt;
} stream_t;

static int read_bit(stream_t *s) {
    if (s->bit_cnt == 0) {
        if ((size_t)(s->in - s->start) >= s->in_len) return -1;
        s->bit_buf = *s->in++;
        s->bit_cnt = 8;
    }
    int b = s->bit_buf & 1;
    s->bit_buf >>= 1;
    s->bit_cnt--;
    return b;
}

static int read_bits(stream_t *s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        int b = read_bit(s);
        if (b < 0) return -1;
        v |= b << i;
    }
    return v;
}

static int read_huff(stream_t *s, huff_t *h) {
    int code = 0;
    for (int b = 1; b <= MAX_BITS; b++) {
        int bit = read_bit(s);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        if (h->table[code].len == b)
            return h->table[code].sym;
    }
    return -1;
}

static int write_byte(stream_t *s, uint8_t b) {
    if (s->out_len >= s->out_cap) {
        s->out_cap = s->out_cap ? s->out_cap * 2 : 4096;
        uint8_t *p = realloc(s->out, s->out_cap);
        if (!p) return -1;
        s->out = p;
    }
    s->out[s->out_len++] = b;
    return 0;
}

static int decode_lz(stream_t *s, int len, int dist) {
    for (int i = 0; i < len; i++) {
        if (dist > (int)s->out_len) return -1;
        uint8_t b = s->out[s->out_len - dist];
        if (write_byte(s, b) < 0) return -1;
    }
    return 0;
}

int len_base[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
int len_extra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
int dst_base[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
int dst_extra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static const int fixed_dst[32] = {
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

static int inflate_block(stream_t *s) {
    int final = read_bit(s);
    int type = read_bits(s, 2);
    if (type < 0) return -1;

    if (type == 0) {
        s->bit_cnt = 0;
        if ((size_t)(s->in - s->start) + 4 > s->in_len) return -1;
        int len = s->in[0] | (s->in[1] << 8);
        s->in += 4;
        if ((size_t)(s->in - s->start) + len > s->in_len) return -1;
        for (int i = 0; i < len; i++)
            if (write_byte(s, s->in[i]) < 0) return -1;
        s->in += len;
        return final;
    }

    huff_t hl, hd;
    memset(&hl, 0, sizeof(hl));
    memset(&hd, 0, sizeof(hd));

    if (type == 1) {
        /* RFC 1951 3.2.6. Built from the spec's ranges: the old literal
         * table had only 256 initializers (80 nines instead of 112, the
         * 7-bit codes at 224..247), so every fixed-Huffman entry — what
         * deflate picks for small files — failed and was silently dropped. */
        int fixed_lit[288];
        for (int i = 0; i < 288; i++)
            fixed_lit[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        huff_build(&hl, fixed_lit, 288);
        huff_build(&hd, fixed_dst, 32);
    } else {
        int nl = read_bits(s, 5) + 257;
        int nd = read_bits(s, 5) + 1;
        int nc = read_bits(s, 4) + 4;
        if (nc > 19) nc = 19;
        static int cl_order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        int cl_lens[19] = {0};
        for (int i = 0; i < nc; i++)
            cl_lens[cl_order[i]] = read_bits(s, 3);

        huff_t hc;
        memset(&hc, 0, sizeof(hc));
        huff_build(&hc, cl_lens, 19);

        int lens[320], li = 0;
        while (li < nl + nd) {
            int sym = read_huff(s, &hc);
            if (sym < 0) return -1;
            if (sym < 16) { lens[li++] = sym; }
            else if (sym == 16) {
                int rep = read_bits(s, 2) + 3;
                int val = li > 0 ? lens[li - 1] : 0;
                for (int j = 0; j < rep && li < nl + nd; j++) lens[li++] = val;
            } else if (sym == 17) {
                int rep = read_bits(s, 3) + 3;
                for (int j = 0; j < rep && li < nl + nd; j++) lens[li++] = 0;
            } else {
                int rep = read_bits(s, 7) + 11;
                for (int j = 0; j < rep && li < nl + nd; j++) lens[li++] = 0;
            }
        }
        huff_build(&hl, lens, nl);
        huff_build(&hd, lens + nl, nd);
    }

    for (;;) {
        int sym = read_huff(s, &hl);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (write_byte(s, (uint8_t)sym) < 0) return -1;
        } else if (sym == 256) {
            return final;
        } else {
            int idx = sym - 257;
            if (idx >= 29) return -1;
            int len = len_base[idx] + read_bits(s, len_extra[idx]);
            int dsym = read_huff(s, &hd);
            if (dsym < 0 || dsym >= 30) return -1;
            int dist = dst_base[dsym] + read_bits(s, dst_extra[dsym]);
            if (decode_lz(s, len, dist) < 0) return -1;
        }
    }
}

void *zip_inflate(const uint8_t *in, size_t in_len, size_t *out_size) {
    stream_t s;
    memset(&s, 0, sizeof(s));
    s.start = in;
    s.in = in;
    s.in_len = in_len;
    s.out = malloc(4096);
    if (!s.out) return NULL;
    s.out_cap = 4096;

    for (;;) {
        int final = inflate_block(&s);
        if (final < 0) { free(s.out); return NULL; }
        if (final) break;
    }

    *out_size = s.out_len;
    return s.out;
}
