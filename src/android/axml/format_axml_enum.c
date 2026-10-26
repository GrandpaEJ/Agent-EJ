#include "format_axml_enum.h"
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
