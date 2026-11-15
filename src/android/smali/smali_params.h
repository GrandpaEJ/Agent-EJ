#ifndef SMALI_PARAMS_H
#define SMALI_PARAMS_H

#include <stdint.h>

/* Number of parameters (not registers) in a proto signature "(IJ)V". */
uint32_t smali_sig_param_count(const char *sig);

/* Maps a register offset inside the "in" window (p0 = 0) to the index of the
 * parameter it holds, skipping "this" for an instance method and counting
 * long/double as two registers. Returns -1 for "this", for the second half
 * of a wide parameter, or for a register past the parameter list. */
int smali_param_index(const char *sig, int is_static, uint32_t reg_off);

#endif
