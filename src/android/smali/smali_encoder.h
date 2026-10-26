#ifndef SMALI_ENCODER_H
#define SMALI_ENCODER_H

#include <stdint.h>
#include "smali_types.h"

uint32_t smali_encode_method_insns(smali_ctx_def_t *ctx, smali_method_def_t *m, uint16_t *out_buf);

/* Fills out[0..insns_count] with each instruction's code-unit offset (payloads 4-byte aligned). */
void smali_compute_insn_offsets(smali_method_def_t *m, uint32_t *out);

#endif
