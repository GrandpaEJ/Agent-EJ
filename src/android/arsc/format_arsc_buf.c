#include "format_arsc_buf.h"
#include <stdlib.h>
#include <string.h>

#define ARSC_BUF_MIN 4096

void arsc_buf_init(arsc_buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void arsc_buf_free(arsc_buf *b) {
    if (!b) return;
    free(b->data);
    arsc_buf_init(b);
}

int arsc_buf_reserve(arsc_buf *b, size_t extra) {
    if (!b) return -1;
    if (b->cap - b->len >= extra) return 0;
    size_t need = b->len + extra;
    size_t cap = b->cap ? b->cap : ARSC_BUF_MIN;
    while (cap < need) cap *= 2;
    uint8_t *p = realloc(b->data, cap);
    if (!p) return -1;
    memset(p + b->len, 0, cap - b->len);
    b->data = p;
    b->cap = cap;
    return 0;
}

int arsc_buf_append(arsc_buf *b, const void *data, size_t len) {
    if (!b || !data) return -1;
    if (arsc_buf_reserve(b, len) != 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

int arsc_buf_zero(arsc_buf *b, size_t len) {
    if (!b) return -1;
    if (arsc_buf_reserve(b, len) != 0) return -1;
    memset(b->data + b->len, 0, len);
    b->len += len;
    return 0;
}

int arsc_buf_u8(arsc_buf *b, uint8_t v) {
    return arsc_buf_append(b, &v, 1);
}

int arsc_buf_u16(arsc_buf *b, uint16_t v) {
    uint8_t t[2];
    t[0] = (uint8_t)(v & 0xFF);
    t[1] = (uint8_t)((v >> 8) & 0xFF);
    return arsc_buf_append(b, t, 2);
}

int arsc_buf_u32(arsc_buf *b, uint32_t v) {
    uint8_t t[4];
    t[0] = (uint8_t)(v & 0xFF);
    t[1] = (uint8_t)((v >> 8) & 0xFF);
    t[2] = (uint8_t)((v >> 16) & 0xFF);
    t[3] = (uint8_t)((v >> 24) & 0xFF);
    return arsc_buf_append(b, t, 4);
}

int arsc_buf_patch32(arsc_buf *b, size_t off, uint32_t v) {
    if (!b || off + 4 > b->len) return -1;
    b->data[off + 0] = (uint8_t)(v & 0xFF);
    b->data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b->data[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b->data[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    return 0;
}

int arsc_buf_align4(arsc_buf *b) {
    if (!b) return -1;
    size_t pad = (4 - (b->len & 3)) & 3;
    if (!pad) return 0;
    return arsc_buf_zero(b, pad);
}
