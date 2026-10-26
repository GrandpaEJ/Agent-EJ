#include "smali_order.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *desc; uint32_t idx; } class_key_t;

static int cmp_key(const void *a, const void *b) {
    return strcmp(((const class_key_t *)a)->desc, ((const class_key_t *)b)->desc);
}

static int find_class(const class_key_t *keys, uint32_t n, const char *desc) {
    if (!desc) return -1;
    class_key_t k = { desc, 0 };
    const class_key_t *r = bsearch(&k, keys, n, sizeof(k), cmp_key);
    return r ? (int)r->idx : -1;
}

typedef struct {
    smali_class_def_t *classes;
    const class_key_t *keys;
    uint32_t n;
    uint8_t *state;
    uint32_t *order;
    uint32_t out;
} order_ctx_t;

static void visit(order_ctx_t *o, uint32_t i) {
    if (o->state[i]) return;                 /* done, or a cycle: keep going */
    o->state[i] = 1;
    smali_class_def_t *c = &o->classes[i];
    int s = find_class(o->keys, o->n, c->super_class);
    if (s >= 0) visit(o, (uint32_t)s);
    for (uint32_t k = 0; k < c->interface_count; k++) {
        int d = find_class(o->keys, o->n, c->interfaces[k]);
        if (d >= 0) visit(o, (uint32_t)d);
    }
    o->order[o->out++] = i;
}

void smali_order_classes(smali_ctx_def_t *ctx) {
    uint32_t n = ctx->class_count;
    if (n < 2) return;
    class_key_t *keys = malloc(n * sizeof(*keys));
    uint8_t *state = calloc(n, 1);
    uint32_t *order = malloc(n * sizeof(*order));
    smali_class_def_t *tmp = malloc(n * sizeof(*tmp));
    if (!keys || !state || !order || !tmp) { free(keys); free(state); free(order); free(tmp); return; }
    for (uint32_t i = 0; i < n; i++) { keys[i].desc = ctx->classes[i].descriptor ? ctx->classes[i].descriptor : ""; keys[i].idx = i; }
    qsort(keys, n, sizeof(*keys), cmp_key);

    order_ctx_t o = { ctx->classes, keys, n, state, order, 0 };
    for (uint32_t k = 0; k < n; k++) visit(&o, keys[k].idx);

    for (uint32_t k = 0; k < n; k++) tmp[k] = ctx->classes[order[k]];
    memcpy(ctx->classes, tmp, n * sizeof(*tmp));
    free(keys); free(state); free(order); free(tmp);
}
