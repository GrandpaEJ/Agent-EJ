/* Locate the original signing certificate: raw DER, PEM, a PKCS#7 signature
 * block (META-INF/CERT.RSA), an APK, or a decoded tree. */
#include "sigkill_internal.h"
#include "crypto.h"
#include "formats.h"
#include "der.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------- DER walker --------------------------- */
static int der_tlv(const uint8_t *p, size_t n, uint8_t *tag,
                   const uint8_t **val, size_t *vlen, size_t *cons) {
    if (n < 2) return -1;
    size_t off = 0;
    uint8_t t = p[off++];
    size_t len = p[off++];
    if (len & 0x80) {
        int nb = (int)(len & 0x7f);
        if (nb == 0 || nb > 4 || off + (size_t)nb > n) return -1;
        len = 0;
        for (int i = 0; i < nb; i++) len = (len << 8) | p[off++];
    }
    if (off + len > n) return -1;
    if (tag) *tag = t;
    if (val) *val = p + off;
    if (vlen) *vlen = len;
    if (cons) *cons = off + len;
    return 0;
}

/* PKCS#7 ContentInfo -> SignedData -> certificates [0]. */
static int pkcs7_first_cert(const uint8_t *p, size_t n,
                            const uint8_t **cert, size_t *clen) {
    const uint8_t *v; size_t vl, cons;
    if (der_tlv(p, n, NULL, &v, &vl, &cons) != 0) return -1;
    const uint8_t *q = v; size_t qn = vl;
    /* contentType OID */
    if (der_tlv(q, qn, NULL, NULL, NULL, &cons) != 0) return -1;
    q += cons; qn -= cons;
    /* [0] EXPLICIT -> SignedData SEQUENCE */
    const uint8_t *sd; size_t sdl, sdc;
    if (der_tlv(q, qn, NULL, &sd, &sdl, &sdc) != 0) return -1;
    if (der_tlv(sd, sdl, NULL, &sd, &sdl, &sdc) != 0) return -1;
    q = sd; qn = sdl;
    /* version, digestAlgorithms, contentInfo */
    for (int i = 0; i < 3; i++) {
        if (der_tlv(q, qn, NULL, NULL, NULL, &cons) != 0) return -1;
        q += cons; qn -= cons;
    }
    /* certificates [0] IMPLICIT */
    uint8_t tag;
    const uint8_t *set; size_t setl;
    if (qn == 0 || der_tlv(q, qn, &tag, &set, &setl, &cons) != 0) return -1;
    if (tag != 0xA0) return -1;
    const uint8_t *c; size_t cl;
    if (der_tlv(set, setl, &tag, &c, &cl, &cons) != 0 || tag != 0x30)
        return -1;
    *cert = set;
    *clen = cons;
    return (int)cons;
}

static int looks_like_cert(const uint8_t *p, size_t n) {
    /* Certificate ::= SEQUENCE { tbsCertificate SEQUENCE, algorithm, signature }
     * The version [0] lives INSIDE tbsCertificate, so the first inner element
     * is a SEQUENCE, not an INTEGER.  Confirm with a real SPKI extraction. */
    if (n < 8 || p[0] != 0x30) return 0;
    const uint8_t *v; size_t vl, cons;
    if (der_tlv(p, n, NULL, &v, &vl, &cons) != 0) return 0;
    uint8_t tag;
    if (der_tlv(v, vl, &tag, NULL, NULL, &cons) != 0 || tag != 0x30)
        return 0;
    uint8_t *spki = der_pubkey_from_cert(p, n, NULL);
    free(spki);
    return spki != NULL;
}

