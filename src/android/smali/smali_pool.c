#include "smali_pool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static uint32_t pool_hash(const char *s) {
    uint32_t h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

static void pool_htable_insert(smali_pool_strings_t *p, uint32_t idx) {
    uint32_t bi = pool_hash(p->strings[idx]) & (p->bucket_count - 1);
    smali_pool_entry_t *e = malloc(sizeof(*e));
    e->idx = idx;
    e->next = p->buckets[bi];
    p->buckets[bi] = e;
}

void smali_pool_init(smali_pool_strings_t *p) {
    p->strings = NULL; p->count = 0; p->cap = 0;
    p->bucket_count = POOL_HASH_SIZE;
    p->buckets = calloc(p->bucket_count, sizeof(smali_pool_entry_t *));
}

void smali_pool_free(smali_pool_strings_t *p) {
    for (uint32_t i = 0; i < p->count; i++) free(p->strings[i]);
    free(p->strings); p->strings = NULL; p->count = 0; p->cap = 0;
    for (uint32_t i = 0; i < p->bucket_count; i++) {
        smali_pool_entry_t *e = p->buckets[i];
        while (e) { smali_pool_entry_t *n = e->next; free(e); e = n; }
    }
    free(p->buckets); p->buckets = NULL; p->bucket_count = 0;
}

uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str) {
    if (!str) return 0xFFFFFFFF;
    uint32_t bi = pool_hash(str) & (p->bucket_count - 1);
    for (smali_pool_entry_t *e = p->buckets[bi]; e; e = e->next) {
        if (strcmp(p->strings[e->idx], str) == 0) return e->idx;
    }
    if (p->count >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->strings = realloc(p->strings, p->cap * sizeof(char *));
    }
    p->strings[p->count] = strdup(str);
    pool_htable_insert(p, p->count);
    return p->count++;
}

uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str) {
    if (!str) return 0xFFFFFFFF;
    uint32_t bi = pool_hash(str) & (p->bucket_count - 1);
    for (smali_pool_entry_t *e = p->buckets[bi]; e; e = e->next) {
        if (strcmp(p->strings[e->idx], str) == 0) return e->idx;
    }
    return 0xFFFFFFFF;
}

static int comp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void pool_rebuild_hash(smali_pool_strings_t *p) {
    for (uint32_t i = 0; i < p->bucket_count; i++) {
        smali_pool_entry_t *e = p->buckets[i];
        while (e) { smali_pool_entry_t *n = e->next; free(e); e = n; }
        p->buckets[i] = NULL;
    }
    for (uint32_t i = 0; i < p->count; i++) pool_htable_insert(p, i);
}

void smali_pool_sort_strings(smali_pool_strings_t *p) {
    /* qsort() must not be called with a NULL base even for a zero count. */
    if (p->count > 1) qsort(p->strings, p->count, sizeof(char *), comp_str);
    pool_rebuild_hash(p);
}

static void add_sig_types(smali_pool_strings_t *strings, const char *sig) {
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') {
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
            else p++;
        } else {
            p++;
        }
        char *type = strndup(start, p - start);
        smali_pool_add(strings, type);
        free(type);
    }
    if (*p == ')') p++;
    if (*p) {
        smali_pool_add(strings, p);
    }
}

/* Adds every bare primitive (and V) in a "(params)ret" signature to the type pool. */
static void add_sig_prims(smali_pool_strings_t *types, const char *sig) {
    const char *p = strchr(sig, '(');
    if (!p) return;
    for (p++; *p && *p != ')'; ) {
        if (*p == 'L') {
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; }
            if (*p) p++;
        } else {
            char t[2] = { *p++, '\0' };
            smali_pool_add(types, t);
        }
    }
    if (*p == ')' && p[1] && strchr("VZBSCIJFD", p[1])) {
        char t[2] = { p[1], '\0' };
        smali_pool_add(types, t);
    }
}

