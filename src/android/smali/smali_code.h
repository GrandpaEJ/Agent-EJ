#ifndef SMALI_CODE_H
#define SMALI_CODE_H

#include "smali_types.h"
#include "smali_buf.h"

/* Writes the code_item for m at the current buffer position.
 * Returns 0 and stores the item's file offset in *out_offset on success.
 * Abstract/native methods and methods without instructions store 0.
 * Returns -1 when the method's instructions fail to encode (the reason is
 * recorded via smali_last_error()); the buffer contents are then undefined. */
int write_code_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m, uint32_t *out_offset);

#endif
