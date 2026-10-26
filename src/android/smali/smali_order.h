#ifndef SMALI_ORDER_H
#define SMALI_ORDER_H

#include "smali_types.h"

/* Sorts classes by descriptor, then so every superclass/interface that lives in the
 * same set precedes the classes that use it (a DEX class_defs requirement). */
void smali_order_classes(smali_ctx_def_t *ctx);

/* Returns the end index (exclusive) of the largest run of classes starting at `start`
 * whose method/field/type references still fit one DEX (65536-entry id tables). */
uint32_t smali_next_dex_group(smali_ctx_def_t *ctx, uint32_t start);

/* Names part N (0-based) of a multi-dex output: part 0 is out_dex itself, then
 * classes.dex -> classes2.dex, classes3.dex ... (or out.dex -> out2.dex ...). */
void smali_dex_part_name(const char *out_dex, uint32_t part, char *buf, size_t sz);

#endif
