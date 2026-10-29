/* Heuristic mode: no mapping file available. Obfuscated class and member names
 * are detected (names carrying non-printable / non-ASCII bytes, or ProGuard's
 * one-or-two-letter names) and given stable, readable replacements:
 *   classes -> deobf/Class0001, deobf/Class0002, ...   (globally, sorted)
 *   methods -> m001, m002 ...   fields -> f001, f002 ... (per class, sorted)
 * Sorting makes the assignment deterministic across runs. The result is for
 * analysis only; it is not meant to be rebuilt into a running APK. */
#include "formats.h"
#include "deobf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

typedef struct { char **v; size_t n, cap; } strv;

static void sv_push(strv *s, const char *str) {
    for (size_t i = 0; i < s->n; i++) if (strcmp(s->v[i], str) == 0) return;
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        char **nv = realloc(s->v, cap * sizeof(char *));
        if (!nv) return;
        s->v = nv; s->cap = cap;
    }
    s->v[s->n] = strdup(str);
    if (s->v[s->n]) s->n++;
}
static void sv_free(strv *s) { for (size_t i = 0; i < s->n; i++) free(s->v[i]); free(s->v); }
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int looks_obf(const char *s) {
    size_t len = 0; int nonascii = 0, alpha_only = 1;
    for (const char *p = s; *p; p++, len++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c >= 0x7f) nonascii = 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) alpha_only = 0;
    }
    if (!len) return 0;
    if (nonascii) return 1;                 /* invisible / unicode names */
    if (len <= 2 && alpha_only) return 1;   /* a, b, ab, ... */
    return 0;
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    *n = fread(b, 1, (size_t)sz, f); b[*n] = 0; fclose(f);
    return b;
}

/* Copies the class descriptor body from a ".class ... L...;" line into out. */
static int class_body(const char *line, char *out, size_t cap) {
    const char *L = strchr(line, 'L');
    if (!L) return -1;
    const char *semi = strchr(L, ';');
    if (!semi) return -1;
    size_t n = (size_t)(semi - L - 1);
    if (n >= cap) n = cap - 1;
    memcpy(out, L + 1, n); out[n] = 0;
    return 0;
}

static const char *last_seg(const char *body) {
    const char *slash = strrchr(body, '/');
    return slash ? slash + 1 : body;
}

/* Extracts a member name from a ".method"/".field" line: the token ending at
 * `end` (either '(' for methods or ':' for fields), or NULL. */
static char *member_name(const char *line, char end) {
    const char *e = strchr(line, end);
    if (!e) return NULL;
    const char *p = e;
    while (p > line && p[-1] != ' ' && p[-1] != '\t') p--;
    size_t n = (size_t)(e - p);
    if (!n) return NULL;
    char *o = malloc(n + 1);
    if (!o) return NULL;
    memcpy(o, p, n); o[n] = 0;
    return o;
}

/* Pass 1: collect obfuscated class bodies from every .class declaration. */
static void collect_classes(const char *dir, strv *classes) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e; char path[4096];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 ||
            (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        DIR *sub = opendir(path);
        if (sub) { closedir(sub); collect_classes(path, classes); continue; }
        size_t nl = strlen(e->d_name);
        if (nl < 7 || strcmp(e->d_name + nl - 6, ".smali") != 0) continue;
        size_t sz = 0; char *txt = slurp(path, &sz);
        if (!txt) continue;
        char *save; char *ln = strtok_r(txt, "\n", &save);
        while (ln) {
            if (strncmp(ln, ".class", 6) == 0) {
                char body[2048];
                if (class_body(ln, body, sizeof(body)) == 0 && looks_obf(last_seg(body)))
                    sv_push(classes, body);
                break;      /* one .class per file */
            }
            ln = strtok_r(NULL, "\n", &save);
        }
        free(txt);
    }
    closedir(d);
}

/* Pass 2: per file, collect & assign member renames for the file's class. */
static void collect_members(const char *dir, deobf_table *t) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e; char path[4096];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 ||
            (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        DIR *sub = opendir(path);
        if (sub) { closedir(sub); collect_members(path, t); continue; }
        size_t nl = strlen(e->d_name);
        if (nl < 7 || strcmp(e->d_name + nl - 6, ".smali") != 0) continue;
        size_t sz = 0; char *txt = slurp(path, &sz);
        if (!txt) continue;
        char body[2048] = {0};
        strv methods = {0}, fields = {0};
        char *save; char *ln = strtok_r(txt, "\n", &save);
        while (ln) {
            if (!body[0] && strncmp(ln, ".class", 6) == 0) class_body(ln, body, sizeof(body));
            else if (strncmp(ln, ".method", 7) == 0) {
                char *nm = member_name(ln, '(');
                if (nm) { if (strcmp(nm, "<init>") && strcmp(nm, "<clinit>") && looks_obf(nm)) sv_push(&methods, nm); free(nm); }
            } else if (strncmp(ln, ".field", 6) == 0) {
                char *nm = member_name(ln, ':');
                if (nm) { if (looks_obf(nm)) sv_push(&fields, nm); free(nm); }
            }
            ln = strtok_r(NULL, "\n", &save);
        }
        if (body[0]) {
            qsort(methods.v, methods.n, sizeof(char *), cmp_str);
            qsort(fields.v, fields.n, sizeof(char *), cmp_str);
            char nm[32];
            for (size_t i = 0; i < methods.n; i++) {
                snprintf(nm, sizeof(nm), "m%03zu", i + 1);
                deobf_add_member(t, body, methods.v[i], nm);
            }
            for (size_t i = 0; i < fields.n; i++) {
                snprintf(nm, sizeof(nm), "f%03zu", i + 1);
                deobf_add_member(t, body, fields.v[i], nm);
            }
        }
        sv_free(&methods); sv_free(&fields);
        free(txt);
    }
    closedir(d);
}

int deobf_heuristic_apply(const char *smali_root, char *report, size_t cap) {
    deobf_table *t = deobf_table_new();
    if (!t) return -1;

    strv classes = {0};
    collect_classes(smali_root, &classes);
    qsort(classes.v, classes.n, sizeof(char *), cmp_str);
    char newb[32];
    for (size_t i = 0; i < classes.n; i++) {
        snprintf(newb, sizeof(newb), "deobf/Class%04zu", i + 1);
        deobf_add_class(t, classes.v[i], newb);
    }
    size_t nclass = classes.n;
    sv_free(&classes);

    collect_members(smali_root, t);

    char rwrep[128] = {0};
    int changed = deobf_rewrite_tree(smali_root, t, rwrep, sizeof(rwrep));
    deobf_table_free(t);
    if (report && cap)
        snprintf(report, cap, "%zu obfuscated classes renamed; %s", nclass, rwrep);
    return changed;
}
