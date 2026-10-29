/* Certificate representation digests: everything an app might hardcode to
 * compare its own signature against.  MD5 is implemented locally because the
 * rest of the tree only needs SHA-1/SHA-256. */
#include "sigkill_internal.h"
#include "crypto.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/* ------------------------------ helpers ---------------------------- */
void sk_hex(const uint8_t *d, size_t n, char *out, int upper) {
    static const char *lo = "0123456789abcdef";
    static const char *up = "0123456789ABCDEF";
    const char *hx = upper ? up : lo;
    for (size_t i = 0; i < n; i++) {
        out[i*2] = hx[d[i] >> 4];
        out[i*2+1] = hx[d[i] & 15];
    }
    out[n*2] = '\0';
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int sk_b64_decode(const char *in, size_t inlen, uint8_t **out, size_t *outlen) {
    uint8_t *buf = malloc(inlen / 4 * 3 + 4);
    if (!buf) return -1;
    size_t o = 0;
    int quad[4], qn = 0;
    for (size_t i = 0; i < inlen; i++) {
        char ch = in[i];
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
        if (ch == '=') break;
        int v = b64val(ch);
        if (v < 0) { free(buf); return -1; }
        quad[qn++] = v;
        if (qn == 4) {
            buf[o++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
            buf[o++] = (uint8_t)(((quad[1] & 15) << 4) | (quad[2] >> 2));
            buf[o++] = (uint8_t)(((quad[2] & 3) << 6) | quad[3]);
            qn = 0;
        }
    }
    if (qn == 2) buf[o++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
    else if (qn == 3) {
        buf[o++] = (uint8_t)((quad[0] << 2) | (quad[1] >> 4));
        buf[o++] = (uint8_t)(((quad[1] & 15) << 4) | (quad[2] >> 2));
    } else if (qn == 1) { free(buf); return -1; }
    *out = buf;
    if (outlen) *outlen = o;
    return 0;
}

/* Java's Arrays.hashCode(byte[]): signed bytes, 31-based. */
int sk_hashcode(const uint8_t *d, size_t n, char *out, size_t cap) {
    int32_t h = 1;
    for (size_t i = 0; i < n; i++) h = 31 * h + (int32_t)(int8_t)d[i];
    snprintf(out, cap, "%d", h);
    return 0;
}

/* ------------------------- representation set ---------------------- */
static void rep_add(sigkill_cert *c, const char *kind,
                    const char *oldv, const char *newv) {
    if (!oldv || !*oldv) return;
    if (c->rep_count >= SIGKILL_MAX_REPS) return;
    for (int i = 0; i < c->rep_count; i++) {
        if (strcmp(c->reps[i].oldv, oldv) == 0) {
            if (!c->reps[i].newv && newv) c->reps[i].newv = strdup(newv);
            return;
        }
    }
    sigkill_rep *r = &c->reps[c->rep_count++];
    r->oldv = strdup(oldv);
    r->newv = newv ? strdup(newv) : NULL;
    snprintf(r->kind, sizeof(r->kind), "%s", kind);
}

static char *b64_of(const uint8_t *d, size_t n) {
    size_t len = 0;
    return base64_encode(d, n, &len);
}

typedef struct {
    char *cert_hex, *cert_hex_uc;
    char *md5_hex, *md5_hex_uc;
    char *sha1_hex, *sha1_hex_uc;
    char *sha256_hex, *sha256_hex_uc;
    char *cert_b64, *md5_b64, *sha1_b64, *sha256_b64;
    char *hashcode;
} rep_set;

static void repset_build(rep_set *s, const uint8_t *der, size_t len) {
    memset(s, 0, sizeof(*s));
    uint8_t md5[16], sha1d[20], sha256d[32];
    char tmp[65];
    s->cert_hex = malloc(len * 2 + 1);
    s->cert_hex_uc = malloc(len * 2 + 1);
    if (!s->cert_hex || !s->cert_hex_uc) return;
    sk_hex(der, len, s->cert_hex, 0);
    sk_hex(der, len, s->cert_hex_uc, 1);
    sk_md5(der, len, md5);
    sha1(der, len, sha1d);
    sha256(der, len, sha256d);
    sk_hex(md5, 16, tmp, 0); s->md5_hex = strdup(tmp);
    sk_hex(md5, 16, tmp, 1); s->md5_hex_uc = strdup(tmp);
    sk_hex(sha1d, 20, tmp, 0); s->sha1_hex = strdup(tmp);
    sk_hex(sha1d, 20, tmp, 1); s->sha1_hex_uc = strdup(tmp);
    sk_hex(sha256d, 32, tmp, 0); s->sha256_hex = strdup(tmp);
    sk_hex(sha256d, 32, tmp, 1); s->sha256_hex_uc = strdup(tmp);
    s->cert_b64 = b64_of(der, len);
    s->md5_b64 = b64_of(md5, 16);
    s->sha1_b64 = b64_of(sha1d, 20);
    s->sha256_b64 = b64_of(sha256d, 32);
    s->hashcode = malloc(24);
    if (s->hashcode) sk_hashcode(der, len, s->hashcode, 24);
}

static void repset_free(rep_set *s) {
    free(s->cert_hex); free(s->cert_hex_uc);
    free(s->md5_hex); free(s->md5_hex_uc);
    free(s->sha1_hex); free(s->sha1_hex_uc);
    free(s->sha256_hex); free(s->sha256_hex_uc);
    free(s->cert_b64); free(s->md5_b64); free(s->sha1_b64); free(s->sha256_b64);
    free(s->hashcode);
}

int sigkill_compute_reps(sigkill_cert *c) {
    if (!c->old_der || !c->old_len) return -1;

    rep_set o, n;
    repset_build(&o, c->old_der, c->old_len);
    if (!o.cert_hex) { repset_free(&o); return -1; }
    int has_new = c->new_der && c->new_len;
    if (has_new) {
        repset_build(&n, c->new_der, c->new_len);
        if (!n.cert_hex) has_new = 0;
    } else {
        memset(&n, 0, sizeof(n));
    }

    static const char *kinds[] = {
        "cert_hex", "cert_hex_uc", "md5_hex", "md5_hex_uc",
        "sha1_hex", "sha1_hex_uc", "sha256_hex", "sha256_hex_uc",
        "cert_b64", "md5_b64", "sha1_b64", "sha256_b64", "hashcode"
    };
    char *const oldv[] = {
        o.cert_hex, o.cert_hex_uc, o.md5_hex, o.md5_hex_uc,
        o.sha1_hex, o.sha1_hex_uc, o.sha256_hex, o.sha256_hex_uc,
        o.cert_b64, o.md5_b64, o.sha1_b64, o.sha256_b64, o.hashcode
    };
    char *const newv[] = {
        n.cert_hex, n.cert_hex_uc, n.md5_hex, n.md5_hex_uc,
        n.sha1_hex, n.sha1_hex_uc, n.sha256_hex, n.sha256_hex_uc,
        n.cert_b64, n.md5_b64, n.sha1_b64, n.sha256_b64, n.hashcode
    };

    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        const char *nv = has_new ? newv[i] : NULL;
        if (nv && oldv[i] && strcmp(nv, oldv[i]) == 0) nv = NULL;
        rep_add(c, kinds[i], oldv[i], nv);
    }

    repset_free(&o);
    if (has_new) repset_free(&n);
    return 0;
}