static void add_shorty_string(smali_pool_strings_t *strings, const char *sig) {
    char shorty[512];
    int s_idx = 0;
    const char *close_paren = strchr(sig, ')');
    if (close_paren) {
        char ret_char = close_paren[1];
        shorty[s_idx++] = (ret_char == 'L' || ret_char == '[') ? 'L' : ret_char;
    } else {
        shorty[s_idx++] = 'V';
    }
    
    const char *p = sig;
    if (*p == '(') p++;
    while (p && *p && *p != ')') {
        if (*p == 'L') {
            shorty[s_idx++] = 'L';
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            shorty[s_idx++] = 'L';
            while (*p == '[') p++;
            if (*p == 'L') {
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else {
                p++;
            }
        } else {
            shorty[s_idx++] = *p;
            p++;
        }
    }
    shorty[s_idx] = '\0';
    smali_pool_add(strings, shorty);
}

static smali_ctx_def_t *s_sort_ctx = NULL;

static int comp_proto_sig(const void *a, const void *b) {
    const char *sigA = *(const char **)a;
    const char *sigB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    if (!sigA || !sigB) {
        if (sigA == sigB) return 0;
        return sigA ? -1 : 1;
    }
    const char *closeA = strchr(sigA, ')');
    const char *closeB = strchr(sigB, ')');
    const char *retA = closeA ? closeA + 1 : "V";
    const char *retB = closeB ? closeB + 1 : "V";
    
    uint32_t typeA = smali_pool_find(&ctx->types, retA);
    uint32_t typeB = smali_pool_find(&ctx->types, retB);
    if (typeA != typeB) return (typeA < typeB) ? -1 : 1;
    
    uint32_t ptypesA[64], ptypesB[64];
    uint32_t countA = 0, countB = 0;
    
    const char *p = sigA;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
        else if (*p == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; } else p++; }
        else p++;
        char *tstr = strndup(start, p - start);
        /* A proto may carry more than 64 parameters; stop writing instead of
         * overflowing the fixed stack arrays. */
        if (countA < 64) ptypesA[countA] = smali_pool_find(&ctx->types, tstr);
        countA++;
        free(tstr);
    }
    
    p = sigB;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
        else if (*p == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; } else p++; }
        else p++;
        char *tstr = strndup(start, p - start);
        if (countB < 64) ptypesB[countB] = smali_pool_find(&ctx->types, tstr);
        countB++;
        free(tstr);
    }
    
    uint32_t min_count = (countA < countB) ? countA : countB;
    if (min_count > 64) min_count = 64;
    for (uint32_t i = 0; i < min_count; i++) {
        if (ptypesA[i] != ptypesB[i]) {
            return (ptypesA[i] < ptypesB[i]) ? -1 : 1;
        }
    }
    if (countA != countB) {
        return (countA < countB) ? -1 : 1;
    }
    return 0;
}

static int field_key_split(const char *key, char **arrow, char **colon) {
    *arrow = key ? strstr(key, "->") : NULL;
    *colon = key ? strchr(key, ':') : NULL;
    return (*arrow && *colon && *colon > *arrow + 2) ? 1 : 0;
}

