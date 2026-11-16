/* Translation view of resources.arsc (what MT calls Translation mode): every
 * <string> resource with its default text and its text in one other
 * configuration, edits written back through the arsc model. Untouched values
 * are never rewritten, so a table with no edits re-encodes byte for byte. */
#include "formats.h"
#include "format_arsc_model.h"
#include "format_arsc_write.h"
#include "format_arsc_xml_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct arsc_tr {
    arsc_model *m;
};

typedef struct {
    char *p;
    size_t len, cap;
} sbuf;

static void put(sbuf *b, const char *s, size_t n) {
    if (!b->p && b->cap == (size_t)-1) return;              /* earlier failure */
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + n + 1) cap *= 2;
        char *g = realloc(b->p, cap);
        if (!g) { free(b->p); b->p = NULL; b->cap = (size_t)-1; return; }
        b->p = g;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

/* One field of a dump line: tab, newline and backslash escaped. */
static void put_esc(sbuf *b, const char *s) {
    for (; s && *s; s++) {
        const char *e = *s == '\\' ? "\\\\" : *s == '\n' ? "\\n" : *s == '\t' ? "\\t" : *s == '\r' ? "\\r" : NULL;
        if (e) put(b, e, 2);
        else put(b, s, 1);
    }
}

arsc_tr *arsc_tr_open(const uint8_t *data, size_t size) {
    arsc_model *m = arsc_model_load(data, size);
    if (!m) return NULL;
    arsc_tr *t = calloc(1, sizeof(*t));
    if (!t) { arsc_model_free(m); return NULL; }
    t->m = m;
    return t;
}

void arsc_tr_free(arsc_tr *t) {
    if (!t) return;
    arsc_model_free(t->m);
    free(t);
}

uint8_t *arsc_tr_encode(arsc_tr *t, size_t *out_size) {
    return t ? arsc_encode(t->m, out_size) : NULL;
}

/* Calls fn for the "string" type of every package. */
#define EACH_STRING_TYPE(m, pk, ty)                                                  \
    for (uint32_t _p = 0; _p < (m)->pkg_count; _p++)                                 \
        for (arsc_model_pkg *pk = &(m)->pkgs[_p]; pk; pk = NULL)                      \
            for (uint32_t _t = 0; _t < pk->type_count; _t++)                          \
                for (arsc_model_type *ty = &pk->types[_t]; ty; ty = NULL)              \
                    if (ty->id && arsc_pkg_type_string(pk, ty->id - 1) &&             \
                        strcmp(arsc_pkg_type_string(pk, ty->id - 1), "string") == 0)

static arsc_model_variant *find_variant(arsc_model_type *ty, const char *qual) {
    for (uint32_t v = 0; v < ty->variant_count; v++) {
        arsc_model_variant *va = &ty->variants[v];
        if (!qual[0]) { if (arsc_variant_is_default(va)) return va; continue; }
        char q[160];
        arsc_variant_qualifier(va, q, sizeof(q));
        if (strcmp(q, qual) == 0) return va;
    }
    return NULL;
}

char *arsc_tr_locales(arsc_tr *t) {
    sbuf b = {0};
    put(&b, "", 0);
    if (!t) return b.p;
    EACH_STRING_TYPE(t->m, pk, ty) {
        for (uint32_t v = 0; v < ty->variant_count; v++) {
            char q[160];
            arsc_variant_qualifier(&ty->variants[v], q, sizeof(q));
            if (!q[0]) continue;                       /* the default is implied */
            size_t ql = strlen(q);
            int seen = 0;                              /* line-exact search */
            for (const char *s = b.p; s && (s = strstr(s, q)); s += ql)
                if ((s == b.p || s[-1] == '\n') && (s[ql] == '\n' || !s[ql])) { seen = 1; break; }
            if (seen) continue;
            if (b.len) put(&b, "\n", 1);
            put(&b, q, ql);
        }
    }
    return b.p;
}

