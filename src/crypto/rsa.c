// Minimal RSA signing for ADB auth.
// Parses PKCS#8 DER private key, performs RSA signature (PKCS#1 v1.5).
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define MAX_LIMBS 64 // 2048 bits = 64 × 32-bit limbs
#define KEY_BYTES 256

typedef struct {
    uint32_t m[MAX_LIMBS]; // modulus n
    uint32_t e[MAX_LIMBS]; // public exponent
    uint32_t d[MAX_LIMBS]; // private exponent
    int bits;
} rsa_key;

static int cmp_shift(const uint32_t *a, const uint32_t *m, int limbs, int shift_bits) {
    int shift_words = shift_bits / 32;
    int shift_rem = shift_bits % 32;
    for (int i = limbs * 2 - 1; i >= 0; i--) {
        uint32_t m_val = 0;
        if (i >= shift_words && i - shift_words <= limbs) {
            uint32_t lo = (i - shift_words < limbs) ? m[i - shift_words] : 0;
            uint32_t hi = (i - shift_words - 1 >= 0 && i - shift_words - 1 < limbs) ? m[i - shift_words - 1] : 0;
            m_val = (lo << shift_rem);
            if (shift_rem > 0) m_val |= (hi >> (32 - shift_rem));
        }
        if (a[i] > m_val) return 1;
        if (a[i] < m_val) return -1;
    }
    return 0;
}

static void sub_shift(uint32_t *a, const uint32_t *m, int limbs, int shift_bits) {
    int shift_words = shift_bits / 32;
    int shift_rem = shift_bits % 32;
    uint64_t borrow = 0;
    for (int i = 0; i < limbs * 2; i++) {
        uint32_t m_val = 0;
        if (i >= shift_words && i - shift_words <= limbs) {
            uint32_t lo = (i - shift_words < limbs) ? m[i - shift_words] : 0;
            uint32_t hi = (i - shift_words - 1 >= 0 && i - shift_words - 1 < limbs) ? m[i - shift_words - 1] : 0;
            m_val = (lo << shift_rem);
            if (shift_rem > 0) m_val |= (hi >> (32 - shift_rem));
        }
        uint64_t sub = (uint64_t)m_val + borrow;
        if (a[i] < (sub & 0xFFFFFFFF)) borrow = 1; else borrow = 0;
        a[i] -= (uint32_t)sub;
    }
}

