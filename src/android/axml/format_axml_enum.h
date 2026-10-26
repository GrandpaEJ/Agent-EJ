#ifndef AXML_ENUM_H
#define AXML_ENUM_H

#include <stdint.h>

// Parses Android gravity flag strings ("top", "center", "top|left", ...) into
// the nibble-pair integer used by the gravity/layout_gravity attributes.
// Exact inverse of grav_nibble()/fmt_flags() in format_axml_output.c so that
// decode(encode(x)) == x for every name the decoder can produce.
// Returns 1 and stores the value on success; returns 0 if any token is
// unrecognized (caller falls through to generic value handling).
int axml_parse_gravity(const char *val, uint32_t *out_value);

#endif
