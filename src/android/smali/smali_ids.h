#ifndef SMALI_IDS_H
#define SMALI_IDS_H

#include "smali_types.h"
#include "smali_buf.h"

/* Offsets of the index sections, which follow the 0x70-byte header. */
typedef struct {
    uint32_t string_ids, type_ids, proto_ids, field_ids, method_ids, class_defs, data;
} smali_id_layout_t;

void smali_id_layout(const smali_ctx_def_t *ctx, smali_id_layout_t *l);

/* Writes every string_data_item (MUTF-8, in string_ids order) at the end of
 * b and fills string_ids. Returns the offset of the first one. */
uint32_t smali_write_string_data(smali_ctx_def_t *ctx, smali_buf_t *b, const smali_id_layout_t *l);

/* Fills type_ids, proto_ids (proto_params: per proto type_list offset),
 * field_ids and method_ids. */
void smali_write_ids(smali_ctx_def_t *ctx, smali_buf_t *b, const smali_id_layout_t *l,
                     const uint32_t *proto_params);

#endif
