#include "smali_evcmp.h"
#include "smali_types.h"
#include <string.h>

uint32_t smali_read_uleb(const uint8_t **p) {
    uint32_t v = 0;
    int shift = 0;
    const uint8_t *q = *p;
    do {
        v |= (uint32_t)(*q & 0x7f) << shift;
        shift += 7;
    } while (*q++ & 0x80 && shift < 35);
    *p = q;
    return v;
}

static const uint8_t *skip_annotation(const uint8_t *p) {
    smali_read_uleb(&p);
    uint32_t n = smali_read_uleb(&p);
    for (uint32_t i = 0; i < n; i++) {
        smali_read_uleb(&p);
        p = smali_ev_skip(p);
    }
    return p;
}

const uint8_t *smali_ev_skip(const uint8_t *p) {
    uint8_t type = *p & 0x1f, arg = *p >> 5;
    p++;
    switch (type) {
    case VALUE_TYPE_NULL:
    case VALUE_TYPE_BOOL:
        return p;
    case VALUE_TYPE_ARRAY: {
        uint32_t n = smali_read_uleb(&p);
        for (uint32_t i = 0; i < n; i++) p = smali_ev_skip(p);
        return p;
    }
    case VALUE_TYPE_ANNOT:
        return skip_annotation(p);
    default:
        return p + arg + 1;
    }
}

/* Raw little-endian payload of arg+1 bytes. */
static uint64_t payload(const uint8_t *p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static int64_t sign_ext(const uint8_t *p, int n) {
    uint64_t v = payload(p, n);
    int sh = 64 - 8 * n;
    return (int64_t)(v << sh) >> sh;
}

/* Right-zero-extended float/double bits back to the full width. */
static uint64_t right_ext(const uint8_t *p, int n, int width) {
    return payload(p, n) << (8 * (width - n));
}

/* Operands must be side-effect free: they are evaluated twice. */
#define CMP(x, y) ((x) < (y) ? -1 : (x) > (y))

/* Java's Float/Double.compare: numeric order, -0.0 < 0.0, NaN last. */
static int cmp_double(double x, double y, int64_t bx, int64_t by) {
    if (x < y) return -1;
    if (x > y) return 1;
    return CMP(bx, by);
}

static int cmp_fp(const uint8_t *a, int na, const uint8_t *b, int nb, int is_double) {
    if (is_double) {
        uint64_t ba = right_ext(a, na, 8), bb = right_ext(b, nb, 8);
        double x, y;
        memcpy(&x, &ba, 8); memcpy(&y, &bb, 8);
        if (x != x) ba = 0x7ff8000000000000ULL;
        if (y != y) bb = 0x7ff8000000000000ULL;
        return cmp_double(x, y, (int64_t)ba, (int64_t)bb);
    }
    uint32_t ba = (uint32_t)right_ext(a, na, 4), bb = (uint32_t)right_ext(b, nb, 4);
    float x, y;
    memcpy(&x, &ba, 4); memcpy(&y, &bb, 4);
    if (x != x) ba = 0x7fc00000u;
    if (y != y) bb = 0x7fc00000u;
    return cmp_double(x, y, (int32_t)ba, (int32_t)bb);
}

int smali_ev_cmp_annotation(const uint8_t *a, const uint8_t *b) {
    uint32_t x = smali_read_uleb(&a), y = smali_read_uleb(&b);   /* type */
    if (x != y) return CMP(x, y);
    uint32_t na = smali_read_uleb(&a), nb = smali_read_uleb(&b);
    for (uint32_t i = 0;; i++) {
        if (i == na || i == nb) return CMP(na, nb);
        x = smali_read_uleb(&a);                                    /* element name */
        y = smali_read_uleb(&b);
        if (x != y) return CMP(x, y);
        int r;
        r = smali_ev_cmp(a, b);
        if (r) return r;
        a = smali_ev_skip(a);
        b = smali_ev_skip(b);
    }
}

int smali_ev_cmp(const uint8_t *a, const uint8_t *b) {
    int ta = *a & 0x1f, tb = *b & 0x1f;
    int na = (*a >> 5) + 1, nb = (*b >> 5) + 1;
    if (ta != tb) return CMP(ta, tb);
    a++; b++;
    switch (ta) {
    case VALUE_TYPE_BYTE: case VALUE_TYPE_SHORT:
    case VALUE_TYPE_INT: case VALUE_TYPE_LONG: {
        int64_t x = sign_ext(a, na), y = sign_ext(b, nb);
        return CMP(x, y);
    }
    case VALUE_TYPE_FLOAT:
        return cmp_fp(a, na, b, nb, 0);
    case VALUE_TYPE_DOUBLE:
        return cmp_fp(a, na, b, nb, 1);
    case VALUE_TYPE_NULL:
        return 0;
    case VALUE_TYPE_BOOL:
        return CMP(na, nb);
    case VALUE_TYPE_ARRAY: {
        /* compareAsList: the size first, then element by element */
        uint32_t ca = smali_read_uleb(&a), cb = smali_read_uleb(&b);
        if (ca != cb) return CMP(ca, cb);
        for (uint32_t i = 0; i < ca; i++) {
            int r = smali_ev_cmp(a, b);
            if (r) return r;
            a = smali_ev_skip(a);
            b = smali_ev_skip(b);
        }
        return 0;
    }
    case VALUE_TYPE_ANNOT:
        return smali_ev_cmp_annotation(a, b);
    default: {   /* char and the pool references: unsigned */
        uint64_t x = payload(a, na), y = payload(b, nb);
        return CMP(x, y);
    }
    }
}

int smali_ev_cmp_array_item(const uint8_t *a, const uint8_t *b) {
    uint32_t na = smali_read_uleb(&a), nb = smali_read_uleb(&b);
    for (uint32_t i = 0;; i++) {
        if (i == na || i == nb) return CMP(na, nb);
        int r = smali_ev_cmp(a, b);
        if (r) return r;
        a = smali_ev_skip(a);
        b = smali_ev_skip(b);
    }
}
