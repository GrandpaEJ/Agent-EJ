/* Map mode: reads an R8 / ProGuard mapping.txt and restores original names
 * across a decoded smali tree.
 *
 * mapping.txt lines:
 *   com.example.Foo -> a.b.c:            (class: original -> obfuscated)
 *       int field            -> a        (field member)
 *       1:9:void method(int) -> b        (method member, optional line range)
 * smali is produced from the obfuscated dex, so the obfuscated name is the
 * lookup key and the original is the replacement. Dotted class names are
 * converted to the slash form smali uses. */
#include "formats.h"
#include "deobf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Converts "a.b.c" to "a/b/c" in place within a fresh malloc'd copy. */
static char *dots_to_slashes(const char *s, size_t len) {
    char *o = malloc(len + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < len; i++) o[i] = (s[i] == '.') ? '/' : s[i];
    o[len] = 0;
    return o;
}

/* Trims leading/trailing ASCII whitespace, returning start and *len. */
static const char *trim(const char *s, size_t *len) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    *len = n;
    return s;
}

/* The original member name is the token just before '(' (method) or the last
 * whitespace-separated token (field), taken from the left side of " -> ". */
static char *orig_member_name(const char *left, size_t len) {
    const char *paren = memchr(left, '(', len);
    size_t upto = paren ? (size_t)(paren - left) : len;
    size_t start = upto;
    while (start > 0 && left[start - 1] != ' ' && left[start - 1] != '\t') start--;
    size_t nlen = upto - start;
    char *o = malloc(nlen + 1);
    if (!o) return NULL;
    memcpy(o, left + start, nlen); o[nlen] = 0;
    return o;
}

int deobf_map_apply(const char *smali_root, const char *mapping_path,
                    char *report, size_t cap) {
    FILE *f = fopen(mapping_path, "rb");
    if (!f) { if (report && cap) snprintf(report, cap, "cannot open mapping file"); return -1; }
    deobf_table *t = deobf_table_new();
    if (!t) { fclose(f); return -1; }

    char line[8192];
    char cur_obf[2048] = {0};     /* current class obfuscated slash body */
    size_t nclass = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        int indented = (line[0] == ' ' || line[0] == '\t');
        size_t len;
        const char *s = trim(line, &len);
        if (!len) continue;
        const char *arrow = strstr(s, " -> ");
        if (!arrow) continue;
        size_t lhs_len = (size_t)(arrow - s);
        const char *rhs = arrow + 4;
        size_t rhs_len = len - lhs_len - 4;

        if (!indented) {
            /* Class line; rhs ends with ':' */
            if (rhs_len && rhs[rhs_len - 1] == ':') rhs_len--;
            char *obf = dots_to_slashes(rhs, rhs_len);
            char *orig = dots_to_slashes(s, lhs_len);
            if (obf && orig) {
                deobf_add_class(t, obf, orig);
                snprintf(cur_obf, sizeof(cur_obf), "%s", obf);
                nclass++;
            }
            free(obf); free(orig);
        } else if (cur_obf[0]) {
            /* Member line within the current class. */
            char *obf_name = malloc(rhs_len + 1);
            if (obf_name) {
                memcpy(obf_name, rhs, rhs_len); obf_name[rhs_len] = 0;
                char *orig_name = orig_member_name(s, lhs_len);
                if (orig_name && strcmp(obf_name, orig_name) != 0)
                    deobf_add_member(t, cur_obf, obf_name, orig_name);
                free(orig_name);
            }
            free(obf_name);
        }
    }
    fclose(f);

    char rwrep[128] = {0};
    int changed = deobf_rewrite_tree(smali_root, t, rwrep, sizeof(rwrep));
    deobf_table_free(t);
    if (report && cap)
        snprintf(report, cap, "%zu classes mapped; %s", nclass, rwrep);
    return changed;
}
