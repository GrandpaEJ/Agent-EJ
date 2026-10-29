#include "format_axml_enum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; uint32_t flag; } grav_entry;

// Gravity flag names -> Android Gravity bit values.
// Must stay the exact inverse of grav_nibble()/fmt_flags() in
// format_axml_output.c (horizontal nibble = bits [3:0], vertical = bits [7:4]).
static const grav_entry grav_table[] = {
    {"center",            0x11},
    {"fill",              0x77},
    {"center_horizontal", 0x01},
    {"left",              0x03},
    {"right",             0x05},
    {"fill_horizontal",   0x07},
    {"clip_horizontal",   0x08},
    {"center_vertical",   0x10},
    {"top",               0x30},
    {"bottom",            0x50},
    {"fill_vertical",     0x70},
    {"clip_vertical",     0x80},
};

int axml_parse_gravity(const char *val, uint32_t *out_value) {
    if (!val || val[0] == '\0' || !out_value) return 0;
    uint32_t acc = 0;
    const char *p = val;
    for (;;) {
        const char *sep = strchr(p, '|');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        int matched = 0;
        for (size_t i = 0; i < sizeof(grav_table) / sizeof(grav_table[0]); i++) {
            if (strlen(grav_table[i].name) == len &&
                strncmp(grav_table[i].name, p, len) == 0) {
                acc |= grav_table[i].flag;
                matched = 1;
                break;
            }
        }
        if (!matched) return 0; // Unknown token: reject so caller falls through
        if (!sep) break;
        p = sep + 1;
    }
    *out_value = acc;
    return 1;
}

/* Complex values (dimension/fraction): 24-bit signed mantissa scaled by
 * 2^0, 2^-7, 2^-15 or 2^-23 (radix 0..3), unit in the low nibble. */
static const float complex_mult[4] = {1.0f, 1.0f / (1 << 7), 1.0f / (1 << 15), 1.0f / (1 << 23)};

/* aapt2's floatToComplex(), so re-encoded values match what aapt wrote. */
static uint32_t float_to_complex(float value) {
    int neg = value < 0;
    if (neg) value = -value;
    uint64_t bits = (uint64_t)(value * (float)(1 << 23) + 0.5f);
    int radix, shift;
    if ((bits & 0x7fffff) == 0) { radix = 0; shift = 23; }
    else if ((bits & 0xffffffffff800000ULL) == 0) { radix = 3; shift = 0; }
    else if ((bits & 0xffffffff80000000ULL) == 0) { radix = 2; shift = 8; }
    else if ((bits & 0xffffff8000000000ULL) == 0) { radix = 1; shift = 16; }
    else { radix = 0; shift = 23; }
    int32_t mantissa = (int32_t)((bits >> shift) & 0xffffff);
    if (neg) mantissa = (-mantissa) & 0xffffff;
    return ((uint32_t)radix << 4) | ((uint32_t)mantissa << 8);
}

static const char *const dim_units[] = {"px", "dp", "sp", "pt", "in", "mm"};

void axml_fmt_complex(char *buf, size_t sz, uint32_t data, int fraction) {
    float v = (float)((int32_t)data >> 8) * complex_mult[(data >> 4) & 3];
    unsigned unit = data & 0xF;
    float scale = fraction ? 100.0f : 1.0f;
    const char *suffix = fraction ? (unit ? "%p" : "%") : (unit < 6 ? dim_units[unit] : "px");
    /* Shortest text that encodes back to the identical bits. */
    char num[32];
    for (int prec = 1; prec <= 9; prec++) {
        snprintf(num, sizeof(num), "%.*g", prec, v * scale);
        if (float_to_complex(strtof(num, NULL) / scale) == (data & ~0xFu)) break;
    }
    if (!strpbrk(num, ".e")) strcat(num, ".0");
    snprintf(buf, sz, "%s%s", num, suffix);
}

int axml_parse_complex(const char *val, uint32_t *out_data, int *is_fraction) {
    char *end = NULL;
    float num = strtof(val, &end);
    if (!end || end == val) return 0;
    while (*end == ' ' || *end == '\t') end++;
    int unit = -1, frac = 0;
    if (strcmp(end, "%") == 0) { unit = 0; frac = 1; }
    else if (strcmp(end, "%p") == 0) { unit = 1; frac = 1; }
    else if (strcmp(end, "dip") == 0) unit = 1;
    else for (int i = 0; i < 6; i++) if (strcmp(end, dim_units[i]) == 0) unit = i;
    if (unit < 0) return 0;
    *out_data = float_to_complex(frac ? num / 100.0f : num) | (uint32_t)unit;
    *is_fraction = frac;
    return 1;
}

void axml_fmt_float(char *buf, size_t sz, float f) {
    for (int prec = 1; prec <= 9; prec++) {
        snprintf(buf, sz, "%.*g", prec, f);
        if (strtof(buf, NULL) == f) break;
    }
    /* Keep a decimal point so the encoder reads it back as a float, not an
     * int (layout_weight="1" would otherwise lose its float type). */
    if (!strpbrk(buf, ".eni")) strncat(buf, ".0", sz - strlen(buf) - 1);
}
