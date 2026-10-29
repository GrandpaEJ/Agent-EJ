/* Applies a rename table to a decoded smali tree. Each file is processed line
 * by line so that both member *references* (L...;->name( / :) and member
 * *declarations* (.field / .method, whose owner is the file's own class) are
 * renamed consistently. Within a line, type descriptors (L...;) are replaced
 * from the table; quoted string literals are skipped so constants are never
 * touched, and a descriptor-opening 'L' is only honoured at a token boundary
 * so identifiers that merely contain an 'L' are left alone. */
#include "formats.h"
#include "deobf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

typedef struct { char *p; size_t n, cap; } sb;

static int sb_reserve(sb *s, size_t extra) {
    if (s->n + extra + 1 <= s->cap) return 0;
    size_t cap = s->cap ? s->cap * 2 : 4096;
    while (cap < s->n + extra + 1) cap *= 2;
    char *np = realloc(s->p, cap);
    if (!np) return -1;
    s->p = np; s->cap = cap;
    return 0;
}
static void sb_putc(sb *s, char c) { if (!sb_reserve(s, 1)) s->p[s->n++] = c; }
static void sb_put(sb *s, const char *b, size_t n) {
    if (!sb_reserve(s, n)) { memcpy(s->p + s->n, b, n); s->n += n; }
}
static void sb_puts(sb *s, const char *b) { sb_put(s, b, strlen(b)); }

static int is_ident(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '$';
}

/* Scans one line (no trailing newline), substituting descriptors and the
 * member names that follow them. Appends to `out`; sets *changed. */
static void scan_line(const char *in, size_t len, deobf_table *t, sb *out, int *changed) {
    int in_str = 0;
    for (size_t i = 0; i < len; ) {
        char c = in[i];
        if (in_str) {
            sb_putc(out, c);
            if (c == '\\' && i + 1 < len) { sb_putc(out, in[i + 1]); i += 2; continue; }
            if (c == '"') in_str = 0;
            i++; continue;
        }
        if (c == '"') { in_str = 1; sb_putc(out, c); i++; continue; }

        int boundary = (i == 0) || !is_ident((unsigned char)in[i - 1]);
        if (c == 'L' && boundary) {
            size_t k = i + 1;
            while (k < len && in[k] != ';' && in[k] != ' ' && in[k] != '\t') k++;
            if (k < len && in[k] == ';') {
                size_t blen = k - (i + 1);
                char *body = malloc(blen + 1);
                if (!body) { sb_putc(out, 'L'); i++; continue; }  /* OOM: 'L' literal, reparse */
                memcpy(body, in + i + 1, blen); body[blen] = 0;
                deobf_class *cl = deobf_find_class(t, body);
                sb_putc(out, 'L');
                if (cl && cl->rep) { sb_puts(out, cl->rep); *changed = 1; }
                else sb_put(out, body, blen);
                sb_putc(out, ';');
                i = k + 1;
                if (i + 1 < len && in[i] == '-' && in[i + 1] == '>') {
                    sb_put(out, "->", 2); i += 2;
                    size_t m = i;
                    while (m < len && in[m] != '(' && in[m] != ':' && in[m] != ' ' && in[m] != '\t') m++;
                    size_t mlen = m - i;
                    char *name = malloc(mlen + 1);
                    if (name) {
                        memcpy(name, in + i, mlen); name[mlen] = 0;
                        const char *rep = deobf_find_member(t, body, name);
                        if (rep && rep[0]) { sb_puts(out, rep); *changed = 1; }
                        else sb_put(out, name, mlen);
                        free(name);
                    } else {
                        sb_put(out, in + i, mlen);      /* OOM: keep original name */
                    }
                    i = m;
                }
                free(body);
                continue;
            }
        }
        sb_putc(out, c);
        i++;
    }
}