static int comp_field_key(const void *a, const void *b) {
    const char *keyA = *(const char **)a;
    const char *keyB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    char *arrowA = NULL, *colonA = NULL, *arrowB = NULL, *colonB = NULL;
    /* Malformed pool keys exist in the wild (e.g. the "??" placeholder emitted
     * for unresolvable refs). Grouping them instead of dereferencing a NULL
     * "->" keeps qsort from segfaulting. */
    int okA = field_key_split(keyA, &arrowA, &colonA);
    int okB = field_key_split(keyB, &arrowB, &colonB);
    if (!okA || !okB) {
        if (okA != okB) return okA ? -1 : 1;
        return strcmp(keyA ? keyA : "", keyB ? keyB : "");
    }
    char *clsA = strndup(keyA, arrowA - keyA);
    char *nameA = strndup(arrowA + 2, colonA - (arrowA + 2));
    char *typeA = colonA + 1;
    
    char *clsB = strndup(keyB, arrowB - keyB);
    char *nameB = strndup(arrowB + 2, colonB - (arrowB + 2));
    char *typeB = colonB + 1;
    
    uint32_t cIdxA = smali_pool_find(&ctx->types, clsA);
    uint32_t cIdxB = smali_pool_find(&ctx->types, clsB);
    free(clsA); free(clsB);
    
    if (cIdxA != cIdxB) {
        free(nameA); free(nameB);
        return (cIdxA < cIdxB) ? -1 : 1;
    }
    
    uint32_t nIdxA = smali_pool_find(&ctx->strings, nameA);
    uint32_t nIdxB = smali_pool_find(&ctx->strings, nameB);
    free(nameA); free(nameB);
    
    if (nIdxA != nIdxB) {
        return (nIdxA < nIdxB) ? -1 : 1;
    }
    
    uint32_t tIdxA = smali_pool_find(&ctx->types, typeA);
    uint32_t tIdxB = smali_pool_find(&ctx->types, typeB);
    return (tIdxA < tIdxB) ? -1 : 1;
}

static int comp_method_key(const void *a, const void *b) {
    const char *keyA = *(const char **)a;
    const char *keyB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    char *arrowA = keyA ? strstr(keyA, "->") : NULL;
    char *parenA = keyA ? strchr(keyA, '(') : NULL;
    char *arrowB = keyB ? strstr(keyB, "->") : NULL;
    char *parenB = keyB ? strchr(keyB, '(') : NULL;
    /* Same hardening as comp_field_key: unresolvable refs land here as
     * placeholders without "->" and must not be dereferenced. */
    if (!arrowA || !parenA || parenA < arrowA + 2 ||
        !arrowB || !parenB || parenB < arrowB + 2) {
        int okA = (arrowA && parenA && parenA > arrowA + 2);
        int okB = (arrowB && parenB && parenB > arrowB + 2);
        if (okA != okB) return okA ? -1 : 1;
        return strcmp(keyA ? keyA : "", keyB ? keyB : "");
    }
    char *clsA = strndup(keyA, arrowA - keyA);
    char *nameA = strndup(arrowA + 2, parenA - (arrowA + 2));
    char *protoA = parenA;
    
    char *clsB = strndup(keyB, arrowB - keyB);
    char *nameB = strndup(arrowB + 2, parenB - (arrowB + 2));
    char *protoB = parenB;
    
    uint32_t cIdxA = smali_pool_find(&ctx->types, clsA);
    uint32_t cIdxB = smali_pool_find(&ctx->types, clsB);
    free(clsA); free(clsB);
    
    if (cIdxA != cIdxB) {
        free(nameA); free(nameB);
        return (cIdxA < cIdxB) ? -1 : 1;
    }
    
    uint32_t nIdxA = smali_pool_find(&ctx->strings, nameA);
    uint32_t nIdxB = smali_pool_find(&ctx->strings, nameB);
    free(nameA); free(nameB);
    
    if (nIdxA != nIdxB) {
        return (nIdxA < nIdxB) ? -1 : 1;
    }
    
    uint32_t pIdxA = smali_pool_find(&ctx->protos, protoA);
    uint32_t pIdxB = smali_pool_find(&ctx->protos, protoB);
    return (pIdxA < pIdxB) ? -1 : 1;
}