static const char *text_of(arsc_model *m, arsc_model_entry *e) {
    if (!e || !e->present || (e->flags & ARSC_ENTRY_COMPLEX) || e->data_type != ARSC_TYPE_STRING) return NULL;
    return arsc_model_value(m, e->data);
}

char *arsc_tr_dump(arsc_tr *t, const char *qual) {
    sbuf b = {0};
    put(&b, "", 0);
    if (!t || !qual) return b.p;
    EACH_STRING_TYPE(t->m, pk, ty) {
        arsc_model_variant *def = find_variant(ty, "");
        arsc_model_variant *tgt = qual[0] ? find_variant(ty, qual) : NULL;
        for (uint32_t i = 0; i < ty->entry_count; i++) {
            const char *dv = text_of(t->m, arsc_variant_entry(def, i));
            if (!dv) continue;                          /* references, bags, gaps */
            arsc_model_entry *de = arsc_variant_entry(def, i);
            char id[16];
            snprintf(id, sizeof(id), "%08x\t", (unsigned)pk->id << 24 | (unsigned)ty->id << 16 | i);
            put(&b, id, 9);
            put_esc(&b, arsc_pkg_key(pk, de->key));
            put(&b, "\t", 1);
            put_esc(&b, dv);
            put(&b, "\t", 1);
            const char *tv = text_of(t->m, arsc_variant_entry(tgt, i));
            if (tv) { put(&b, "=", 1); put_esc(&b, tv); }  /* "=" marks a value, even an empty one */
            put(&b, "\n", 1);
        }
    }
    return b.p;
}

/* Config for "-ll" or "-ll-rCC", shaped like the default one (aapt2 size). */
static int locale_config(const arsc_model_variant *def, const char *qual, uint8_t *cfg, uint8_t *size) {
    size_t n = strlen(qual);
    if (!(n == 3 || (n == 7 && qual[3] == '-' && qual[4] == 'r')) || qual[0] != '-') return -1;
    *size = def && def->config_size >= 12 ? def->config_size : ARSC_CONFIG_BYTES;
    memset(cfg, 0, ARSC_CONFIG_BYTES);
    cfg[0] = *size;                                     /* ResTable_config.size, little endian */
    cfg[8] = (uint8_t)qual[1];
    cfg[9] = (uint8_t)qual[2];
    if (n == 7) { cfg[10] = (uint8_t)qual[5]; cfg[11] = (uint8_t)qual[6]; }
    return 0;
}

int arsc_tr_set(arsc_tr *t, uint32_t id, const char *qual, const char *text) {
    if (!t || !qual || !text) return -1;
    uint32_t idx = id & 0xFFFF;
    EACH_STRING_TYPE(t->m, pk, ty) {
        if (pk->id != (id >> 24) || ty->id != ((id >> 16) & 0xFF)) continue;
        arsc_model_variant *def = find_variant(ty, "");
        arsc_model_entry *de = arsc_variant_entry(def, idx);
        if (!text_of(t->m, de)) return -1;              /* only plain strings are translatable */
        arsc_model_variant *va = find_variant(ty, qual);
        if (!va) {                                      /* a new language */
            uint8_t cfg[ARSC_CONFIG_BYTES], size;
            if (locale_config(def, qual, cfg, &size) != 0) return -1;
            va = arsc_type_add_variant(ty, ty->entry_count, cfg, size, 0);
            if (!va) return -1;
            def = find_variant(ty, "");                 /* realloc moved the array */
            de = arsc_variant_entry(def, idx);
        }
        arsc_model_entry *e = arsc_variant_entry(va, idx);
        if (!e) return -1;
        const char *cur = text_of(t->m, e);
        if (cur && strcmp(cur, text) == 0) return 0;    /* unchanged: leave the pool alone */
        if (!e->present) {
            e->present = 1;
            e->flags = de->flags & (uint16_t)~ARSC_ENTRY_COMPLEX;
            e->key = de->key;
        }
        e->data_type = ARSC_TYPE_STRING;
        e->data = arsc_model_add_value(t->m, text);
        return 1;
    }
    return -1;
}
