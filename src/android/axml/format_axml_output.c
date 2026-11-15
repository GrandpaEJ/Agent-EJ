#include "format_axml_internal.h"
#include "format_axml_enum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Typed attribute values (refs, dimensions, enums, flags, colors) as text. */
typedef struct { const char *attr; int32_t val; const char *name; int is_flag; } ae_t;
static const ae_t android_enums[] = {
    {"orientation", 0, "horizontal"}, {"orientation", 1, "vertical"},
    {"launchMode", 0, "standard"}, {"launchMode", 1, "singleTop"},
    {"launchMode", 2, "singleTask"}, {"launchMode", 3, "singleInstance"},
    {"screenOrientation", 0, "unspecified"}, {"screenOrientation", 1, "portrait"},
    {"screenOrientation", 2, "landscape"}, {"screenOrientation", 3, "user"},
    {"screenOrientation", 4, "behind"}, {"screenOrientation", 5, "sensor"},
    {"screenOrientation", 6, "nosensor"}, {"screenOrientation", 7, "sensorLandscape"},
    {"screenOrientation", 8, "sensorPortrait"}, {"screenOrientation", 9, "reverseLandscape"},
    {"screenOrientation", 10, "reversePortrait"}, {"screenOrientation", 11, "fullSensor"},
    {"screenOrientation", 12, "userLandscape"}, {"screenOrientation", 13, "userPortrait"},
    {"screenOrientation", 14, "locked"},
    {"configChanges", 0x0020, "keyboardHidden", 1}, {"configChanges", 0x0080, "orientation", 1},
    {"configChanges", 0x0100, "screenLayout", 1}, {"configChanges", 0x0200, "uiMode", 1},
    {"configChanges", 0x0400, "screenSize", 1}, {"configChanges", 0x0800, "smallestScreenSize", 1},
    {"configChanges", 0x0001, "mcc", 1}, {"configChanges", 0x0002, "mnc", 1},
    {"configChanges", 0x0004, "locale", 1}, {"configChanges", 0x0008, "touchscreen", 1},
    {"configChanges", 0x0010, "keyboard", 1}, {"configChanges", 0x0040, "navigation", 1},
    {"configChanges", 0x1000, "fontScale", 1},
    {"visibility", 0, "visible"}, {"visibility", 1, "invisible"}, {"visibility", 2, "gone"},
    {"importantForAccessibility", 0, "auto"}, {"importantForAccessibility", 1, "yes"},
    {"importantForAccessibility", 2, "no"}, {"importantForAccessibility", 4, "noHideDescendants"},
    {"scrollbars", 0x100, "vertical"}, {"scrollbars", 0x200, "horizontal"},
};

// Nibble names for gravity (horizontal nibble [3:0], vertical nibble [7:4])
static const char* grav_nibble(uint8_t nib, int is_vert) {
    switch (nib) {
        case 0x00: return NULL; // no gravity = 0 on that axis
        case 0x01: return is_vert ? NULL : "center_horizontal";
        case 0x03: return is_vert ? NULL : "left";
        case 0x05: return is_vert ? NULL : "right";
        case 0x07: return is_vert ? NULL : "fill_horizontal";
        case 0x08: return "clip_horizontal";
        case 0x10: return is_vert ? "center_vertical" : NULL;
        case 0x30: return is_vert ? "top" : NULL;
        case 0x50: return is_vert ? "bottom" : NULL;
        case 0x70: return is_vert ? "fill_vertical" : NULL;
        case 0x80: return "clip_vertical";
        default: return NULL;
    }
}

// Decompose compound flag value supporting both bitmask and gravity nibble-pair
static const char* fmt_flags(const char *an, int32_t val) {
    static char buf[128];
    buf[0] = '\0';
    if (!an || val == 0) return NULL;
    
    // Special case: gravity/layout_gravity use nibble-pair encoding
    if ((strcmp(an, "gravity") == 0 || strcmp(an, "layout_gravity") == 0)) {
        uint8_t h = val & 0x0F, v = val & 0xF0;
        // Check for combined constants: center=0x11, fill=0x77
        if (val == 0x11) return "center";
        if (val == 0x77) return "fill";
        const char *hn = grav_nibble(h, 0), *vn = grav_nibble(v, 1);
        if (vn && hn) { snprintf(buf, sizeof(buf), "%s|%s", vn, hn); return buf; }
        if (vn) { snprintf(buf, sizeof(buf), "%s", vn); return buf; }
        if (hn) { snprintf(buf, sizeof(buf), "%s", hn); return buf; }
        return NULL;
    }
    
    // Bitmask flags (configChanges, etc.)
    int first = 1;
    for (size_t i = 0; i < sizeof(android_enums)/sizeof(android_enums[0]); i++) {
        if (!android_enums[i].is_flag) continue;
        if (strcmp(an, android_enums[i].attr) != 0) continue;
        int32_t fv = android_enums[i].val;
        if (fv != 0 && (val & fv) == fv) {
            if (!first) strcat(buf, "|");
            strcat(buf, android_enums[i].name);
            first = 0; val &= ~fv;
        }
    }
    if (!first && val == 0) return buf;
    return NULL;
}