static void pool_add_annotation_elem(smali_ctx_def_t *ctx, smali_annotation_elem_t *el) {
    if (el->name) smali_pool_add(&ctx->strings, el->name);
    if (el->value_type == VALUE_TYPE_ENUM && el->value_str) {
        /* Only well-formed "class->name:type" keys may enter the field pool;
         * the writer and the comparator both require that shape. */
        char *arrow = strstr(el->value_str, "->");
        char *colon = strrchr(el->value_str, ':');
        if (arrow && colon && colon > arrow + 2) {
            smali_pool_add(&ctx->fields, el->value_str);
            char *cls = strndup(el->value_str, arrow - el->value_str);
            char *typ = strndup(colon + 1, strlen(colon + 1));
            char *name = strndup(arrow + 2, colon - (arrow + 2));
            smali_pool_add(&ctx->types, cls); smali_pool_add(&ctx->strings, cls);
            smali_pool_add(&ctx->types, typ); smali_pool_add(&ctx->strings, typ);
            smali_pool_add(&ctx->strings, name);
            free(cls); free(typ); free(name);
        }
    }
    if (el->value_type == VALUE_TYPE_TYPE && el->value_str)
        smali_pool_add(&ctx->types, el->value_str);
    if (el->value_type == VALUE_TYPE_STRING && el->value_str)
        smali_pool_add(&ctx->strings, el->value_str);
    if (el->value_type == VALUE_TYPE_METHOD && el->value_str) {
        /* Same rule as the enum branch: only "class->name(args)ret" keys. */
        char *arrow = strstr(el->value_str, "->");
        char *paren = arrow ? strchr(arrow, '(') : NULL;
        if (arrow && paren && paren > arrow + 2) {
            smali_pool_add(&ctx->methods, el->value_str);
            smali_pool_add(&ctx->strings, el->value_str);
            char *cls = strndup(el->value_str, arrow - el->value_str);
            smali_pool_add(&ctx->strings, cls);
            smali_pool_add(&ctx->types, cls);
            free(cls);
            char *name = strndup(arrow + 2, paren - (arrow + 2));
            smali_pool_add(&ctx->strings, name);
            add_sig_types(&ctx->strings, paren);
            add_shorty_string(&ctx->strings, paren);
            smali_pool_add(&ctx->protos, paren);
            free(name);
        }
    }
    if (el->value_type == VALUE_TYPE_FIELD && el->value_str) {
        char *arrow = strstr(el->value_str, "->");
        char *colon = arrow ? strchr(arrow, ':') : NULL;
        if (arrow && colon && colon > arrow + 2) {
            smali_pool_add(&ctx->fields, el->value_str);
            smali_pool_add(&ctx->strings, el->value_str);
            char *cls = strndup(el->value_str, arrow - el->value_str);
            char *name = strndup(arrow + 2, colon - (arrow + 2));
            char *typ = strdup(colon + 1);
            smali_pool_add(&ctx->strings, cls); smali_pool_add(&ctx->types, cls);
            smali_pool_add(&ctx->strings, name);
            smali_pool_add(&ctx->strings, typ); smali_pool_add(&ctx->types, typ);
            free(cls); free(name); free(typ);
        }
    }
    if (el->value_type == VALUE_TYPE_ARRAY) {
        smali_annot_val_t *node = el->arr_head;
        while (node) {
            pool_add_annotation_elem(ctx, &node->elem);
            node = node->next;
        }
    }
    if (el->value_type == VALUE_TYPE_ANNOT && el->annot_type) {
        smali_pool_add(&ctx->strings, el->annot_type);
        for (int si = 0; si < el->annot_elem_count; si++) {
            pool_add_annotation_elem(ctx, &el->sub_elems[si]);
        }
    }
}

static void pool_add_annot(smali_ctx_def_t *ctx, smali_annotation_t *a) {
    if (a->type) smali_pool_add(&ctx->strings, a->type);
    for (uint32_t k = 0; k < a->elem_count && k < MAX_ANNOT_ELEMS; k++)
        pool_add_annotation_elem(ctx, &a->elems[k]);
}

