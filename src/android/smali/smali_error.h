#ifndef SMALI_ERROR_H
#define SMALI_ERROR_H

#include <stdint.h>

/* Assembler diagnostics.
 *
 * The smali assembler runs inside the tool layer, which must answer every
 * request with JSON. Library code therefore never aborts the process: on
 * failure it records a message here (and mirrors it to stderr), the call
 * chain propagates a return code, and the tool layer reads
 * smali_last_error() to include the reason in its JSON response.
 *
 * Single-threaded by design: one global slot, matching s_sort_ctx in
 * smali_pool.c. */

/* Records fmt... as the last error and returns -1 for use at call sites. */
int smali_fail(const char *fmt, ...);

/* Records "file:line: fmt..." as the last error and returns -1. The file is
 * the source name set with smali_set_source_name(), the line is the 1-based
 * source line of the offending construct. */
int smali_fail_at(uint32_t line, const char *fmt, ...);

/* Records fmt... without printing; the caller decides how to report it. */
void smali_set_error(const char *fmt, ...);

/* Last recorded error message ("" when no error was recorded). */
const char *smali_last_error(void);

/* Clears the last recorded error (called at the start of each assembly). */
void smali_clear_error(void);

/* Source name used by parser diagnostics (the .smali file path). */
void smali_set_source_name(const char *name);
const char *smali_source_name(void);

#endif