static void mul_mod(uint32_t *r, const uint32_t *a, const uint32_t *b,
                    const uint32_t *m, int limbs) {
    uint32_t tmp[MAX_LIMBS * 2] = {0};
    for (int i = 0; i < limbs; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < limbs; j++) {
            uint64_t prod = (uint64_t)a[i] * b[j] + tmp[i + j] + carry;
            tmp[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        tmp[i + limbs] = (uint32_t)carry;
    }

    for (int i = limbs * 32; i >= 0; i--) {
        if (cmp_shift(tmp, m, limbs, i) >= 0) {
            sub_shift(tmp, m, limbs, i);
        }
    }
    memcpy(r, tmp, limbs * 4);
}

static void mod_exp(uint32_t *result, const uint32_t *base,
                    const uint32_t *exp, const uint32_t *mod, int limbs) {
    uint32_t r[MAX_LIMBS] = {0}; r[0] = 1;
    uint32_t b[MAX_LIMBS];
    memcpy(b, base, limbs * 4);
    int bits = limbs * 32;
    for (int i = 0; i < bits; i++) {
        if (exp[i >> 5] & (1U << (i & 31))) {
            uint32_t tmp[MAX_LIMBS];
            memcpy(tmp, r, limbs * 4);
            mul_mod(r, tmp, b, mod, limbs);
        }
        uint32_t tmp[MAX_LIMBS];
        memcpy(tmp, b, limbs * 4);
        mul_mod(b, tmp, tmp, mod, limbs);
    }
    memcpy(result, r, limbs * 4);
}

// Parse PKCS#8 DER private key, extract n and d
static int der_read_len(const uint8_t *der, size_t len, size_t *off) {
    if (*off >= len) return -1;
    uint8_t b = der[*off]; (*off)++;
    if (!(b & 0x80)) return b;
    int nbytes = b & 0x7F;
    if (nbytes == 0 || nbytes > 4 || *off + nbytes > len) return -1;
    int val = 0;
    for (int i = 0; i < nbytes; i++) { val = (val << 8) | der[*off]; (*off)++; }
    return val;
}

static int parse_pkcs8_der(const uint8_t *der, size_t len, rsa_key *key) {
    // PKCS#8: SEQUENCE { INTEGER(0), SEQUENCE { OID, NULL }, OCTET_STRING { RSAPrivateKey } }
    // RSAPrivateKey: SEQUENCE { version, n, e, d, p, q, dp, dq, qinv }
    if (len < 4 || der[0] != 0x30) return -1;
    size_t off = 1;
    int seqlen = der_read_len(der, len, &off);
    if (seqlen < 0 || off + seqlen > len) return -1;

    // Skip INTEGER(0)
    if (off >= len || der[off] != 0x02) return -1;
    off++;
    int ilen = der_read_len(der, len, &off);
    if (ilen < 0) return -1;
    off += ilen;

    // Skip algorithm SEQUENCE (OID + NULL)
    if (off >= len || der[off] != 0x30) return -1;
    off++;
    int alen = der_read_len(der, len, &off);
    if (alen < 0) return -1;
    off += alen;

    // Read OCTET_STRING containing RSAPrivateKey
    if (off >= len || der[off] != 0x04) return -1;
    off++;
    int olen = der_read_len(der, len, &off);
    if (olen < 0 || off + olen > len) return -1;
    const uint8_t *rsa = der + off;
    size_t rslen = olen;

    // RSAPrivateKey: SEQUENCE
    if (rslen < 2 || rsa[0] != 0x30) return -1;
    size_t idx = 1;
    int rsa_seqlen = der_read_len(rsa, rslen, &idx);
    if (rsa_seqlen < 0 || idx + rsa_seqlen > rslen) return -1;

    // Skip version INTEGER
    if (idx >= rslen || rsa[idx] != 0x02) return -1;
    idx++;
    int vlen = der_read_len(rsa, rslen, &idx);
    if (vlen < 0) return -1;
    idx += vlen;

    // Read n (INTEGER)
    if (idx >= rslen || rsa[idx] != 0x02) return -1;
    idx++;
    int nlen = der_read_len(rsa, rslen, &idx);
    if (nlen < 0 || nlen > KEY_BYTES + 1) return -1;
    memset(key->m, 0, sizeof(key->m));
    if (rsa[idx] == 0) { idx++; nlen--; }
    for (int i = 0; i < nlen; i++)
        key->m[(nlen - 1 - i) / 4] |= (uint32_t)rsa[idx + i] << (((nlen - 1 - i) % 4) * 8);
    key->bits = nlen * 8;
    idx += nlen;

    // Read e (INTEGER)
    if (idx >= rslen || rsa[idx] != 0x02) return -1;
    idx++;
    int elen = der_read_len(rsa, rslen, &idx);
    if (elen < 0 || elen > 8) return -1;
    memset(key->e, 0, sizeof(key->e));
    for (int i = 0; i < elen; i++)
        key->e[(elen - 1 - i) / 4] |= (uint32_t)rsa[idx + i] << (((elen - 1 - i) % 4) * 8);
    idx += elen;

    // Read d (INTEGER)
    if (idx >= rslen || rsa[idx] != 0x02) return -1;
    idx++;
    int dlen = der_read_len(rsa, rslen, &idx);
    if (dlen < 0 || dlen > KEY_BYTES + 1) return -1;
    memset(key->d, 0, sizeof(key->d));
    if (rsa[idx] == 0) { idx++; dlen--; }
    for (int i = 0; i < dlen; i++)
        key->d[(dlen - 1 - i) / 4] |= (uint32_t)rsa[idx + i] << (((dlen - 1 - i) % 4) * 8);

    return 0;
}

int rsa_load_key(const char *path, rsa_key *key) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return -1; }
    fread(buf, 1, sz, fp);
    fclose(fp);

    // Find base64 data between PEM headers
    const char *start = (const char *)buf;
    const char *b64 = strstr(start, "\n");
    if (!b64) { free(buf); return -1; }
    b64++;
    const char *end = strstr(b64, "-----");
    if (!end) { free(buf); return -1; }
    size_t b64len = end - b64;

    // Decode base64 to DER
    static signed char b64t_init;
    static signed char b64t[256];
    if (!b64t_init) {
        for (int i = 0; i < 256; i++) b64t[i] = -1;
        b64t['A']=0; b64t['B']=1; b64t['C']=2; b64t['D']=3;
        b64t['E']=4; b64t['F']=5; b64t['G']=6; b64t['H']=7;
        b64t['I']=8; b64t['J']=9; b64t['K']=10; b64t['L']=11;
        b64t['M']=12; b64t['N']=13; b64t['O']=14; b64t['P']=15;
        b64t['Q']=16; b64t['R']=17; b64t['S']=18; b64t['T']=19;
        b64t['U']=20; b64t['V']=21; b64t['W']=22; b64t['X']=23;
        b64t['Y']=24; b64t['Z']=25; b64t['a']=26; b64t['b']=27;
        b64t['c']=28; b64t['d']=29; b64t['e']=30; b64t['f']=31;
        b64t['g']=32; b64t['h']=33; b64t['i']=34; b64t['j']=35;
        b64t['k']=36; b64t['l']=37; b64t['m']=38; b64t['n']=39;
        b64t['o']=40; b64t['p']=41; b64t['q']=42; b64t['r']=43;
        b64t['s']=44; b64t['t']=45; b64t['u']=46; b64t['v']=47;
        b64t['w']=48; b64t['x']=49; b64t['y']=50; b64t['z']=51;
        b64t['0']=52; b64t['1']=53; b64t['2']=54; b64t['3']=55;
        b64t['4']=56; b64t['5']=57; b64t['6']=58; b64t['7']=59;
        b64t['8']=60; b64t['9']=61; b64t['+']=62; b64t['/']=63;
        b64t_init = 1;
    }
    size_t est = b64len / 4 * 3 + 4;
    uint8_t *der = malloc(est);
    int di = 0, nb = 0;
    uint32_t acc = 0;
    for (size_t i = 0; i < b64len; i++) {
        char c = b64[i];
        if (c == '=') { break; }
        int v = b64t[(unsigned char)c];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        nb += 6;
        if (nb >= 8) {
            nb -= 8;
            der[di++] = (acc >> nb) & 0xFF;
        }
    }
    int ret = parse_pkcs8_der(der, di, key);
    free(der);
    free(buf);
    return ret;
}

