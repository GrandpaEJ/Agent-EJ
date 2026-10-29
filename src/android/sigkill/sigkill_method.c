/* Heuristic detection and neutralisation of Java/Kotlin signature checks.
 * Works on the smali text the decoder produces; build_apk reassembles it. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *SIG_API[] = {
    "Landroid/content/pm/PackageManager;->getPackageInfo",
    "Landroid/content/pm/PackageManager;->getPackageArchiveInfo",
    "Landroid/content/pm/PackageManager;->getInstalledPackages",
    "Landroid/content/pm/PackageManager;->getInstalledApplications",
    "Landroid/content/pm/PackageInfo;->signatures",
    "Landroid/content/pm/PackageInfo;->signingInfo",
    "Landroid/content/pm/PackageManager;->checkSignatures",
    "Landroid/content/pm/SigningInfo;",
    "Landroid/content/pm/Signature;",
    "Ljava/security/cert/X509Certificate;",
    "Ljava/security/cert/Certificate;",
    "getApkContentsSigners",
    "getSigningCertificateHistory",
    "hasMultipleSigners",
    "Ljava/security/MessageDigest;",
};

static int mem_has(const char *hay, size_t len, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0 || len < nl) return 0;
    for (size_t i = 0; i + nl <= len; i++)
        if (memcmp(hay + i, needle, nl) == 0) return 1;
    return 0;
}

static int body_has_sig_api(const char *body, size_t len) {
    for (size_t i = 0; i < sizeof(SIG_API) / sizeof(SIG_API[0]); i++)
        if (mem_has(body, len, SIG_API[i])) return 1;
    return 0;
}

static int extra_matches(const char *name, const char *extra) {
    if (!extra || !*extra) return 0;
    const char *p = extra;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        if (n > 0 && n < 128) {
            char frag[128];
            memcpy(frag, p, n);
            frag[n] = '\0';
            if (sk_icontains(name, frag)) return 1;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

static int name_is_check(const char *name, const char *extra) {
    if (!name || !*name || name[0] == '<') return 0;
    if (extra_matches(name, extra)) return 1;
    static const char *direct[] = {
        "signature", "signcheck", "checksign", "verifysign", "validatesign",
        "issigned", "signvalid", "sigvalid", "checkcert", "verifycert",
        "appsign", "apksign", "checkcertificate", "verifycertificate",
        "signture", "getsign", "signaturecheck", "signatureverify",
    };
    for (size_t i = 0; i < sizeof(direct) / sizeof(direct[0]); i++)
        if (sk_icontains(name, direct[i])) return 1;
    int sig = sk_icontains(name, "sig") || sk_icontains(name, "sign") ||
              sk_icontains(name, "cert");
    int cmp = sk_icontains(name, "check") || sk_icontains(name, "verif") ||
              sk_icontains(name, "valid") || sk_icontains(name, "match") ||
              sk_icontains(name, "equal") || sk_icontains(name, "cmp") ||
              sk_icontains(name, "trust") || sk_icontains(name, "tamper");
    return sig && cmp;
}

/* ------------------------- method block helpers --------------------- */
static int line_trim_eq(const char *line, size_t len, const char *what) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    size_t wl = strlen(what);
    if (len - i < wl) return 0;
    if (memcmp(line + i, what, wl) != 0) return 0;
    size_t j = i + wl;
    return j == len || line[j] == '\n' || line[j] == '\r' ||
           line[j] == ' ' || line[j] == '\t';
}

static size_t method_block_len(const char *p, const char *end) {
    const char *q = p;
    while (q < end) {
        const char *nl = memchr(q, '\n', (size_t)(end - q));
        size_t l = nl ? (size_t)(nl - q + 1) : (size_t)(end - q);
        if (line_trim_eq(q, l, ".end method")) return (size_t)(q + l - p);
        q += l;
    }
    return (size_t)(end - p);
}

static int parse_header(const char *line, size_t len, char *name, size_t ncap,
                        char *ret, size_t rcap, int *skip) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (len - i < 7 || memcmp(line + i, ".method", 7) != 0) return -1;

    char buf[1024];
    size_t n = len - i < sizeof(buf) - 1 ? len - i : sizeof(buf) - 1;
    memcpy(buf, line + i, n);
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';

    *skip = 0;
    if (strstr(buf, "abstract") || strstr(buf, " native")) *skip = 1;

    char *paren = strchr(buf, '(');
    if (!paren) return -1;
    char *nm = paren;
    while (nm > buf && nm[-1] != ' ' && nm[-1] != '\t') nm--;
    size_t nn = (size_t)(paren - nm);
    if (nn >= ncap) nn = ncap - 1;
    memcpy(name, nm, nn);
    name[nn] = '\0';

    char *close = strrchr(buf, ')');
    if (!close) return -1;
    char *r = close + 1;
    while (*r == ' ' || *r == '\t') r++;
    snprintf(ret, rcap, "%s", r);
    return 0;
}