void smali_pool_build_all(smali_ctx_def_t *ctx) {
    smali_pool_init(&ctx->strings);
    smali_pool_init(&ctx->types);
    smali_pool_init(&ctx->protos);
    smali_pool_init(&ctx->fields);
    smali_pool_init(&ctx->methods);

    // Seed only common primitive type strings (VZIJFD are always needed for shorties)
    // B, C, S are added on-demand when actually referenced
    const char *prims[] = {"V", "Z", "I", "J", "F", "D"};
    for (int i = 0; i < 6; i++) smali_pool_add(&ctx->strings, prims[i]);

    // 1. Gather all raw strings from class headers and instructions
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        smali_pool_add(&ctx->strings, c->descriptor);
        if (c->super_class) smali_pool_add(&ctx->strings, c->super_class);
        if (c->source_file) smali_pool_add(&ctx->strings, c->source_file);
        for (uint32_t j = 0; j < c->interface_count; j++) smali_pool_add(&ctx->strings, c->interfaces[j]);

        /* add annotation types and element names */
        for (uint32_t j = 0; j < c->annot_count && j < MAX_ANNOTS; j++) {
            smali_annotation_t *a = &c->annots[j];
            if (a->type) smali_pool_add(&ctx->strings, a->type);
            for (uint32_t k = 0; k < a->elem_count && k < MAX_ANNOT_ELEMS; k++) {
                smali_annotation_elem_t *el = &a->elems[k];
                pool_add_annotation_elem(ctx, el);
            }
        }
        /* add method annotation types */
        for (int mt = 0; mt < 2; mt++) {
            uint32_t mc = mt ? c->virtual_method_count : c->direct_method_count;
            smali_method_def_t *ma = mt ? c->virtual_methods : c->direct_methods;
            for (uint32_t j = 0; j < mc; j++) {
for (uint32_t pi = 0; pi < ma[j].param_name_count && ma[j].param_annots && ma[j].param_annot_counts; pi++)
                    for (uint32_t ai = 0; ma[j].param_annots[pi] && ai < ma[j].param_annot_counts[pi] && ai < MAX_ANNOTS; ai++)
                        pool_add_annot(ctx, &ma[j].param_annots[pi][ai]);
                if (ma[j].annot_count > 0) {
                    for (uint32_t ai = 0; ai < ma[j].annot_count && ai < MAX_ANNOTS; ai++) {
                        smali_annotation_t *a = &ma[j].annots[ai];
                        if (a->type) smali_pool_add(&ctx->strings, a->type);
                        for (uint32_t k = 0; k < a->elem_count && k < MAX_ANNOT_ELEMS; k++) {
                            smali_annotation_elem_t *el = &a->elems[k];
                            pool_add_annotation_elem(ctx, el);
                        }
                    }
                }
            }
        }

        for (uint32_t j = 0; j < c->static_field_count; j++) {
            smali_pool_add(&ctx->strings, c->static_fields[j].name);
            smali_pool_add(&ctx->strings, c->static_fields[j].type);
            if (c->static_fields[j].has_init_value) {
                smali_field_def_t *f = &c->static_fields[j];
                if (f->value_type == VALUE_TYPE_STRING && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
                else if (f->value_type == VALUE_TYPE_TYPE && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
                else if (f->value_type == VALUE_TYPE_ENUM && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
            }
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            smali_pool_add(&ctx->strings, c->instance_fields[j].name);
            smali_pool_add(&ctx->strings, c->instance_fields[j].type);
        }
        for (int ft = 0; ft < 2; ft++) {
            uint32_t fc = ft ? c->instance_field_count : c->static_field_count;
            smali_field_def_t *fa = ft ? c->instance_fields : c->static_fields;
            for (uint32_t j = 0; j < fc; j++)
                for (uint32_t ai = 0; ai < fa[j].annot_count && ai < MAX_ANNOTS; ai++)
                    pool_add_annot(ctx, &fa[j].annots[ai]);
        }
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                smali_method_def_t *m = &m_arr[j];
                smali_pool_add(&ctx->strings, m->name);
                add_sig_types(&ctx->strings, m->signature);
                add_shorty_string(&ctx->strings, m->signature);

                /* The debug information refers to the parameter and local
                 * names by their index in the string pool. */
                for (uint32_t pi = 0; pi < m->param_name_count; pi++) {
                    if (m->param_names[pi]) smali_pool_add(&ctx->strings, m->param_names[pi]);
                }
                for (uint32_t li = 0; li < m->local_count; li++) {
                    if (m->local_names[li]) smali_pool_add(&ctx->strings, m->local_names[li]);
                    if (m->local_types[li]) {
                        smali_pool_add(&ctx->types, m->local_types[li]);
                        smali_pool_add(&ctx->strings, m->local_types[li]);
                    }
                    if (m->local_sigs[li]) smali_pool_add(&ctx->strings, m->local_sigs[li]);
                }

                for (uint32_t c_idx = 0; c_idx < m->catches_count; c_idx++) {
                    if (m->catches[c_idx].type) {
                        smali_pool_add(&ctx->strings, m->catches[c_idx].type);
                    }
                }

                for (uint32_t k = 0; k < m->insns_count; k++) {
                    smali_insn_t *ins = &m->insns[k];
                    if (ins->ref_str) {
                        char *arrow = (ins->kind == 3 || ins->kind == 4) ? strstr(ins->ref_str, "->") : NULL;
                        if (arrow) {
                            char *colon = strchr(ins->ref_str, ':');
                            char *paren = strchr(ins->ref_str, '(');
                            char *class_part = strndup(ins->ref_str, arrow - ins->ref_str);
                            smali_pool_add(&ctx->strings, class_part);
                            if (colon) {
                                char *name_part = strndup(arrow + 2, colon - (arrow + 2));
                                smali_pool_add(&ctx->strings, name_part);
                                smali_pool_add(&ctx->strings, colon + 1);
                                free(name_part);
                            } else if (paren) {
                                char *name_part = strndup(arrow + 2, paren - (arrow + 2));
                                smali_pool_add(&ctx->strings, name_part);
                                add_sig_types(&ctx->strings, paren);
                                add_shorty_string(&ctx->strings, paren);
                                free(name_part);
                            }
                            free(class_part);
                        } else {
                            smali_pool_add(&ctx->strings, ins->ref_str);
                        }
                    }
                }
            }
        }
    }

    smali_pool_sort_strings(&ctx->strings);

    // 2. Build Type Pool - only types actually referenced
    for (uint32_t i = 0; i < ctx->strings.count; i++) {
        const char *s = ctx->strings.strings[i];
        if ((s[0] == 'L' && s[1] != '\0' && strchr(s, ';') && !strstr(s, "->")) || s[0] == '[') {
            smali_pool_add(&ctx->types, s);
        }
    }
    /* V is always needed as a type (void return type) */
    smali_pool_add(&ctx->types, "V");

    // Add primitive types that appear as field types or return types in method sigs
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].type && c->static_fields[j].type[0])
                smali_pool_add(&ctx->types, c->static_fields[j].type);
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (c->instance_fields[j].type && c->instance_fields[j].type[0])
                smali_pool_add(&ctx->types, c->instance_fields[j].type);
        }
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = mtype ? c->virtual_method_count : c->direct_method_count;
            smali_method_def_t *m_arr = mtype ? c->virtual_methods : c->direct_methods;
            for (uint32_t j = 0; j < mc; j++) {
                const char *sig = m_arr[j].signature;
                add_sig_prims(&ctx->types, sig);
                for (uint32_t k = 0; k < m_arr[j].insns_count; k++) {
                    smali_insn_t *ins = &m_arr[j].insns[k];
                    if (ins->ref_str && ins->kind == 4) {
                        add_sig_prims(&ctx->types, ins->ref_str);
                    }
                }
            }
        }
    }

    // 3. Build Proto/Field/Method Pools
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                smali_method_def_t *m = &m_arr[j];
                char key[4096];
                snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, m->name, m->signature);
                smali_pool_add(&ctx->methods, key);
                smali_pool_add(&ctx->protos, m->signature);

                for (uint32_t k = 0; k < m->insns_count; k++) {
                    smali_insn_t *ins = &m->insns[k];
                    if (ins->ref_str) {
                        if (ins->kind == 3) {
                            /* Unresolvable refs are emitted as "??" placeholders by
                             * the disassembler; never let them into the pool. */
                            if (strstr(ins->ref_str, "->"))
                                smali_pool_add(&ctx->fields, ins->ref_str);
                        } else if (ins->kind == 4) {
                            if (strstr(ins->ref_str, "->"))
                                smali_pool_add(&ctx->methods, ins->ref_str);
                            char *paren = strchr(ins->ref_str, '(');
                            if (paren) {
                                smali_pool_add(&ctx->protos, paren);
                            }
                        }
                    }
                }
            }
        }
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            char key[4096];
            snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
            smali_pool_add(&ctx->fields, key);
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            char key[4096];
            snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
            smali_pool_add(&ctx->fields, key);
        }
    }

    /* Add enum/type field references from annotation values */
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (uint32_t j = 0; j < c->annot_count && j < MAX_ANNOTS; j++) {
            for (uint32_t k = 0; k < c->annots[j].elem_count && k < MAX_ANNOT_ELEMS; k++) {
                smali_annotation_elem_t *el = &c->annots[j].elems[k];
                pool_add_annotation_elem(ctx, el);
                if (el->value_type == VALUE_TYPE_FIELD && el->value_str) {
                    char *ar = strstr(el->value_str, "->");
                    char *co = ar ? strrchr(el->value_str, ':') : NULL;
                    if (ar && co && co > ar + 2) {
                        smali_pool_add(&ctx->fields, el->value_str);
                        char *cls = strndup(el->value_str, ar - el->value_str);
                        char *typ = strndup(co + 1, strlen(co + 1));
                        char *nm = strndup(ar + 2, co - (ar + 2));
                        smali_pool_add(&ctx->types, cls); smali_pool_add(&ctx->strings, cls);
                        smali_pool_add(&ctx->types, typ); smali_pool_add(&ctx->strings, typ);
                        smali_pool_add(&ctx->strings, nm);
                        free(cls); free(typ); free(nm);
                    }
                }
                if (el->value_type == VALUE_TYPE_ARRAY) {
                    smali_annot_val_t *node = el->arr_head;
                    while (node) {
                        if (node->elem.value_type == VALUE_TYPE_ENUM && node->elem.value_str) {
                            char *ar = strstr(node->elem.value_str, "->");
                            char *co = ar ? strrchr(node->elem.value_str, ':') : NULL;
                            if (ar && co && co > ar + 2) {
                                smali_pool_add(&ctx->fields, node->elem.value_str);
                                char *cls = strndup(node->elem.value_str, ar - node->elem.value_str);
                                char *typ = strndup(co + 1, strlen(co + 1));
                                char *nm = strndup(ar + 2, co - (ar + 2));
                                smali_pool_add(&ctx->types, cls); smali_pool_add(&ctx->strings, cls);
                                smali_pool_add(&ctx->types, typ); smali_pool_add(&ctx->strings, typ);
                                smali_pool_add(&ctx->strings, nm);
                                free(cls);
                                free(typ);
                                free(nm);
                            }
                        }
                        if (node->elem.value_type == VALUE_TYPE_TYPE && node->elem.value_str)
                            smali_pool_add(&ctx->types, node->elem.value_str);
                        if (node->elem.value_type == VALUE_TYPE_METHOD && node->elem.value_str) {
                            char *ar = strstr(node->elem.value_str, "->");
                            char *paren = ar ? strchr(ar, '(') : NULL;
                            if (ar && paren && paren > ar + 2) {
                                smali_pool_add(&ctx->methods, node->elem.value_str);
                                char *cls = strndup(node->elem.value_str, ar - node->elem.value_str);
                                smali_pool_add(&ctx->types, cls); smali_pool_add(&ctx->strings, cls);
                                free(cls);
                                char *nm = strndup(ar + 2, paren - (ar + 2));
                                smali_pool_add(&ctx->strings, nm);
                                free(nm);
                                smali_pool_add(&ctx->protos, paren);
                            }
                        }
                        node = node->next;
                    }
                }
            }
        }
        for (int mt = 0; mt < 2; mt++) {
            uint32_t mc = mt ? c->virtual_method_count : c->direct_method_count;
            smali_method_def_t *ma = mt ? c->virtual_methods : c->direct_methods;
            for (uint32_t j = 0; j < mc; j++) {
                if (!ma[j].annot_count) continue;
                for (uint32_t ai = 0; ai < ma[j].annot_count && ai < MAX_ANNOTS; ai++) {
                smali_annotation_t *ann = &ma[j].annots[ai];
                for (uint32_t k = 0; k < ann->elem_count && k < MAX_ANNOT_ELEMS; k++) {
                    smali_annotation_elem_t *el = &ann->elems[k];
                    pool_add_annotation_elem(ctx, el);
                    if (el->value_type == VALUE_TYPE_FIELD && el->value_str) {
                        smali_pool_add(&ctx->fields, el->value_str);
                        char *ar = strstr(el->value_str, "->");
                        char *co = strrchr(el->value_str, ':');
                        if (ar && co && co > ar) {
                            char *cls = strndup(el->value_str, ar - el->value_str);
                            char *typ = strndup(co + 1, strlen(co + 1));
                            char *nm = strndup(ar + 2, co - (ar + 2));
                            smali_pool_add(&ctx->types, cls); smali_pool_add(&ctx->strings, cls);
                            smali_pool_add(&ctx->types, typ); smali_pool_add(&ctx->strings, typ);
                            smali_pool_add(&ctx->strings, nm);
                            free(cls); free(typ); free(nm);
                        }
                    }
                }
            }
            }
        }
    }

    /* Add names from field/method pools to strings for writer lookups */
    for (uint32_t i = 0; i < ctx->fields.count; i++) {
        const char *key = ctx->fields.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        char *colon = strchr(key, ':');
        if (arrow && colon && colon > arrow + 2) {
            char *name = strndup(arrow + 2, colon - (arrow + 2));
            if (name && name[0]) smali_pool_add(&ctx->strings, name);
            free(name);
        }
    }
    for (uint32_t i = 0; i < ctx->methods.count; i++) {
        const char *key = ctx->methods.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        if (arrow) {
            char *colon = strchr(key, ':');
            char *paren = strchr(key, '(');
            if (colon && colon > arrow + 2) {
                char *name = strndup(arrow + 2, colon - (arrow + 2));
                if (name && name[0]) smali_pool_add(&ctx->strings, name);
                free(name);
            } else if (paren && paren > arrow + 2) {
                char *name = strndup(arrow + 2, paren - (arrow + 2));
                if (name && name[0]) smali_pool_add(&ctx->strings, name);
                free(name);
            }
        }
    }

    smali_pool_sort_strings(&ctx->strings);
    smali_pool_sort_strings(&ctx->types);

    s_sort_ctx = ctx;
    /* qsort() must not be called with a NULL base even for a zero count. */
    if (ctx->protos.count > 1)
        qsort(ctx->protos.strings, ctx->protos.count, sizeof(char *), comp_proto_sig);
    pool_rebuild_hash(&ctx->protos);
    if (ctx->fields.count > 1)
        qsort(ctx->fields.strings, ctx->fields.count, sizeof(char *), comp_field_key);
    pool_rebuild_hash(&ctx->fields);
    if (ctx->methods.count > 1)
        qsort(ctx->methods.strings, ctx->methods.count, sizeof(char *), comp_method_key);
    pool_rebuild_hash(&ctx->methods);
    s_sort_ctx = NULL;
}
