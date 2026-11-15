#ifndef SMALI_STATICVAL_H
#define SMALI_STATICVAL_H

#include "smali_types.h"
#include "smali_buf.h"

/* Encodes the static_values encoded_array_item of a class into out (its
 * static fields must already be in field_id order). As in dexlib2's
 * StaticInitializerUtil, trailing default values are dropped; returns 0 and
 * writes nothing when every static field starts at its default value. */
int smali_static_values(smali_ctx_def_t *ctx, smali_class_def_t *c, smali_buf_t *out);

#endif
