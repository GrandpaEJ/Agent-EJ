/* Runtime hijack: after every PackageManager lookup, overwrite the returned
 * PackageInfo.signatures with the ORIGINAL certificate, and wrap every
 * Signature[] read so legacy and modern (SigningInfo) checks agree. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SK_CLASS "Lcom/agent/ej/SigKiller;"

static int has(const char *s, const char *w) {
    return sk_has(s, w);
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* If `p` begins a "move-result-object vN" line, copy the register. */
static int move_result_reg(const char *p, const char *end, char *reg, size_t cap,
                           size_t *line_len) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
    char tmp[128];
    size_t n = l < sizeof(tmp) - 1 ? l : sizeof(tmp) - 1;
    memcpy(tmp, p, n);
    tmp[n] = '\0';
    const char *t = skip_ws(tmp);
    if (strncmp(t, "move-result-object", 18) != 0) return 0;
    t += 18;
    t = skip_ws(t);
    size_t i = 0;
    if (*t != 'v' && *t != 'p') return 0;
    while (t[i] && (t[i] == 'v' || t[i] == 'p' ||
                    (t[i] >= '0' && t[i] <= '9')) && i + 1 < cap) {
        reg[i] = t[i];
        i++;
    }
    if (i == 0) return 0;
    reg[i] = '\0';
    *line_len = l;
    return 1;
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

static int is_instrumented(const char *p, const char *end) {
    if (p >= end) return 0;
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(end - p);
    if (l >= 20) {
        const char *w = SK_CLASS;
        for (size_t i = 0; i + strlen(w) <= l; i++)
            if (memcmp(p + i, w, strlen(w)) == 0) return 1;
    }
    return 0;
}

char *sk_pkg_inject_block(const char *block, size_t len, sigkill_report *r,
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

        int is_pkg = has(t, "Landroid/content/pm/PackageManager;->getPackageInfo") ||
                     has(t, "Landroid/content/pm/PackageManager;->getPackageArchiveInfo");
        int is_list = has(t, "Landroid/content/pm/PackageManager;->getInstalledPackages") ||
                      has(t, "Landroid/content/pm/PackageManager;->getInstalledApplications");
        int is_sigarr = has(t, "->getApkContentsSigners") ||
                        has(t, "->getSigningCertificateHistory");
        int is_field = (strncmp(t, "iget-object", 11) == 0 ||
                        strncmp(t, "sget-object", 11) == 0) &&
                       has(t, "->signatures:[Landroid/content/pm/Signature;");

        sk_buf_add(&out, p, ll);

        if ((is_pkg || is_list || is_sigarr) && nl) {
            const char *noise = p + ll;
            const char *q = skip_noise(noise, end);
            char reg[16];
            size_t ql = 0;
            if (q < end && move_result_reg(q, end, reg, sizeof(reg), &ql) &&
                !is_instrumented(q + ql, end)) {
                /* keep the blank/comment lines the decoder emitted */
                sk_buf_add(&out, noise, (size_t)(q - noise));
                sk_buf_add(&out, q, ql);
                if (is_list) {
                    sk_buf_addf(&out, "    invoke-static {%s}, " SK_CLASS
                                      "->fixList(Ljava/util/List;)V\n", reg);
                    sk_detail(r, "pkg_hook", file, "fixList %s", reg);
                } else if (is_pkg) {
                    sk_buf_addf(&out, "    invoke-static {%s}, " SK_CLASS
                                      "->fix(Landroid/content/pm/PackageInfo;)V\n", reg);
                    sk_detail(r, "pkg_hook", file, "fix %s", reg);
                } else {
                    sk_buf_addf(&out, "    invoke-static {%s}, " SK_CLASS
                                      "->orig([Landroid/content/pm/Signature;)"
                                      "[Landroid/content/pm/Signature;\n"
                                      "    move-result-object %s\n", reg, reg);
                    sk_detail(r, "sig_hook", file, "orig %s", reg);
                }
                n++;
                p = q + ql;
                continue;
            }
        }

        if (is_field && !is_instrumented(p + ll, end)) {
            char mnem[24], dst[16];
            if (sscanf(t, "%23s %15s", mnem, dst) == 2) {
                char *comma = strchr(dst, ',');
                if (comma) *comma = '\0';
                if (dst[0] == 'v' || dst[0] == 'p') {
                    sk_buf_addf(&out, "    invoke-static {%s}, " SK_CLASS
                                      "->orig([Landroid/content/pm/Signature;)"
                                      "[Landroid/content/pm/Signature;\n"
                                      "    move-result-object %s\n", dst, dst);
                    sk_detail(r, "sig_field_hook", file, "orig %s", dst);
                    n++;
                }
            }
        }
        p += ll;
    }

    if (n == 0) { free(out.p); return NULL; }
    *sites += n;
    return sk_buf_take(&out);
}

