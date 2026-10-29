#include "format_axml_enum.h"
#include "axml_fw_attrs.h"
#include <stdio.h>
#include <string.h>

/* Framework (android:*) attribute lookups over the SDK-generated tables in
 * axml_fw_attrs.h. Decoder and encoder both go through here, so every
 * enum/flag name the decoder prints maps back to the same integer. */

static int fw_index(uint32_t id) {
    if (id < AXML_FW_ATTR_BASE || id >= AXML_FW_ATTR_BASE + AXML_FW_ATTR_COUNT) return -1;
    return (int)(id - AXML_FW_ATTR_BASE);
}

uint32_t axml_fw_attr_id(const char *name) {
    if (!name || !*name) return 0;
    const char *p = axml_fw_attr_names;
    for (int i = 0; i < AXML_FW_ATTR_COUNT; i++) {
        if (strcmp(p, name) == 0) return AXML_FW_ATTR_BASE + (uint32_t)i;
        p += strlen(p) + 1;
    }
    return 0;
}

int axml_fw_attr_fmt(uint32_t id) {
    int i = fw_index(id);
    return i < 0 ? 0 : axml_fw_attr_fmts[i];
}

const char *axml_fw_enum_name(uint32_t id, uint32_t value) {
    static char buf[512];
    int idx = fw_index(id);
    if (idx < 0) return NULL;
    int has_flags = 0;
    const char *nm = axml_fw_enum_names;
    for (int k = 0; k < AXML_FW_ENUM_COUNT; nm += strlen(nm) + 1, k++) {
        if ((axml_fw_enum_attr[k] & 0x7FFF) != idx) continue;
        if (axml_fw_enum_attr[k] & 0x8000) has_flags = 1;
        else if (axml_fw_enum_val[k] == value) return nm;
    }
    if (!has_flags) return NULL;

    /* Flags are stored largest-first, so greedy decomposition prefers
     * multi-bit masks (adjustNothing=0x30 over adjustPan=0x20); names are
     * only emitted when they OR back to exactly the stored value. */
    uint32_t rem = value;
    size_t len = 0;
    buf[0] = '\0';
    nm = axml_fw_enum_names;
    for (int k = 0; k < AXML_FW_ENUM_COUNT; nm += strlen(nm) + 1, k++) {
        if ((axml_fw_enum_attr[k] & 0x7FFF) != idx || !(axml_fw_enum_attr[k] & 0x8000)) continue;
        uint32_t fv = axml_fw_enum_val[k];
        if (fv ? (rem & fv) != fv : (value != 0 || len)) continue;
        int n = snprintf(buf + len, sizeof(buf) - len, "%s%s", len ? "|" : "", nm);
        if (n < 0 || (size_t)n >= sizeof(buf) - len) return NULL;
        len += (size_t)n;
        rem &= ~fv;
    }
    return (len && rem == 0) ? buf : NULL;
}

int axml_fw_enum_value(uint32_t id, const char *text, uint32_t *out, int *is_flag) {
    int idx = fw_index(id);
    if (idx < 0 || !text || !*text) return 0;
    uint32_t acc = 0;
    int flag = 0;
    for (const char *p = text;;) {
        const char *sep = strchr(p, '|');
        size_t tl = sep ? (size_t)(sep - p) : strlen(p);
        int found = 0;
        const char *nm = axml_fw_enum_names;
        for (int k = 0; k < AXML_FW_ENUM_COUNT && !found; nm += strlen(nm) + 1, k++) {
            if ((axml_fw_enum_attr[k] & 0x7FFF) != idx) continue;
            if (strlen(nm) != tl || strncmp(nm, p, tl) != 0) continue;
            acc |= axml_fw_enum_val[k];
            flag |= (axml_fw_enum_attr[k] & 0x8000) != 0;
            found = 1;
        }
        if (!found) return 0;
        if (!sep) break;
        p = sep + 1;
    }
    *out = acc;
    if (is_flag) *is_flag = flag;
    return 1;
}
