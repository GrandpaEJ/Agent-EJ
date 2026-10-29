/* The deobfuscation rename table: a hash map from an obfuscated class
 * descriptor body (e.g. "a/b/c") to its replacement, each carrying a short
 * list of member (field/method) renames. Both the mapping-file and heuristic
 * modes fill this table; the rewrite engine consumes it. */
#include "formats.h"
#include "deobf_internal.h"
#include <stdlib.h>
#include <string.h>

#define DEOBF_BUCKETS 4096u

static size_t hash_str(const char *s) {
    size_t h = 5381;
    for (; *s; s++) h = ((h << 5) + h) ^ (unsigned char)*s;
    return h;
}

deobf_table *deobf_table_new(void) {
    deobf_table *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->nbuckets = DEOBF_BUCKETS;
    t->buckets = calloc(t->nbuckets, sizeof(deobf_class *));
    if (!t->buckets) { free(t); return NULL; }
    return t;
}

void deobf_table_free(deobf_table *t) {
    if (!t) return;
    for (size_t i = 0; i < t->nbuckets; i++) {
        deobf_class *c = t->buckets[i];
        while (c) {
            deobf_class *cn = c->hnext;
            deobf_member *m = c->members;
            while (m) { deobf_member *mn = m->next; free(m->obf); free(m->rep); free(m); m = mn; }
            free(c->obf); free(c->rep); free(c);
            c = cn;
        }
    }
    free(t->buckets);
    free(t);
}

deobf_class *deobf_find_class(deobf_table *t, const char *obf_body) {
    if (!t || !obf_body) return NULL;
    size_t b = hash_str(obf_body) % t->nbuckets;
    for (deobf_class *c = t->buckets[b]; c; c = c->hnext)
        if (strcmp(c->obf, obf_body) == 0) return c;
    return NULL;
}

/* Returns the class entry for obf_body, creating it if absent. */
static deobf_class *class_intern(deobf_table *t, const char *obf_body) {
    deobf_class *c = deobf_find_class(t, obf_body);
    if (c) return c;
    c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->obf = strdup(obf_body);
    if (!c->obf) { free(c); return NULL; }
    size_t b = hash_str(obf_body) % t->nbuckets;
    c->hnext = t->buckets[b];
    t->buckets[b] = c;
    t->nclasses++;
    return c;
}

void deobf_add_class(deobf_table *t, const char *obf_body, const char *new_body) {
    deobf_class *c = class_intern(t, obf_body);
    if (!c || !new_body) return;
    if (!c->rep) c->rep = strdup(new_body);   /* first mapping wins */
}

void deobf_add_member(deobf_table *t, const char *obf_class_body,
                      const char *obf_name, const char *new_name) {
    if (!obf_name || !new_name) return;
    deobf_class *c = class_intern(t, obf_class_body);
    if (!c) return;
    for (deobf_member *m = c->members; m; m = m->next)
        if (strcmp(m->obf, obf_name) == 0) {
            /* Same obfuscated name mapping to a different original is an
             * ambiguous overload collapse; neutralise it (empty rep = skip). */
            if (m->rep && m->rep[0] && strcmp(m->rep, new_name) != 0) {
                free(m->rep); m->rep = strdup("");
            }
            return;
        }
    deobf_member *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->obf = strdup(obf_name);
    m->rep = strdup(new_name);
    if (!m->obf || !m->rep) { free(m->obf); free(m->rep); free(m); return; }
    m->next = c->members;
    c->members = m;
}

const char *deobf_find_member(deobf_table *t, const char *obf_body,
                              const char *obf_member) {
    deobf_class *c = deobf_find_class(t, obf_body);
    if (!c) return NULL;
    for (deobf_member *m = c->members; m; m = m->next)
        if (strcmp(m->obf, obf_member) == 0) return m->rep;
    return NULL;
}
