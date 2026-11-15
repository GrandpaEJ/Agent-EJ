/* Dex editor rename: rewrite class descriptors (or whole package prefixes)
 * in smali text, so a renamed class and every reference to it agree. Works on
 * the text the disassembler renders: types, member refs, signatures. */
#include <stdlib.h>
#include <string.h>

static int is_hit(const char *hit, size_t fl, int cls) {
    return !cls || hit[fl] == ';' || hit[fl] == '<';
}

/* One mapping over text: a fresh malloc'd string, NULL when out of memory. */
static char *rename_one(const char *text, const char *from, const char *to) {
    size_t fl = strlen(from), tl = strlen(to);
    int cls = fl > 1 && from[fl - 1] == ';';
    if (cls) fl--, tl -= (tl && to[tl - 1] == ';');     /* match the stem, keep the next char */
    char *stem = strndup(from, fl);
    if (!stem) return NULL;
    size_t n = 0;
    for (const char *p = text; (p = strstr(p, stem)) != NULL; p += fl) n += is_hit(p, fl, cls);
    char *out = (char *)malloc(strlen(text) + n * (tl > fl ? tl - fl : 0) + 1);
    if (out) {
        char *w = out;
        const char *p = text, *hit;
        while ((hit = strstr(p, stem)) != NULL) {
            size_t k = (size_t)(hit - p);
            memcpy(w, p, k), w += k;
            if (is_hit(hit, fl, cls)) memcpy(w, to, tl), w += tl;
            else memcpy(w, hit, fl), w += fl;
            p = hit + fl;
        }
        strcpy(w, p);
    }
    free(stem);
    return out;
}

char *smali_rename(const char *text, const char *const *from, const char *const *to, int n) {
    char *cur = text ? strdup(text) : NULL;
    for (int i = 0; cur && i < n; i++) {
        if (!from[i] || !to[i] || !from[i][0] || !strstr(cur, from[i])) continue;
        char *next = rename_one(cur, from[i], to[i]);
        free(cur);
        cur = next;
    }
    return cur;
}
