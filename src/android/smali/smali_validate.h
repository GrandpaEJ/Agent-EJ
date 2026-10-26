#ifndef SMALI_VALIDATE_H
#define SMALI_VALIDATE_H

#include "smali_types.h"

/* Validates the register operands of one instruction against the DEX
 * bytecode format limits (dalvik bytecode format table, formats 10x..51l).
 *
 * vA/vB/vC are the resolved register numbers (p-registers already mapped to
 * their absolute index). For F_35c (fmt 14) the argument registers live in
 * ins->regs[], so they must be resolved by the caller and passed in
 * invoke_regs (5 entries); every other format passes NULL.
 *
 * Returns 0 when the instruction is encodable. On failure an error is
 * recorded (smali_last_error()) and -1 is returned; the caller must abort
 * the encoding instead of truncating the operand. */
int smali_validate_insn(const smali_method_def_t *m, const smali_insn_t *ins,
                        uint32_t vA, uint32_t vB, uint32_t vC,
                        const uint32_t *invoke_regs);

#endif
