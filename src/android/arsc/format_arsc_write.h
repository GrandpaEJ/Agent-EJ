#ifndef FORMAT_ARSC_WRITE_H
#define FORMAT_ARSC_WRITE_H

#include "format_arsc_model.h"
#include "format_arsc_buf.h"

/* Writes a string pool chunk. `strings` holds `count` UTF-8 strings. */
int arsc_write_pool(arsc_buf *b, char *const *strings, uint32_t count, int utf8);
/* Writes a ResTable_typeSpec chunk for the type. */
int arsc_write_type_spec(arsc_buf *b, const arsc_model_type *t);
/* Writes a ResTable_type chunk for one configuration of the type. */
int arsc_write_type(arsc_buf *b, const arsc_model_type *t, const arsc_model_variant *v);
/* Serialises the whole model into a resources.arsc image. */
uint8_t *arsc_encode(const arsc_model *m, size_t *out_size);

#endif
