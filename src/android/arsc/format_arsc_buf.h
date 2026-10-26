#ifndef FORMAT_ARSC_BUF_H
#define FORMAT_ARSC_BUF_H

#include <stddef.h>
#include <stdint.h>

/* Growable byte buffer used by the resource table writer. */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} arsc_buf;

void arsc_buf_init(arsc_buf *b);
void arsc_buf_free(arsc_buf *b);
/* Makes room for `extra` more bytes. Returns 0 on success, -1 on failure. */
int arsc_buf_reserve(arsc_buf *b, size_t extra);
int arsc_buf_append(arsc_buf *b, const void *data, size_t len);
int arsc_buf_zero(arsc_buf *b, size_t len);
int arsc_buf_u8(arsc_buf *b, uint8_t v);
int arsc_buf_u16(arsc_buf *b, uint16_t v);
int arsc_buf_u32(arsc_buf *b, uint32_t v);
/* Overwrites the 4 bytes at `off`. The caller must have written them before. */
int arsc_buf_patch32(arsc_buf *b, size_t off, uint32_t v);
/* Pads with zeros until the length is a multiple of 4. */
int arsc_buf_align4(arsc_buf *b);

#endif
