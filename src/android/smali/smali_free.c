#include "smali_types.h"
#include "smali_pool.h"
#include "smali_annot.h"
#include <stdlib.h>
#include <string.h>

void smali_annot_elem_free(smali_annotation_elem_t *e) {
    if (!e) return;
    free(e->name);
    free(e->type);
    free(e->value_str);
    free(e->annot_type);
    smali_annot_val_t *node = e->arr_head;
    while (node) {
        smali_annot_val_t *next = node->next;
        smali_annot_elem_free(&node->elem);
        free(node);
        node = next;
    }
    if (e->sub_elems) {
        for (int i = 0; i < e->annot_elem_count; i++)
            smali_annot_elem_free(&e->sub_elems[i]);
        free(e->sub_elems);
    }
}

void smali_annot_free(smali_annotation_t *a) {
    free(a->type);
    for (uint32_t i = 0; i < a->elem_count; i++)
        smali_annot_elem_free(&a->elems[i]);
}

static void free_method(smali_method_def_t *m) {
    free(m->name);
    free(m->signature);
    for (uint32_t i = 0; i < m->insns_count; i++) {
        free(m->insns[i].ref_str);
        free(m->insns[i].label_target);
        for (uint32_t k = 0; k < m->insns[i].payload_targets_count; k++)
            free(m->insns[i].payload_targets[k]);
        free(m->insns[i].payload_targets);
        free(m->insns[i].payload_keys);
        free(m->insns[i].payload_data);
    }
    free(m->insns);
    for (uint32_t i = 0; i < m->labels_count; i++) free(m->labels[i].name);
    free(m->labels);
    for (uint32_t i = 0; i < m->catches_count; i++) {
        free(m->catches[i].type);
        free(m->catches[i].start_label);
        free(m->catches[i].end_label);
        free(m->catches[i].handler_label);
    }
    free(m->catches);
    for (uint32_t i = 0; i < m->param_name_count; i++) free(m->param_names[i]);
    free(m->param_names);
    for (uint32_t i = 0; i < m->local_count; i++) {
        free(m->local_names[i]);
        free(m->local_types[i]);
        free(m->local_sigs[i]);
    }
    free(m->local_names);
    free(m->local_types);
    free(m->local_sigs);
    free(m->local_regs);
    for (uint32_t i = 0; i < m->annot_count; i++) smali_annot_free(&m->annots[i]);
    free(m->annots);
    for (uint32_t i = 0; i < m->param_name_count; i++) {
        if (m->param_annots && m->param_annots[i]) {
            for (uint32_t k = 0; k < m->param_annot_counts[i]; k++)
                smali_annot_free(&m->param_annots[i][k]);
            free(m->param_annots[i]);
        }
    }
    free(m->param_annots);
    free(m->param_annot_counts);
}

static void free_field(smali_field_def_t *f) {
    free(f->name);
    free(f->type);
    free(f->value_str);
    free(f->array_vals);
    for (uint32_t i = 0; i < f->annot_count; i++) smali_annot_free(&f->annots[i]);
    free(f->annots);
}

static void free_class(smali_class_def_t *c) {
    free(c->descriptor);
    free(c->super_class);
    free(c->source_file);
    for (uint32_t i = 0; i < c->interface_count; i++) free(c->interfaces[i]);
    free(c->interfaces);
    for (uint32_t i = 0; i < c->static_field_count; i++) free_field(&c->static_fields[i]);
    free(c->static_fields);
    for (uint32_t i = 0; i < c->instance_field_count; i++) free_field(&c->instance_fields[i]);
    free(c->instance_fields);
    for (uint32_t i = 0; i < c->direct_method_count; i++) free_method(&c->direct_methods[i]);
    free(c->direct_methods);
    for (uint32_t i = 0; i < c->virtual_method_count; i++) free_method(&c->virtual_methods[i]);
    free(c->virtual_methods);
    for (uint32_t i = 0; i < c->annot_count; i++) smali_annot_free(&c->annots[i]);
    free(c->annots);
    for (uint32_t i = 0; i < c->field_annot_count; i++) smali_annot_free(&c->field_annots[i]);
    free(c->field_annots);
}

void smali_ctx_free(smali_ctx_def_t *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->class_count; i++) free_class(&ctx->classes[i]);
    free(ctx->classes);
    smali_pool_free(&ctx->strings);
    smali_pool_free(&ctx->types);
    smali_pool_free(&ctx->protos);
    smali_pool_free(&ctx->fields);
    smali_pool_free(&ctx->methods);
    memset(ctx, 0, sizeof(*ctx));
}
