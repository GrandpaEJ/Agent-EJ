/* `call` technique: replace PackageManager lookups with a static wrapper that
 * returns an already-fixed PackageInfo (or fixed PackageInfo list).  Unlike
 * the post-call `pkg` hook this intercepts the invocation itself, so callers
 * that inspect the object identity or hand it straight to another method still
 * see the original signature. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

struct call_rule {
    const char *match;
    const char *sig;
    const char *label;
};

static const struct call_rule RULES[] = {
    { "Landroid/content/pm/PackageManager;->getPackageInfo(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
      "Lcom/agent/ej/SigKiller;->getPackageInfo(Landroid/content/pm/PackageManager;"
      "Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
      "getPackageInfo" },
    { "Landroid/content/pm/PackageManager;->getPackageArchiveInfo(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
      "Lcom/agent/ej/SigKiller;->getPackageArchiveInfo(Landroid/content/pm/PackageManager;"
      "Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
      "getPackageArchiveInfo" },
    { "Landroid/content/pm/PackageManager;->getInstalledPackages(I)Ljava/util/List;",
      "Lcom/agent/ej/SigKiller;->getInstalledPackages(Landroid/content/pm/PackageManager;"
      "I)Ljava/util/List;",
      "getInstalledPackages" },
};

char *sk_call_rewrite_block(const char *block, size_t len, sigkill_report *r,
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

        int handled = 0;
        if (strncmp(t, "invoke-virtual", 14) == 0) {
            for (size_t i = 0; i < sizeof(RULES) / sizeof(RULES[0]); i++) {
                if (!sk_has(t, RULES[i].match)) continue;
                char *rw = sk_rewrite_invoke_static(p, ll, RULES[i].sig);
                if (!rw) continue;
                sk_buf_add(&out, rw, strlen(rw));
                free(rw);
                sk_detail(r, "call", file, "%s -> wrapper", RULES[i].label);
                n++;
                handled = 1;
                break;
            }
        }
        if (!handled) sk_buf_add(&out, p, ll);
        p += ll;
    }

    if (n == 0) { free(out.p); return NULL; }
    *sites += n;
    return sk_buf_take(&out);
}
