/* DEX search for the dex editor (MT "Search" tab): class / field / method
 * names, string constants, integer constants and free smali text.
 *
 * A cheap pass over the parsed tables (names, raw code units) picks the
 * candidate classes; only those are rendered to smali, and the matching
 * smali lines are reported with their 0-based line numbers. Only a plain
 * "smali" search has no prefilter and renders every class in scope. */
#include "dex_find.h"
#include "format_dex_internal.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *dex_to_smali_class(dex_ctx *ctx, uint32_t class_idx);

typedef struct {
    const char *q;
    int flags;
    regex_t re;
    int has_re;
    long long num;      /* DEX_FIND_INTEGER target */
} matcher;

static const char *ci_strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    for (; *h; h++)
        if (strncasecmp(h, n, nl) == 0) return h;
    return NULL;
}

/* Does [s, s+len) contain the query? */
static int text_hit(matcher *m, const char *s, size_t len) {
    char buf[1024];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    if (m->has_re) return regexec(&m->re, buf, 0, NULL, 0) == 0;
    return (m->flags & DEX_FIND_CASE) ? strstr(buf, m->q) != NULL : ci_strstr(buf, m->q) != NULL;
}

static const char *pool_str(dex_ctx *c, uint32_t idx) {
    return (idx < (uint32_t)c->sp.count && c->sp.strings[idx]) ? c->sp.strings[idx] : "";
}

static const char *type_desc(dex_ctx *c, uint32_t type_idx) {
    if (type_idx >= c->type_count || c->type_ids[type_idx] < 0) return "";
    return pool_str(c, (uint32_t)c->type_ids[type_idx]);
}

/* "Lcom/a/B;" lies under package scope "com/a" (exact package only unless deep). */
static int in_scope(const char *desc, const char *scope, int deep) {
    if (!scope || !scope[0]) return 1;
    size_t sl = strlen(scope);
    if (desc[0] != 'L' || strncmp(desc + 1, scope, sl) != 0 || desc[1 + sl] != '/') return 0;
    return deep || strchr(desc + 2 + sl, '/') == NULL;
}

/* Raw code-unit scan for const-string / integer constants, without decoding
 * the instruction stream: a stray match in payload data only costs a render. */
static int code_has(dex_ctx *c, const dex_method_enc *me, int kind, const uint8_t *strset, matcher *m) {
    if (!me->code_off || (size_t)me->code_off + 16 > c->size) return 0;
    const uint8_t *ci = c->data + me->code_off;
    uint32_t n = (uint32_t)ci[12] | (uint32_t)ci[13] << 8 | (uint32_t)ci[14] << 16 | (uint32_t)ci[15] << 24;
    if ((size_t)me->code_off + 16 + (size_t)n * 2 > c->size) return 0;
    const uint8_t *u = ci + 16;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t op = u[i * 2];
        uint32_t w1 = i + 1 < n ? (uint32_t)u[i * 2 + 2] | (uint32_t)u[i * 2 + 3] << 8 : 0;
        uint32_t w2 = i + 2 < n ? (uint32_t)u[i * 2 + 4] | (uint32_t)u[i * 2 + 5] << 8 : 0;
        if (kind == DEX_FIND_STRING) {
            uint32_t s = op == 0x1a ? w1 : op == 0x1b ? (w1 | w2 << 16) : UINT32_MAX;
            if (s < c->string_count && strset[s]) return 1;
        } else {
            long long v;
            switch (op) {
            case 0x12: v = (int8_t)(u[i * 2 + 1] & 0xF0) >> 4; break;           /* const/4 */
            case 0x13: case 0x16: v = (int16_t)w1; break;                       /* const/16 */
            case 0x14: case 0x17: v = (int32_t)(w1 | w2 << 16); break;          /* const */
            case 0x15: v = (int32_t)(w1 << 16); break;                          /* const/high16 */
            case 0x19: v = (long long)((uint64_t)w1 << 48); break;              /* wide/high16 */
            default: continue;
            }
            if (v == m->num) return 1;
        }
    }
    return 0;
}

static int class_candidate(dex_ctx *c, dex_class *k, int kind, const uint8_t *strset, matcher *m) {
    if (kind == DEX_FIND_SMALI) return 1;
    if (kind == DEX_FIND_FIELD) {
        for (int i = 0; i < k->static_count + k->instance_count; i++) {
            dex_field_enc *f = i < k->static_count ? &k->static_fields[i] : &k->instance_fields[i - k->static_count];
            const char *n = f->field_idx < c->field_count ? pool_str(c, c->fields[f->field_idx].name_idx) : "";
            if (text_hit(m, n, strlen(n))) return 1;
        }
        return 0;
    }
    for (int i = 0; i < k->direct_count + k->virtual_count; i++) {
        dex_method_enc *me = i < k->direct_count ? &k->direct[i] : &k->virtual[i - k->direct_count];
        if (kind == DEX_FIND_METHOD) {
            const char *n = me->method_idx < c->method_count ? pool_str(c, c->methods[me->method_idx].name_idx) : "";
            if (text_hit(m, n, strlen(n))) return 1;
        } else if (code_has(c, me, kind, strset, m)) return 1;
    }
    /* strings also live in annotation values and static field initialisers
     * (Kotlin metadata d2 = {...}); those classes are rendered and checked */
    return kind == DEX_FIND_STRING && (k->annotations_off || k->static_values_off);
}

