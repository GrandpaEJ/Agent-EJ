/* Simple resource values as text and back, for editing them in place:
 * colours (#rrggbb, #aarrggbb), dimensions (16dp, 1.5sp), booleans and
 * integers. Dimensions use aapt's complex encoding (24-bit mantissa, radix,
 * unit) so a value written here reads back exactly like one aapt wrote. */
#include "format_arsc_model.h"
#include "format_arsc_value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_DIMENSION 0x05u
#define TYPE_ARGB8 0x1cu

static const char *UNITS[] = {"px", "dp", "sp", "pt", "in", "mm"};

static int is_color(uint8_t t) { return (t >= 0x1c && t <= 0x1f); }

int arsc_value_editable(uint8_t type) {
    return type == ARSC_TYPE_STRING || type == ARSC_TYPE_INT_BOOLEAN || type == ARSC_TYPE_INT_DEC ||
           type == ARSC_TYPE_INT_HEX || type == TYPE_DIMENSION || is_color(type);
}

static double complex_value(uint32_t data) {
    static const double radix[4] = {1.0, 1.0 / (1 << 7), 1.0 / (1 << 15), 1.0 / (1 << 23)};
    return (double)(int32_t)(data & 0xFFFFFF00u) / 256.0 * radix[(data >> 4) & 3];
}

int arsc_value_format(uint8_t type, uint32_t data, char *out, size_t n) {
    switch (type) {
    case ARSC_TYPE_INT_BOOLEAN: return snprintf(out, n, "%s", data ? "true" : "false") > 0 ? 0 : -1;
    case ARSC_TYPE_INT_DEC: snprintf(out, n, "%d", (int32_t)data); return 0;
    case ARSC_TYPE_INT_HEX: snprintf(out, n, "0x%x", data); return 0;
    case TYPE_DIMENSION: {
        unsigned u = data & 0xF;
        if (u >= sizeof(UNITS) / sizeof(UNITS[0])) return -1;
        double v = complex_value(data);
        if (v > -1e9 && v < 1e9 && v == (double)(long long)v) snprintf(out, n, "%.0f%s", v, UNITS[u]);
        else {
            snprintf(out, n, "%.4f", v);
            char *e = out + strlen(out);                 /* trim trailing zeros */
            while (e > out && e[-1] == '0') *--e = '\0';
            snprintf(e, n - (size_t)(e - out), "%s", UNITS[u]);
        }
        return 0;
    }
    default:
        if (!is_color(type)) return -1;
        if (type == 0x1d || type == 0x1f) snprintf(out, n, "#%06x", data & 0xFFFFFFu);
        else snprintf(out, n, "#%08x", data);
        return 0;
    }
}

/* aapt's float → complex (ResourceUtils::floatToComplex). */
static uint32_t to_complex(double value, unsigned unit) {
    int neg = value < 0;
    if (neg) value = -value;
    uint64_t bits = (uint64_t)(value * (1 << 23) + .5);
    unsigned radix, shift;
    if ((bits & 0x7fffff) == 0) { radix = 0; shift = 23; }
    else if ((bits & 0xffffffffff800000ULL) == 0) { radix = 3; shift = 0; }
    else if ((bits & 0xffffffff80000000ULL) == 0) { radix = 2; shift = 8; }
    else if ((bits & 0xffffff8000000000ULL) == 0) { radix = 1; shift = 16; }
    else { radix = 0; shift = 23; }
    uint32_t mant = (uint32_t)((bits >> shift) & 0xFFFFFF);
    if (neg) mant = (uint32_t)(-(int32_t)mant) & 0xFFFFFF;
    return mant << 8 | radix << 4 | unit;
}

static int parse_color(const char *s, uint32_t *data) {
    size_t n = s[0] == '#' ? strlen(s + 1) : 0;
    if (n != 3 && n != 4 && n != 6 && n != 8) return -1;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[1 + i])) return -1;
    uint32_t v = (uint32_t)strtoul(s + 1, NULL, 16);
    if (n <= 4) {                                      /* #rgb / #argb: double each digit */
        uint32_t w = 0;
        for (int i = (int)n - 1; i >= 0; i--) { uint32_t d = (v >> (4 * i)) & 0xF; w = w << 8 | d << 4 | d; }
        v = w;
    }
    if (n == 3 || n == 6) v |= 0xFF000000u;
    *data = v;
    return 0;
}

int arsc_value_parse(uint8_t type, const char *s, uint8_t *out_type, uint32_t *data) {
    *out_type = type;
    if (is_color(type)) {
        if (parse_color(s, data) != 0) return -1;
        size_t n = strlen(s + 1);
        int alpha = n == 4 || n == 8;
        if (alpha && (type == 0x1d || type == 0x1f)) *out_type = TYPE_ARGB8;   /* alpha needs an argb type */
        return 0;
    }
    char *end = NULL;
    switch (type) {
    case ARSC_TYPE_INT_BOOLEAN:
        if (!strcmp(s, "true")) *data = 0xFFFFFFFFu;
        else if (!strcmp(s, "false")) *data = 0;
        else return -1;
        return 0;
    case ARSC_TYPE_INT_DEC:
    case ARSC_TYPE_INT_HEX: {
        long long v = strtoll(s, &end, 0);
        if (!s[0] || !end || *end || v < -2147483648LL || v > 4294967295LL) return -1;
        *data = (uint32_t)v;
        *out_type = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? ARSC_TYPE_INT_HEX : ARSC_TYPE_INT_DEC;
        return 0;
    }
    case TYPE_DIMENSION: {
        double v = strtod(s, &end);
        if (end == s || !end) return -1;
        const char *u = !strcmp(end, "dip") ? "dp" : end;
        for (unsigned i = 0; i < sizeof(UNITS) / sizeof(UNITS[0]); i++)
            if (!strcmp(u, UNITS[i])) { *data = to_complex(v, i); return 0; }
        return -1;
    }
    }
    return -1;
}
