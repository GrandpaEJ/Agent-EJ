#ifndef SMALI_ENCODER_H
#define SMALI_ENCODER_H

#include <stdint.h>
#include "smali_types.h"

/* Encodes m's instructions into out_buf and stores the number of code units
 * in *out_words. Returns 0 on success. On failure (register operand outside
 * its format limit, unresolvable reference) returns -1 and records the
 * reason via smali_last_error(); out_buf contents are then undefined. */
int smali_encode_method_insns(smali_ctx_def_t *ctx, smali_method_def_t *m, uint16_t *out_buf, uint32_t *out_words);

/* Fills out[0..insns_count] with each instruction's code-unit offset (payloads 4-byte aligned). */
void smali_compute_insn_offsets(smali_method_def_t *m, uint32_t *out);

#endif
