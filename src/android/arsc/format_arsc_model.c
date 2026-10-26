#include "format_arsc_model.h"
#include <stdlib.h>
#include <string.h>

static char *arsc_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *o = malloc(n);
    if (o) memcpy(o, s, n);
    return o;
}

/* Appends `s` to a NULL terminated array of strings, returns its index. */
static uint32_t str_list_add(char ***list, uint32_t *count, const char *s) {
    char **grown = realloc(*list, (*count + 1) * sizeof(char *));
    if (!grown) return 0;
    *list = grown;
    grown[*count] = arsc_dup(s);
    if (!grown[*count]) return 0;
    (*count)++;
    return *count - 1;
}

arsc_model *arsc_model_new(void) {
    arsc_model *m = calloc(1, sizeof(*m));
    if (m) m->utf8 = 1;
    return m;
}

void arsc_model_free(arsc_model *m) {
    if (!m) return;
    for (uint32_t i = 0; i < m->value_count; i++) free(m->values[i]);
    free(m->values);
    for (uint32_t p = 0; p < m->pkg_count; p++) {
        arsc_model_pkg *pk = &m->pkgs[p];
        for (uint32_t i = 0; i < pk->type_string_count; i++) free(pk->type_strings[i]);
        for (uint32_t i = 0; i < pk->key_string_count; i++) free(pk->key_strings[i]);
        free(pk->type_strings);
        free(pk->key_strings);
        for (uint32_t t = 0; t < pk->type_count; t++) {
            arsc_model_type *ty = &pk->types[t];
            for (uint32_t v = 0; v < ty->variant_count; v++) {
                arsc_model_variant *va = &ty->variants[v];
                for (uint32_t e = 0; e < va->entry_count; e++) {
                    free(va->entries[e].map);
                }
                free(va->entries);
            }
            free(ty->variants);
            free(ty->spec_flags);
        }
        free(pk->types);
    }
    free(m->pkgs);
    free(m);
}

arsc_model_pkg *arsc_model_add_pkg(arsc_model *m, uint8_t id, const char *name) {
    if (!m) return NULL;
    arsc_model_pkg *grown = realloc(m->pkgs, (m->pkg_count + 1) * sizeof(arsc_model_pkg));
    if (!grown) return NULL;
    m->pkgs = grown;
    arsc_model_pkg *pk = &m->pkgs[m->pkg_count++];
    memset(pk, 0, sizeof(*pk));
    pk->id = id;
    if (name) {
        strncpy(pk->name, name, sizeof(pk->name) - 1);
    }
    return pk;
}

uint32_t arsc_model_add_value(arsc_model *m, const char *s) {
    if (!m) return 0;
    return str_list_add(&m->values, &m->value_count, s ? s : "");
}

const char *arsc_model_value(const arsc_model *m, uint32_t index) {
    if (!m || index >= m->value_count) return NULL;
    return m->values[index];
}

uint32_t arsc_pkg_add_type_string(arsc_model_pkg *p, const char *s) {
    if (!p) return 0;
    return str_list_add(&p->type_strings, &p->type_string_count, s ? s : "");
}

uint32_t arsc_pkg_add_key(arsc_model_pkg *p, const char *s) {
    if (!p) return 0;
    return str_list_add(&p->key_strings, &p->key_string_count, s ? s : "");
}

const char *arsc_pkg_type_string(const arsc_model_pkg *p, uint32_t index) {
    if (!p || index >= p->type_string_count) return NULL;
    return p->type_strings[index];
}

const char *arsc_pkg_key(const arsc_model_pkg *p, uint32_t index) {
    if (!p || index >= p->key_string_count) return NULL;
    return p->key_strings[index];
}

arsc_model_type *arsc_pkg_find_type(arsc_model_pkg *p, uint8_t id) {
    if (!p) return NULL;
    for (uint32_t i = 0; i < p->type_count; i++) {
        if (p->types[i].id == id) return &p->types[i];
    }
    return NULL;
}

