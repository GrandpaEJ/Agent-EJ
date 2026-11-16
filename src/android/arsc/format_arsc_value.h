#ifndef FORMAT_ARSC_VALUE_H
#define FORMAT_ARSC_VALUE_H

#include <stddef.h>
#include <stdint.h>

/* Value types arsc_value_format/parse handle (plus strings, done by the caller). */
int arsc_value_editable(uint8_t type);

/* Text form of a simple value: "true", "12", "0x1f", "16dp", "#ff2196f3". 0 or -1. */
int arsc_value_format(uint8_t type, uint32_t data, char *out, size_t n);

/* Reads text for an entry of [type]. *out_type may differ (a colour that gains
 * alpha becomes ARGB8; "0x.." integers become hex). 0, or -1 when unreadable. */
int arsc_value_parse(uint8_t type, const char *s, uint8_t *out_type, uint32_t *data);

#endif
