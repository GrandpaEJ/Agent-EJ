#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
void sk_buf_init(sk_buf *b) {
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static int sk_buf_reserve(sk_buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    char *p = realloc(b->p, cap);
    if (!p) return -1;
    b->p = p;
    b->cap = cap;
    return 0;
}

int sk_buf_add(sk_buf *b, const char *s, size_t n) {
    if (sk_buf_reserve(b, n) != 0) return -1;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 0;
}

int sk_buf_adds(sk_buf *b, const char *s) {
    return sk_buf_add(b, s, strlen(s));
}

int sk_buf_addf(sk_buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n < sizeof(tmp)) return sk_buf_add(b, tmp, (size_t)n);
    char *big = malloc((size_t)n + 1);
    if (!big) return -1;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int rc = sk_buf_add(b, big, (size_t)n);
    free(big);
    return rc;
}

char *sk_buf_take(sk_buf *b) {
    char *p = b->p;
    b->p = NULL;
    b->len = b->cap = 0;
    return p;
}

/* ------------------------------------------------------------------ */
int sk_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

char *sk_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len) *len = got;
    return buf;
}

int sk_write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, len, f);
    int rc = (n == len && fclose(f) == 0) ? 0 : -1;
    return rc;
}

int sk_walk(const char *root, int (*fn)(const char *path, void *ud), void *ud) {
    DIR *d = opendir(root);
    if (!d) return -1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(d)) != NULL && rc == 0) {
        if (ent->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) rc = sk_walk(path, fn, ud);
        else if (S_ISREG(st.st_mode)) rc = fn(path, ud);
    }
    closedir(d);
    return rc;
}

int sk_mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(tmp, 0755);
        *p = '/';
    }
    return mkdir(tmp, 0755);
}

/* ------------------------------------------------------------------ */
static void sk_json_escape(sk_buf *b, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  sk_buf_adds(b, "\\\""); break;
        case '\\': sk_buf_adds(b, "\\\\"); break;
        case '\n': sk_buf_adds(b, "\\n"); break;
        case '\r': sk_buf_adds(b, "\\r"); break;
        case '\t': sk_buf_adds(b, "\\t"); break;
        default:
            if (*p < 0x20) sk_buf_addf(b, "\\u%04x", *p);
            else sk_buf_add(b, (const char *)p, 1);
        }
    }
}

void sk_detail(sigkill_report *r, const char *type, const char *file,
               const char *fmt, ...) {
    if (!r) return;
    if (r->details_len > 256 * 1024) { r->findings_truncated = 1; return; }
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    sk_buf b;
    sk_buf_init(&b);
    if (r->details_len == 0) sk_buf_adds(&b, "[");
    else sk_buf_adds(&b, ",");
    sk_buf_adds(&b, "{\"type\":\"");
    sk_json_escape(&b, type);
    sk_buf_adds(&b, "\",\"file\":\"");
    sk_json_escape(&b, file ? file : "");
    sk_buf_adds(&b, "\",\"msg\":\"");
    sk_json_escape(&b, msg);
    sk_buf_adds(&b, "\"}");
    sk_buf_add(&b, "", 0);

    size_t n = b.len;
    char *grown = realloc(r->details, r->details_len + n + 1);
    if (grown) {
        r->details = grown;
        memcpy(r->details + r->details_len, b.p, n);
        r->details_len += n;
        r->details[r->details_len] = '\0';
    } else {
        r->findings_truncated = 1;
    }
    free(b.p);
}

/* ------------------------------------------------------------------ */
int sk_replace_all(char **text, size_t *len, const char *oldv, const char *newv) {
    if (!text || !*text || !oldv || !*oldv || !newv) return 0;
    size_t ol = strlen(oldv), nl = strlen(newv);
    if (ol == nl && strcmp(oldv, newv) == 0) return 0;

    size_t count = 0;
    for (const char *p = *text; (p = strstr(p, oldv)) != NULL; p += ol) count++;
    if (count == 0) return 0;

    size_t new_len = *len + count * (nl - ol);
    char *out = malloc(new_len + 1);
    if (!out) return 0;

    const char *src = *text;
    char *dst = out;
    const char *hit;
    while ((hit = strstr(src, oldv)) != NULL) {
        size_t pre = (size_t)(hit - src);
        memcpy(dst, src, pre);
        dst += pre;
        memcpy(dst, newv, nl);
        dst += nl;
        src = hit + ol;
    }
    size_t tail = (size_t)((*text + *len) - src);
    memcpy(dst, src, tail);
    dst += tail;
    *dst = '\0';

    free(*text);
    *text = out;
    *len = new_len;
    return (int)count;
}

int sk_has(const char *s, const char *needle) {
    return s && needle && strstr(s, needle) != NULL;
}

int sk_icontains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}
