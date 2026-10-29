/* Signature-object and API-result overrides that run at the call site.
 *
 *  sigobj   - Signature.toCharsString()/hashCode()/equals() results are
 *             overwritten with the original certificate's values; toByteArray
 *             is retargeted to SigKiller.toBytes().
 *  checkapi - PackageManager.checkSignatures(...) is forced to 0
 *             (SIGNATURE_MATCH).
 */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *skip_noise(const char *p, const char *end) {
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
        const char *t = skip_ws(p);
        if (t < p + l && *t != '\n' && *t != '\r' && *t != '#') break;
        p += l;
    }
    return p;
}

/* Parse "move-result[-object] vN" at p; 1 on success. */
static int next_move(const char *p, const char *end, char *reg, size_t cap,
                     int object, size_t *line_len) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
    char tmp[128];
    size_t n = l < sizeof(tmp) - 1 ? l : sizeof(tmp) - 1;
    memcpy(tmp, p, n);
    tmp[n] = '\0';
    const char *t = skip_ws(tmp);
    const char *pat = object ? "move-result-object" : "move-result";
    size_t pl = strlen(pat);
    if (strncmp(t, pat, pl) != 0) return 0;
    if (!object && t[pl] != ' ' && t[pl] != '\t') return 0;
    t += pl;
    t = skip_ws(t);
    size_t i = 0;
    if (*t != 'v' && *t != 'p') return 0;
    while (t[i] && i + 1 < cap &&
           (t[i] == 'v' || t[i] == 'p' || (t[i] >= '0' && t[i] <= '9'))) {
        reg[i] = t[i];
        i++;
    }
    if (i == 0) return 0;
    reg[i] = '\0';
    *line_len = l;
    return 1;
}

static int next_line_is(const char *p, const char *end, const char *what) {
    p = skip_noise(p, end);
    if (p >= end) return 0;
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
    const char *t = skip_ws(p);
    size_t tl = l - (size_t)(t - p);
    size_t wl = strlen(what);
    while (tl && (t[tl - 1] == '\n' || t[tl - 1] == '\r' ||
                  t[tl - 1] == ' ' || t[tl - 1] == '\t')) tl--;
    return tl >= wl && memcmp(t, what, wl) == 0;
}

static const char *rep_value(const sigkill_cert *c, const char *kind) {
    for (int i = 0; i < c->rep_count; i++)
        if (strcmp(c->reps[i].kind, kind) == 0) return c->reps[i].oldv;
    return NULL;
}

static char *override_text(int chars, int hash, int eq, const sigkill_cert *c,
                           const char *reg) {
    char buf[64];
    if (chars) {
        const char *hex = rep_value(c, "cert_hex");
        if (!hex) return NULL;
        size_t n = strlen(hex) + strlen(reg) + 32;
        char *s = malloc(n);
        if (!s) return NULL;
        snprintf(s, n, "const-string %s, \"%s\"", reg, hex);
        return s;
    }
    if (hash) {
        const char *hc = rep_value(c, "hashcode");
        if (!hc) return NULL;
        long v = strtol(hc, NULL, 10);
        snprintf(buf, sizeof(buf), "const %s, 0x%08x", reg, (unsigned)v);
        return strdup(buf);
    }
    if (eq) {
        snprintf(buf, sizeof(buf), "const/4 %s, 0x1", reg);
        return strdup(buf);
    }
    return NULL;
}

char *sk_sigobj_inject_block(const char *block, size_t len, sigkill_cert *c,
                             sigkill_report *r, const char *file, int *sites) {
    sk_buf out;
    sk_buf_init(&out);
    const char *p = block, *end = block + len;
    int n = 0;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t ll = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
        char line[1024];
        size_t ln = ll < sizeof(line) - 1 ? ll : sizeof(line) - 1;
        memcpy(line, p, ln);
        line[ln] = '\0';
        const char *t = skip_ws(line);

        int is_invoke = strncmp(t, "invoke-", 7) == 0;
        int is_chars = is_invoke && sk_has(t, "Landroid/content/pm/Signature;->toCharsString()");
        int is_bytes = is_invoke && sk_has(t, "Landroid/content/pm/Signature;->toByteArray()[B");
        int is_hash = is_invoke && sk_has(t, "Landroid/content/pm/Signature;->hashCode()I");
        int is_eq = is_invoke && sk_has(t, "Landroid/content/pm/Signature;->equals(Ljava/lang/Object;)Z");

        if (is_bytes) {
            char *rw = sk_rewrite_invoke_static(
                p, ll, "Lcom/agent/ej/SigKiller;->toBytes(Landroid/content/pm/Signature;)[B");
            if (rw) {
                sk_buf_add(&out, rw, strlen(rw));
                free(rw);
                sk_detail(r, "sigobj", file, "toByteArray -> original bytes");
                n++;
                p += ll;
                continue;
            }
        }

        if (is_chars || is_hash || is_eq) {
            char reg[16];
            size_t ml = 0;
            const char *q = skip_noise(p + ll, end);
            if (q < end && next_move(q, end, reg, sizeof(reg), is_chars, &ml)) {
                char *ov = override_text(is_chars, is_hash, is_eq, c, reg);
                if (ov && !next_line_is(q + ml, end, ov)) {
                    sk_buf_add(&out, p, ll);
                    sk_buf_add(&out, p + ll, (size_t)(q - (p + ll)));
                    sk_buf_add(&out, q, ml);
                    sk_buf_adds(&out, "    ");
                    sk_buf_adds(&out, ov);
                    sk_buf_adds(&out, "\n");
                    sk_detail(r, "sigobj", file, "%s -> original",
                              is_chars ? "toCharsString" :
                              is_hash ? "hashCode" : "equals");
                    n++;
                    p = q + ml;
                    free(ov);
                    continue;
                }
                free(ov);
            }
        }

        sk_buf_add(&out, p, ll);
        p += ll;
    }

    if (n == 0) { free(out.p); return NULL; }
    *sites += n;
    return sk_buf_take(&out);
}

char *sk_checkapi_inject_block(const char *block, size_t len, sigkill_report *r,
                               const char *file, int *sites) {
    sk_buf out;
    sk_buf_init(&out);
    const char *p = block, *end = block + len;
    int n = 0;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t ll = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
        char line[1024];
        size_t ln = ll < sizeof(line) - 1 ? ll : sizeof(line) - 1;
        memcpy(line, p, ln);
        line[ln] = '\0';
        const char *t = skip_ws(line);

        if (strncmp(t, "invoke-", 7) == 0 &&
            sk_has(t, "Landroid/content/pm/PackageManager;->checkSignatures(")) {
            char reg[16];
            size_t ml = 0;
            const char *q = skip_noise(p + ll, end);
            if (q < end && next_move(q, end, reg, sizeof(reg), 0, &ml)) {
                char ov[64];
                snprintf(ov, sizeof(ov), "const/4 %s, 0x0", reg);
                if (!next_line_is(q + ml, end, ov)) {
                    sk_buf_add(&out, p, ll);
                    sk_buf_add(&out, p + ll, (size_t)(q - (p + ll)));
                    sk_buf_add(&out, q, ml);
                    sk_buf_addf(&out, "    %s\n", ov);
                    sk_detail(r, "checkapi", file, "checkSignatures -> MATCH");
                    n++;
                    p = q + ml;
                    continue;
                }
            }
        }

        sk_buf_add(&out, p, ll);
        p += ll;
    }

    if (n == 0) { free(out.p); return NULL; }
    *sites += n;
    return sk_buf_take(&out);
}