/* Integer constant of a smali line such as "const v0, 0x7f0b0012" (else 0). */
static int line_int(const char *l, size_t len, long long want) {
    while (len && isspace((unsigned char)*l)) l++, len--;
    if (len < 6 || strncmp(l, "const", 5) != 0 || (len > 12 && strncmp(l, "const-string", 12) == 0)) return 0;
    const char *comma = memchr(l, ',', len);
    if (!comma) return 0;
    char *end;
    long long v = strtoll(comma + 1, &end, 0);
    return end != comma + 1 && v == want;
}

static int line_hit(matcher *m, int kind, const char *l, size_t len) {
    const char *t = l;
    size_t tl = len;
    while (tl && isspace((unsigned char)*t)) t++, tl--;
    switch (kind) {
    case DEX_FIND_FIELD:
    case DEX_FIND_METHOD: {
        const char *dir = kind == DEX_FIND_FIELD ? ".field " : ".method ";
        size_t dl = strlen(dir);
        if (tl < dl || strncmp(t, dir, dl) != 0) return 0;
        const char *end = t + tl, *sep = memchr(t, kind == DEX_FIND_FIELD ? ':' : '(', tl);
        if (!sep) return 0;
        const char *nm = sep;
        while (nm > t && nm[-1] != ' ') nm--;
        (void)end;
        return text_hit(m, nm, (size_t)(sep - nm));
    }
    case DEX_FIND_STRING: {
        /* any "..." literal on the line: const-string, annotation value, field init;
         * comments (# ...) are skipped so baksmali's type hints don't match */
        const char *e = t + tl;
        for (const char *p = t; p < e; p++) {
            if (*p == '#') return 0;
            if (*p != '"') continue;
            const char *s = ++p;
            while (p < e && *p != '"') p += (*p == '\\' && p + 1 < e) ? 2 : 1;
            if (text_hit(m, s, (size_t)(p - s))) return 1;
        }
        return 0;
    }
    case DEX_FIND_INTEGER:
        return line_int(l, len, m->num);
    default:
        return text_hit(m, l, len);
    }
}

int dex_find(dex_ctx *c, int kind, const char *query, int flags, const char *scope,
             dex_find_fn fn, void *user) {
    if (!c || !query || !query[0] || !fn) return -1;
    matcher m = { .q = query, .flags = flags };
    if (kind == DEX_FIND_INTEGER) {
        char *end;
        m.num = strtoll(query, &end, 0);
        if (*end) return -1;
    } else if (flags & DEX_FIND_REGEX) {
        int rf = REG_EXTENDED | REG_NOSUB | ((flags & DEX_FIND_CASE) ? 0 : REG_ICASE);
        if (regcomp(&m.re, query, rf) != 0) return -1;
        m.has_re = 1;
    }
    uint8_t *strset = NULL;
    if (kind == DEX_FIND_STRING) {
        strset = calloc(c->string_count ? c->string_count : 1, 1);
        if (!strset) goto out;
        int any = 0;
        for (uint32_t i = 0; i < c->string_count; i++) {
            const char *s = pool_str(c, i);
            if (text_hit(&m, s, strlen(s))) strset[i] = 1, any = 1;
        }
        if (!any) goto out;
    }
    int deep = (flags & DEX_FIND_SHALLOW) == 0;
    for (uint32_t ci = 0; ci < c->class_count; ci++) {
        dex_class *k = &c->classes[ci];
        const char *desc = type_desc(c, k->class_idx);
        if (!in_scope(desc, scope, deep)) continue;
        if (kind == DEX_FIND_CLASS) {
            if (text_hit(&m, desc, strlen(desc)) && fn(user, ci, 0, desc)) break;
            continue;
        }
        if (!class_candidate(c, k, kind, strset, &m)) continue;
        char *smali = dex_to_smali_class(c, ci);
        if (!smali) continue;
        int stop = 0;
        uint32_t line = 0;
        for (char *l = smali; *l && !stop; line++) {
            char *nl = strchr(l, '\n');
            size_t len = nl ? (size_t)(nl - l) : strlen(l);
            if (line_hit(&m, kind, l, len)) {
                char save = l[len];
                l[len] = '\0';
                stop = fn(user, ci, line, l);
                l[len] = save;
            }
            l += len + (nl ? 1 : 0);
        }
        free(smali);
        if (stop) break;
    }
out:
    free(strset);
    if (m.has_re) regfree(&m.re);
    return 0;
}