arsc_model_type *arsc_pkg_add_type(arsc_model_pkg *p, uint8_t id, uint32_t entry_count) {
    if (!p) return NULL;
    arsc_model_type *grown = realloc(p->types, (p->type_count + 1) * sizeof(arsc_model_type));
    if (!grown) return NULL;
    p->types = grown;
    arsc_model_type *ty = &p->types[p->type_count++];
    memset(ty, 0, sizeof(*ty));
    ty->id = id;
    ty->entry_count = entry_count;
    if (entry_count) {
        ty->spec_flags = calloc(entry_count, sizeof(uint32_t));
        if (!ty->spec_flags) return NULL;
    }
    return ty;
}

arsc_model_variant *arsc_type_add_variant(arsc_model_type *t, uint32_t entry_count,
                                          const uint8_t *config, uint8_t config_size, uint8_t flags) {
    if (!t) return NULL;
    arsc_model_variant *grown = realloc(t->variants, (t->variant_count + 1) * sizeof(arsc_model_variant));
    if (!grown) return NULL;
    t->variants = grown;
    arsc_model_variant *v = &t->variants[t->variant_count++];
    memset(v, 0, sizeof(*v));
    v->flags = flags;
    v->entry_count = entry_count;
    if (config_size > ARSC_CONFIG_BYTES) config_size = ARSC_CONFIG_BYTES;
    v->config_size = config_size;
    if (config && config_size) memcpy(v->config, config, config_size);
    if (entry_count) {
        v->entries = calloc(entry_count, sizeof(arsc_model_entry));
        if (!v->entries) return NULL;
    }
    return v;
}

arsc_model_entry *arsc_variant_entry(arsc_model_variant *v, uint32_t index) {
    if (!v || index >= v->entry_count) return NULL;
    return &v->entries[index];
}

int arsc_variant_is_default(const arsc_model_variant *v) {
    if (!v) return 0;
    for (uint32_t i = 4; i < v->config_size; i++) {
        if (v->config[i]) return 0;
    }
    return 1;
}

/* Locates the type that holds `res_id`. */
static arsc_model_type *model_find_type(arsc_model *m, uint32_t res_id, uint32_t *entry_index) {
    uint8_t pkg_id = (uint8_t)((res_id >> 24) & 0xFF);
    uint8_t type_id = (uint8_t)((res_id >> 16) & 0xFF);
    uint32_t index = res_id & 0xFFFF;
    for (uint32_t p = 0; p < m->pkg_count; p++) {
        if (m->pkgs[p].id != pkg_id) continue;
        arsc_model_type *ty = arsc_pkg_find_type(&m->pkgs[p], type_id);
        if (!ty) return NULL;
        if (entry_index) *entry_index = index;
        return ty;
    }
    return NULL;
}

arsc_model_entry *arsc_model_find_entry(arsc_model *m, uint32_t res_id) {
    if (!m) return NULL;
    uint32_t index = 0;
    arsc_model_type *ty = model_find_type(m, res_id, &index);
    if (!ty) return NULL;
    for (uint32_t v = 0; v < ty->variant_count; v++) {
        arsc_model_variant *va = &ty->variants[v];
        if (!arsc_variant_is_default(va)) continue;
        arsc_model_entry *e = arsc_variant_entry(va, index);
        if (e && e->present) return e;
    }
    return NULL;
}

int arsc_model_set_string(arsc_model *m, uint32_t res_id, const char *text) {
    arsc_model_entry *e = arsc_model_find_entry(m, res_id);
    if (!e || !text) return -1;
    uint32_t idx = arsc_model_add_value(m, text);
    e->data_type = ARSC_TYPE_STRING;
    e->data = idx;
    e->map_count = 0;
    free(e->map);
    e->map = NULL;
    e->flags &= (uint16_t)~ARSC_ENTRY_COMPLEX;
    return 0;
}

int arsc_model_set_int(arsc_model *m, uint32_t res_id, int32_t value, uint8_t data_type) {
    arsc_model_entry *e = arsc_model_find_entry(m, res_id);
    if (!e) return -1;
    e->data_type = data_type;
    e->data = (uint32_t)value;
    e->map_count = 0;
    free(e->map);
    e->map = NULL;
    e->flags &= (uint16_t)~ARSC_ENTRY_COMPLEX;
    return 0;
}
