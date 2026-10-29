/* Method-body neutralisation: emit a passing implementation for a detected
 * signature check, preserving method annotations. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* --------------------------- stub generation ------------------------ */
static int stub_body(sk_buf *b, const char *ret, const char *name,
                     const sigkill_cert *c) {
    (void)name;
    if (strcmp(ret, "V") == 0) {
        sk_buf_adds(b, "    .locals 0\n    return-void\n");
        return 0;
    }
    if (strchr("ZIBSC", ret[0]) && ret[1] == '\0') {
        sk_buf_adds(b, "    .locals 1\n    const/4 v0, 0x1\n    return v0\n");
        return 0;
    }
    if (strcmp(ret, "J") == 0) {
        sk_buf_adds(b, "    .locals 2\n    const-wide/16 v0, 0x1\n    return-wide v0\n");
        return 0;
    }
    if (strcmp(ret, "F") == 0) {
        sk_buf_adds(b, "    .locals 1\n    const/high16 v0, 0x3f800000\n    return v0\n");
        return 0;
    }
    if (strcmp(ret, "D") == 0) {
        sk_buf_adds(b, "    .locals 2\n    const-wide v0, 0x3ff0000000000000\n    return-wide v0\n");
        return 0;
    }
    if (strcmp(ret, "Ljava/lang/String;") == 0) {
        if (!c->sig_b64 || !c->reps[0].oldv) return -1;
        /* toCharsString()-style value: the original certificate as hex. */
        for (int i = 0; i < c->rep_count; i++) {
            if (strcmp(c->reps[i].kind, "cert_hex") == 0) {
                sk_buf_addf(b, "    .locals 1\n    const-string v0, \"%s\"\n"
                               "    return-object v0\n", c->reps[i].oldv);
                return 0;
            }
        }
        return -1;
    }
    if (!c->sig_b64) return -1;
    if (strcmp(ret, "Landroid/content/pm/Signature;") == 0 ||
        strcmp(ret, "[B") == 0) {
        sk_buf_addf(b, "    .locals 2\n    const-string v0, \"%s\"\n"
                       "    const/4 v1, 0x0\n"
                       "    invoke-static {v0, v1}, Landroid/util/Base64;->decode(Ljava/lang/String;I)[B\n"
                       "    move-result-object v0\n", c->sig_b64);
        if (ret[0] == '[') sk_buf_adds(b, "    return-object v0\n");
        else sk_buf_adds(b, "    new-instance v1, Landroid/content/pm/Signature;\n"
                            "    invoke-direct {v1, v0}, Landroid/content/pm/Signature;-><init>([B)V\n"
                            "    return-object v1\n");
        return 0;
    }
    if (strcmp(ret, "[Landroid/content/pm/Signature;") == 0) {
        sk_buf_addf(b, "    .locals 3\n    const-string v0, \"%s\"\n"
                       "    const/4 v1, 0x0\n"
                       "    invoke-static {v0, v1}, Landroid/util/Base64;->decode(Ljava/lang/String;I)[B\n"
                       "    move-result-object v0\n"
                       "    new-instance v1, Landroid/content/pm/Signature;\n"
                       "    invoke-direct {v1, v0}, Landroid/content/pm/Signature;-><init>([B)V\n"
                       "    const/4 v0, 0x1\n"
                       "    new-array v0, v0, [Landroid/content/pm/Signature;\n"
                       "    const/4 v2, 0x0\n"
                       "    aput-object v1, v0, v2\n"
                       "    return-object v0\n", c->sig_b64);
        return 0;
    }
    return -1;
}

/* --------------------------- block processing ----------------------- */
char *sk_stub_block(const char *block, size_t len, const char *file,
                        const char *name, const char *ret,
                        sigkill_cert *c, sigkill_report *r) {
    sk_buf b;
    sk_buf_init(&b);
    const char *nl = memchr(block, '\n', len);
    size_t hl = nl ? (size_t)(nl - block + 1) : len;
    sk_buf_add(&b, block, hl);

    /* Preserve annotations attached to the method itself. */
    const char *p = block + hl;
    const char *end = block + len;
    while (p < end) {
        const char *l = memchr(p, '\n', (size_t)(end - p));
        size_t ll = l ? (size_t)(l - p + 1) : (size_t)(end - p);
        if (!line_trim_eq(p, ll, ".annotation")) break;
        const char *q = p;
        while (q < end) {
            const char *qnl = memchr(q, '\n', (size_t)(end - q));
            size_t ql = qnl ? (size_t)(qnl - q + 1) : (size_t)(end - q);
            sk_buf_add(&b, q, ql);
            q += ql;
            if (line_trim_eq(q - ql, ql, ".end annotation")) break;
        }
        p = q;
    }

    sk_buf body;
    sk_buf_init(&body);
    if (stub_body(&body, ret, name, c) != 0) {
        free(b.p); free(body.p);
        return NULL;
    }
    sk_buf_add(&b, body.p, body.len);
    sk_buf_adds(&b, ".end method\n");
    free(body.p);

    sk_detail(r, "stub", file, "%s()%s -> pass", name, ret);
    return sk_buf_take(&b);
}