const char *axml_fmt_typed(uint8_t type, uint32_t data, int raw_idx, const char *an, const arsc_ctx *arsc, uint32_t attr_res_id) {
    static char buf[1024]; /* resource names can be long (m3_comp_..._state_layer_opacity) */
    if (type == 0x03 || (type == 0x01 && raw_idx >= 0)) return NULL;
    if (type == 0x01) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "@%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "@%08x", data);
        return buf;
    }
    if (type == 0x02) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "?%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "?%08x", data);
        return buf;
    }
    if (type == 0x04) { float f; memcpy(&f, &data, 4); axml_fmt_float(buf, sizeof(buf), f); return buf; }
    if (type == 0x05 || type == 0x06) {
        axml_fmt_complex(buf, sizeof(buf), data, type == 0x06);
        return buf;
    }
    if (type == 0x10) {
        int32_t s = (int32_t)data;
        if (s == -1 && an && (strstr(an, "width") || strstr(an, "height"))) return "match_parent";
        if (s == -2 && an && (strstr(an, "width") || strstr(an, "height"))) return "wrap_content";
        if (s == -1) return "fill_parent";
        if (an && arsc) {
            // For non-layout attrs, use the enum table
            (void)arsc;
        }
        if (an) {
            // Framework attrs: SDK table, the exact inverse of the encoder's
            const char *fw = axml_fw_enum_name(attr_res_id, data);
            if (fw) return fw;
            // Dynamic ARSC lookup (app attrs)
            if (arsc && attr_res_id != 0xFFFFFFFF) {
                const char *dyn = arsc_attr_enum((arsc_ctx*)arsc, attr_res_id, s);
                if (dyn) return dyn;
            }
            // Check non-flag enums next (exact match from hardcoded table)
            for (size_t i = 0; i < sizeof(android_enums)/sizeof(android_enums[0]); i++) {
                if (!android_enums[i].is_flag && strcmp(an, android_enums[i].attr) == 0 && s == android_enums[i].val)
                    return android_enums[i].name;
            }
            // Try flag decomposition
            const char *ff = fmt_flags(an, s);
            if (ff) return ff;
        }
        snprintf(buf, sizeof(buf), "%d", s);
        return buf;
    }
    if (type == 0x12) return data ? "true" : "false";
    /* Colors keep their written width so the encoder picks the same type:
     * 0x1c #aarrggbb, 0x1d #rrggbb, 0x1e #argb, 0x1f #rgb. */
    if (type == 0x1c) { snprintf(buf, sizeof(buf), "#%08x", data); return buf; }
    if (type == 0x1d) { snprintf(buf, sizeof(buf), "#%06x", data & 0xFFFFFF); return buf; }
    if (type == 0x1e) {
        snprintf(buf, sizeof(buf), "#%x%x%x%x", (data >> 28) & 0xF, (data >> 20) & 0xF,
                 (data >> 12) & 0xF, (data >> 4) & 0xF);
        return buf;
    }
    if (type == 0x1f) {
        snprintf(buf, sizeof(buf), "#%x%x%x", (data >> 20) & 0xF, (data >> 12) & 0xF, (data >> 4) & 0xF);
        return buf;
    }
    if (type == 0x11) {
        if (an) {
            const char *fw = axml_fw_enum_name(attr_res_id, data);
            if (fw) return fw;
            // Dynamic ARSC lookup
            if (arsc && attr_res_id != 0xFFFFFFFF) {
                const char *dyn = arsc_attr_enum((arsc_ctx*)arsc, attr_res_id, (int32_t)data);
                if (dyn) return dyn;
            }
            const char *ff = fmt_flags(an, (int32_t)data);
            if (ff) return ff;
        }
        snprintf(buf, sizeof(buf), "0x%x", data);
        return buf;
    }
    snprintf(buf, sizeof(buf), "0x%x", data);
    return buf;
}