char *sk_helper_smali(const sigkill_cert *c, size_t *len) {
    if (!c->sig_b64) return NULL;
    sk_buf b;
    sk_buf_init(&b);
    sk_buf_adds(&b, ".class public final Lcom/agent/ej/SigKiller;\n"
                    ".super Ljava/lang/Object;\n\n");

    sk_buf_addf(&b, ".method public static fix(Landroid/content/pm/PackageInfo;)V\n"
                    "    .locals 3\n"
                    "    if-nez p0, :done\n"
                    "    const-string v0, \"%s\"\n"
                    "    const/4 v1, 0x0\n"
                    "    invoke-static {v0, v1}, Landroid/util/Base64;->decode(Ljava/lang/String;I)[B\n"
                    "    move-result-object v0\n"
                    "    new-instance v1, Landroid/content/pm/Signature;\n"
                    "    invoke-direct {v1, v0}, Landroid/content/pm/Signature;-><init>([B)V\n"
                    "    const/4 v0, 0x1\n"
                    "    new-array v0, v0, [Landroid/content/pm/Signature;\n"
                    "    const/4 v2, 0x0\n"
                    "    aput-object v1, v0, v2\n"
                    "    iput-object v0, p0, Landroid/content/pm/PackageInfo;->signatures:[Landroid/content/pm/Signature;\n"
                    "    :done\n"
                    "    return-void\n"
                    ".end method\n\n", c->sig_b64);

    sk_buf_addf(&b, ".method public static orig([Landroid/content/pm/Signature;)[Landroid/content/pm/Signature;\n"
                    "    .locals 3\n"
                    "    const-string v0, \"%s\"\n"
                    "    const/4 v1, 0x0\n"
                    "    invoke-static {v0, v1}, Landroid/util/Base64;->decode(Ljava/lang/String;I)[B\n"
                    "    move-result-object v0\n"
                    "    new-instance v1, Landroid/content/pm/Signature;\n"
                    "    invoke-direct {v1, v0}, Landroid/content/pm/Signature;-><init>([B)V\n"
                    "    const/4 v0, 0x1\n"
                    "    new-array v0, v0, [Landroid/content/pm/Signature;\n"
                    "    const/4 v2, 0x0\n"
                    "    aput-object v1, v0, v2\n"
                    "    return-object v0\n"
                    ".end method\n\n", c->sig_b64);

    sk_buf_adds(&b, ".method public static fixList(Ljava/util/List;)V\n"
                    "    .locals 3\n"
                    "    if-nez p0, :done\n"
                    "    const/4 v0, 0x0\n"
                    "    :loop\n"
                    "    invoke-interface {p0}, Ljava/util/List;->size()I\n"
                    "    move-result v1\n"
                    "    if-ge v0, v1, :done\n"
                    "    invoke-interface {p0, v0}, Ljava/util/List;->get(I)Ljava/lang/Object;\n"
                    "    move-result-object v1\n"
                    "    check-cast v1, Landroid/content/pm/PackageInfo;\n"
                    "    invoke-static {v1}, Lcom/agent/ej/SigKiller;->fix(Landroid/content/pm/PackageInfo;)V\n"
                    "    add-int/lit8 v0, v0, 0x1\n"
                    "    goto :loop\n"
                    "    :done\n"
                    "    return-void\n"
                    ".end method\n\n");

    /* Wrappers used by the `call` technique: same register list as the
     * original invoke-virtual (receiver + args) turns into invoke-static. */
    sk_buf_adds(&b, ".method public static getPackageInfo(Landroid/content/pm/PackageManager;Ljava/lang/String;I)Landroid/content/pm/PackageInfo;\n"
                    "    .locals 1\n"
                    "    invoke-virtual {p0, p1, p2}, Landroid/content/pm/PackageManager;->getPackageInfo(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;\n"
                    "    move-result-object v0\n"
                    "    invoke-static {v0}, Lcom/agent/ej/SigKiller;->fix(Landroid/content/pm/PackageInfo;)V\n"
                    "    return-object v0\n"
                    ".end method\n\n"
                    ".method public static getPackageArchiveInfo(Landroid/content/pm/PackageManager;Ljava/lang/String;I)Landroid/content/pm/PackageInfo;\n"
                    "    .locals 1\n"
                    "    invoke-virtual {p0, p1, p2}, Landroid/content/pm/PackageManager;->getPackageArchiveInfo(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;\n"
                    "    move-result-object v0\n"
                    "    invoke-static {v0}, Lcom/agent/ej/SigKiller;->fix(Landroid/content/pm/PackageInfo;)V\n"
                    "    return-object v0\n"
                    ".end method\n\n"
                    ".method public static getInstalledPackages(Landroid/content/pm/PackageManager;I)Ljava/util/List;\n"
                    "    .locals 1\n"
                    "    invoke-virtual {p0, p1}, Landroid/content/pm/PackageManager;->getInstalledPackages(I)Ljava/util/List;\n"
                    "    move-result-object v0\n"
                    "    invoke-static {v0}, Lcom/agent/ej/SigKiller;->fixList(Ljava/util/List;)V\n"
                    "    return-object v0\n"
                    ".end method\n\n");

    /* Original certificate bytes for `sigobj` (Signature.toByteArray). */
    sk_buf_addf(&b, ".method public static toBytes(Landroid/content/pm/Signature;)[B\n"
                    "    .locals 2\n"
                    "    const-string v0, \"%s\"\n"
                    "    const/4 v1, 0x0\n"
                    "    invoke-static {v0, v1}, Landroid/util/Base64;->decode(Ljava/lang/String;I)[B\n"
                    "    move-result-object v0\n"
                    "    return-object v0\n"
                    ".end method\n", c->sig_b64);

    if (len) *len = b.len;
    return sk_buf_take(&b);
}
