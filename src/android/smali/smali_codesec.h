#ifndef SMALI_CODESEC_H
#define SMALI_CODESEC_H

#include "smali_types.h"
#include "smali_buf.h"

/* debug_info, code and class_data sections in smali.jar's layout. */
typedef struct {
    uint32_t **code_off;      /* per class, per method (direct then virtual) */
    uint32_t debug_first, debug_count;
    uint32_t code_first, code_count;
    uint32_t *class_data_off; /* per class, 0 for a class without members */
    uint32_t class_data_first, class_data_count;
} smali_codesec_t;

/* Writes every debug_info_item (identical ones shared), then every code_item,
 * walking classes in `order` (sorted by type) and their direct then virtual
 * methods. Returns -1 when an instruction failed to encode: every method is
 * still attempted so all errors get reported. */
int smali_codesec_write(smali_codesec_t *cs, smali_ctx_def_t *ctx, smali_buf_t *b,
                        const uint32_t *order);

/* Writes the class_data_items in class_defs order; a class without fields
 * and methods gets none. */
int smali_classdata_write(smali_codesec_t *cs, smali_ctx_def_t *ctx, smali_buf_t *b);

void smali_codesec_free(smali_codesec_t *cs, uint32_t class_count);

#endif