// PKCS#1 v1.5 signature: DER(SHA-1 digest)
static const uint8_t pkcs1_sha1_prefix[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a,
    0x05, 0x00, 0x04, 0x14
};

// PKCS#1 v1.5 signature: DER(SHA-256 digest)
static const uint8_t pkcs1_sha256_prefix[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
    0x05, 0x00, 0x04, 0x20
};

static int rsa_sign_internal(const rsa_key *key, const uint8_t *hash, uint8_t *signature, 
                             const uint8_t *prefix, size_t prefix_len, size_t hash_len) {
    int k = key->bits / 8;
    uint8_t *em = malloc(k);
    memset(em, 0, k);
    em[0] = 0x00; em[1] = 0x01;
    int ps_len = k - 3 - (int)prefix_len - (int)hash_len;
    memset(em + 2, 0xFF, ps_len);
    em[2 + ps_len] = 0x00;
    memcpy(em + 2 + ps_len + 1, prefix, prefix_len);
    memcpy(em + 2 + ps_len + 1 + prefix_len, hash, hash_len);

    uint32_t base[MAX_LIMBS], result[MAX_LIMBS];
    memset(base, 0, sizeof(base));
    for (int i = 0; i < k; i++)
        base[(k - 1 - i) / 4] |= (uint32_t)em[i] << ((3 - (i % 4)) * 8);

    mod_exp(result, base, key->d, key->m, (k + 3) / 4);
    memset(signature, 0, k);
    for (int i = 0; i < k; i++)
        signature[i] = (result[(k - 1 - i) / 4] >> ((3 - (i % 4)) * 8)) & 0xFF;

    free(em);
    return 0;
}

int rsa_sign(const rsa_key *key, const uint8_t *hash, uint8_t *signature) {
    return rsa_sign_internal(key, hash, signature, pkcs1_sha1_prefix, sizeof(pkcs1_sha1_prefix), 20);
}

int rsa_sign_sha256(const rsa_key *key, const uint8_t *hash, uint8_t *signature) {
    return rsa_sign_internal(key, hash, signature, pkcs1_sha256_prefix, sizeof(pkcs1_sha256_prefix), 32);
}