static char *process_block(const char *block, size_t len,
                           const sigkill_opts *o, sigkill_cert *c,
                           sigkill_report *r, const char *file, int *stubbed) {
    char name[256] = {0}, ret[256] = {0};
    int skip = 0;
    if (parse_header(block, len, name, sizeof(name), ret, sizeof(ret), &skip) != 0)
        return NULL;

    int has_api = body_has_sig_api(block, len);
    int named = name_is_check(name, o->extra_names);
    int suspicious = has_api && (named || o->aggressive);

    if (!has_api && !named) {
        /* Call-site transforms must still run: the check may call
         * checkSignatures() or Signature methods without matching a name
         * heuristic or using one of the report tokens. */
        if (skip || !(o->do_pkg || o->do_call || o->do_sigobj || o->do_checkapi))
            return NULL;
        return sk_chain_transforms(block, len, o, c, r, file);
    }
    r->checks_found++;
    sk_detail(r, has_api ? "sig_api" : "sig_name", file, "%s()%s", name, ret);

    if (o->do_stub && !skip && (suspicious || o->aggressive)) {
        int code_like = ret[0] != '\0' && strchr("ZIBSCJFDV", ret[0]) != NULL;
        int obj_like = strcmp(ret, "Ljava/lang/String;") == 0 ||
                       strcmp(ret, "Landroid/content/pm/Signature;") == 0 ||
                       strcmp(ret, "[B") == 0 ||
                       strcmp(ret, "[Landroid/content/pm/Signature;") == 0;
        int name_ok = named || sk_icontains(name, "sig") ||
                      sk_icontains(name, "sign") || sk_icontains(name, "cert");
        if ((code_like || (obj_like && name_ok)) && ret[0] != '\0') {
            char *nb = sk_stub_block(block, len, file, name, ret, c, r);
            if (nb) { (*stubbed)++; return nb; }
        }
        if (code_like || obj_like)
            sk_detail(r, "stub_skip", file, "%s()%s: no safe stub", name, ret);
    }

    if (!skip && (o->do_pkg || o->do_call || o->do_sigobj || o->do_checkapi))
        return sk_chain_transforms(block, len, o, c, r, file);
    return NULL;
}

/* --------------------------- file entry ----------------------------- */
char *sk_process_smali(const char *text, size_t len, const sigkill_opts *o,
                       sigkill_cert *c, sigkill_report *r, const char *file,
                       int *stubbed) {
    /* Never touch our own injected hook class. */
    if (mem_has(text, len, ".class public final Lcom/agent/ej/SigKiller;"))
        return NULL;
    sk_buf out;
    sk_buf_init(&out);
    int changed = 0;
    size_t i = 0;
    const char *end = text + len;

    while (i < len) {
        const char *line = text + i;
        const char *nl = memchr(line, '\n', len - i);
        size_t ll = nl ? (size_t)(nl - line + 1) : (len - i);
        if (line_trim_eq(line, ll, ".method")) {
            r->methods_scanned++;
            size_t blen = method_block_len(line, end);
            char *block = strndup(line, blen);
            char *nb = NULL;
            if (block)
                nb = process_block(block, blen, o, c, r, file, stubbed);
            if (nb) {
                sk_buf_add(&out, nb, strlen(nb));
                free(nb);
                changed = 1;
            } else if (block) {
                sk_buf_add(&out, block, blen);
            }
            free(block);
            i += blen;
            continue;
        }
        sk_buf_add(&out, line, ll);
        i += ll;
    }

    char *res = sk_buf_take(&out);
    if (!res) return NULL;
    size_t res_len = strlen(res);
    int replaced = 0;
    if (o->do_const && !o->dry_run) {
        for (int k = 0; k < c->rep_count; k++) {
            if (!c->reps[k].newv) continue;
            int n = sk_replace_all(&res, &res_len, c->reps[k].oldv, c->reps[k].newv);
            if (n > 0) {
                replaced += n;
                r->consts_replaced += n;
                sk_detail(r, "const", file, "%s x%d", c->reps[k].kind, n);
            }
        }
    }
    if (replaced > 0) r->const_files++;
    if (!changed && replaced == 0) { free(res); return NULL; }
    return res;
}
