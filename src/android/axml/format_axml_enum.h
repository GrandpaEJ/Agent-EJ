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

// Dimension/fraction complex values. Formatting prints the shortest text
// that axml_parse_complex() encodes back to the identical bits.
#include <stddef.h>
void axml_fmt_complex(char *buf, size_t sz, uint32_t data, int fraction);
int axml_parse_complex(const char *val, uint32_t *out_data, int *is_fraction);
// Shortest round-tripping float text, always with a decimal point.
void axml_fmt_float(char *buf, size_t sz, float f);

// Framework android:* attributes (tables generated from the SDK; see
// scripts/gen_axml_fw_attrs.py). Binary XML identifies these by resource ID.
#define AXML_ANDROID_NS "http://schemas.android.com/apk/res/android"
// Declared value formats (attrs.xml format="..."); must match FMT_BITS in the generator.
#define AXML_FMT_STRING    1
#define AXML_FMT_INTEGER   2
#define AXML_FMT_BOOLEAN   4
#define AXML_FMT_COLOR     8
#define AXML_FMT_FLOAT     16
#define AXML_FMT_DIMENSION 32
#define AXML_FMT_FRACTION  64
#define AXML_FMT_REFERENCE 128
uint32_t axml_fw_attr_id(const char *name);       // 0 if not a public attr
int axml_fw_attr_fmt(uint32_t id);                // AXML_FMT_* bits, 0 if unknown
// Enum name, or flag names joined by '|', for a stored value; NULL if the
// value has no exact symbolic form. Points to a static buffer.
const char *axml_fw_enum_name(uint32_t id, uint32_t value);
// Inverse of axml_fw_enum_name(): 1 and *out on success, 0 if any token is
// not one of this attr's enum/flag names.
int axml_fw_enum_value(uint32_t id, const char *text, uint32_t *out, int *is_flag);

#endif