int sk_find_cert_der(const uint8_t *buf, size_t len,
                     const uint8_t **cert, size_t *clen) {
    if (!buf || len < 8) return -1;
    const uint8_t *c; size_t cl;
    if (pkcs7_first_cert(buf, len, &c, &cl) == 0 && looks_like_cert(c, cl)) {
        *cert = c; *clen = cl;
        return 0;
    }
    /* Raw certificate? */
    if (looks_like_cert(buf, len)) {
        const uint8_t *v; size_t vl, cons;
        if (der_tlv(buf, len, NULL, &v, &vl, &cons) == 0) {
            *cert = buf; *clen = cons;
            return 0;
        }
    }
    /* Brute force: the longest SEQUENCE that parses as an X.509 cert. */
    const uint8_t *best = NULL; size_t best_len = 0;
    for (size_t i = 0; i + 8 < len; i++) {
        if (buf[i] != 0x30) continue;
        const uint8_t *v; size_t vl, cons;
        if (der_tlv(buf + i, len - i, NULL, &v, &vl, &cons) != 0) continue;
        if (cons <= best_len) continue;
        if (!looks_like_cert(buf + i, cons)) continue;
        best = buf + i;
        best_len = cons;
    }
    if (best) { *cert = best; *clen = best_len; return 0; }
    return -1;
}

/* ----------------------------- PEM ---------------------------------- */
static int cert_from_pem(const char *text, size_t len,
                         uint8_t **out, size_t *outlen) {
    const char *begin = strstr(text, "-----BEGIN CERTIFICATE-----");
    if (!begin) return -1;
    begin += strlen("-----BEGIN CERTIFICATE-----");
    const char *end = strstr(begin, "-----END CERTIFICATE-----");
    size_t n = end ? (size_t)(end - begin) : len - (size_t)(begin - text);
    return sk_b64_decode(begin, n, out, outlen);
}

/* --------------------------- certificate ---------------------------- */
static void cert_store(sigkill_cert *c, const uint8_t *der, size_t len,
                       const char *source) {
    c->old_der = malloc(len);
    if (!c->old_der) return;
    memcpy(c->old_der, der, len);
    c->old_len = len;
    snprintf(c->old_source, sizeof(c->old_source), "%s", source ? source : "?");
    uint8_t h[32];
    sha256(der, len, h);
    sk_hex(h, 32, c->old_sha256, 0);
    size_t blen = 0;
    c->sig_b64 = base64_encode(der, len, &blen);
}

int sigkill_cert_load_file(sigkill_cert *c, const char *path) {
    size_t len = 0;
    char *data = sk_read_file(path, &len);
    if (!data) return -1;
    uint8_t *der = NULL; size_t dlen = 0;
    int rc;
    if (strstr(data, "BEGIN CERTIFICATE")) {
        rc = cert_from_pem(data, len, &der, &dlen);
    } else {
        const uint8_t *p; size_t pl;
        rc = sk_find_cert_der((const uint8_t *)data, len, &p, &pl);
        if (rc == 0) { der = malloc(pl); if (der) { memcpy(der, p, pl); dlen = pl; } }
    }
    free(data);
    if (rc != 0 || !der) { free(der); return -1; }
    cert_store(c, der, dlen, path);
    free(der);
    return c->old_der ? 0 : -1;
}

int sk_cert_from_blob(sigkill_cert *c, const uint8_t *blob, size_t len,
                       const char *source) {
    const uint8_t *p; size_t pl;
    if (sk_find_cert_der(blob, len, &p, &pl) != 0) return -1;
    cert_store(c, p, pl, source);
    return c->old_der ? 0 : -1;
}

int sigkill_cert_load(sigkill_cert *c, const char *orig_cert,
                      const char *orig_apk, const char *tree_dir) {
    memset(c, 0, sizeof(*c));
    if (orig_cert) return sigkill_cert_load_file(c, orig_cert);
    if (orig_apk) return cert_from_apk(c, orig_apk);
    if (tree_dir) return cert_from_tree(c, tree_dir);
    return -1;
}

/* --------------------------- APK / tree ----------------------------- */
int sigkill_cert_set_target(sigkill_cert *c, const uint8_t *der, size_t len) {
    if (!der || !len) return -1;
    free(c->new_der);
    c->new_der = malloc(len);
    if (!c->new_der) return -1;
    memcpy(c->new_der, der, len);
    c->new_len = len;
    uint8_t h[32];
    sha256(der, len, h);
    sk_hex(h, 32, c->new_sha256, 0);
    return 0;
}

void sigkill_cert_free(sigkill_cert *c) {
    if (!c) return;
    free(c->old_der);
    free(c->new_der);
    free(c->sig_b64);
    for (int i = 0; i < c->rep_count; i++) {
        free(c->reps[i].oldv);
        free(c->reps[i].newv);
    }
    memset(c, 0, sizeof(*c));
}