/* Copies the class descriptor body from a ".class ... L...;" line into out. */
static int line_class_body(const char *line, size_t len, char *out, size_t cap) {
    for (size_t i = 0; i + 1 < len; i++)
        if (line[i] == 'L') {
            size_t k = i + 1;
            while (k < len && line[k] != ';' && line[k] != ' ') k++;
            if (k < len && line[k] == ';') {
                size_t n = k - (i + 1);
                if (n >= cap) n = cap - 1;
                memcpy(out, line + i + 1, n); out[n] = 0;
                return 0;
            }
        }
    return -1;
}

/* For a ".field"/".method" declaration line, renames the declared member name
 * (owner = the file's class) by writing the adjusted line into `out`; returns 1
 * if it handled the line. `end` is ':' for fields, '(' for methods. */
static int decl_rename(const char *line, size_t len, const char *owner,
                       char end, deobf_table *t, sb *out, int *changed) {
    const char *e = memchr(line, end, len);
    if (!e) return 0;
    const char *p = e;
    while (p > line && p[-1] != ' ' && p[-1] != '\t') p--;
    size_t nlen = (size_t)(e - p);
    if (!nlen) return 0;
    char *name = malloc(nlen + 1);
    if (!name) return 0;
    memcpy(name, p, nlen); name[nlen] = 0;
    const char *rep = owner[0] ? deobf_find_member(t, owner, name) : NULL;
    if (rep && rep[0]) {
        sb_put(out, line, (size_t)(p - line));     /* up to the name */
        sb_puts(out, rep);                          /* renamed */
        *changed = 1;
        /* the remainder (type/signature) may hold descriptors: scan it */
        scan_line(e, len - (size_t)(e - line), t, out, changed);
        free(name);
        return 1;
    }
    free(name);
    return 0;
}

static char *rewrite_text(const char *in, size_t len, deobf_table *t) {
    sb out = {0};
    int changed = 0;
    char owner[2048] = {0};
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && in[j] != '\n') j++;           /* [i,j) = line */
        size_t llen = j - i;
        const char *line = in + i;

        const char *tl = line; size_t tn = llen;
        while (tn && (*tl == ' ' || *tl == '\t')) { tl++; tn--; }

        if (tn >= 6 && strncmp(tl, ".class", 6) == 0)
            line_class_body(line, llen, owner, sizeof(owner));

        int handled = 0;
        if (tn >= 7 && strncmp(tl, ".method", 7) == 0)
            handled = decl_rename(line, llen, owner, '(', t, &out, &changed);
        else if (tn >= 6 && strncmp(tl, ".field", 6) == 0)
            handled = decl_rename(line, llen, owner, ':', t, &out, &changed);
        if (!handled)
            scan_line(line, llen, t, &out, &changed);

        if (j < len) sb_putc(&out, '\n');
        i = j + 1;
    }
    if (!changed) { free(out.p); return NULL; }
    sb_putc(&out, '\0'); out.n--;
    return out.p;
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    *n = fread(b, 1, (size_t)sz, f); b[*n] = 0;
    fclose(f);
    return b;
}

/* Recursively rewrites every *.smali below dir; returns files changed. */
static int walk(const char *dir, deobf_table *t) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int changed = 0;
    struct dirent *e;
    char path[4096];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        DIR *sub = opendir(path);
        if (sub) { closedir(sub); changed += walk(path, t); continue; }
        size_t nl = strlen(e->d_name);
        if (nl < 7 || strcmp(e->d_name + nl - 6, ".smali") != 0) continue;
        size_t sz = 0;
        char *txt = slurp(path, &sz);
        if (!txt) continue;
        char *nw = rewrite_text(txt, sz, t);
        free(txt);
        if (nw) {
            FILE *w = fopen(path, "wb");
            if (w) { fwrite(nw, 1, strlen(nw), w); fclose(w); changed++; }
            free(nw);
        }
    }
    closedir(d);
    return changed;
}

int deobf_rewrite_tree(const char *root, deobf_table *t, char *report, size_t cap) {
    if (!root || !t) return -1;
    int changed = walk(root, t);
    if (report && cap)
        snprintf(report, cap, "%d smali file(s) rewritten", changed);
    return changed;
}
