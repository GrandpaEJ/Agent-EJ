#include "format_axml_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static char *escape_xml(const char *s) {
    if (!s) return strdup("");
    size_t cap = strlen(s) * 2 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t pos = 0;
    for (const char *p = s; *p; p++) {
        if (pos + 6 >= cap) {
            cap *= 2;
            char *n = realloc(out, cap);
            if (!n) { free(out); return NULL; }
            out = n;
        }
        switch (*p) {
        case '&': memcpy(out + pos, "&amp;", 5); pos += 5; break;
        case '<': memcpy(out + pos, "&lt;", 4); pos += 4; break;
        case '>': memcpy(out + pos, "&gt;", 4); pos += 4; break;
        case '"': memcpy(out + pos, "&quot;", 6); pos += 6; break;
        case '\'': memcpy(out + pos, "&apos;", 6); pos += 6; break;
        default: out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return out;
}

static int append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t slen = strlen(s);
    if (*len + slen + 1 > *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
    return 0;
}

static int append_fmt(char **buf, size_t *len, size_t *cap,
                      const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return -1;
    if (*len + (size_t)needed + 1 > *cap) {
        while (*cap < *len + (size_t)needed + 1)
            *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += needed;
    return 0;
}

static void indent(char **buf, size_t *len, size_t *cap, int depth) {
    for (int i = 0; i < depth; i++)
        append(buf, len, cap, "    ");
}

static const char *get_str(axml_ctx *ctx, int idx) {
    if (idx < 0 || idx >= ctx->pool.count) return NULL;
    return ctx->pool.strings[idx];
}

static const char *get_ns_prefix(const char *uri) {
    if (!uri) return NULL;
    if (strstr(uri, "android")) return "android";
    return NULL;
}

// Hardcoded android enum/flag values for common attributes
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

static const char* fmt_typed(uint8_t type, uint32_t data, int raw_idx, const char *an, const arsc_ctx *arsc, uint32_t attr_res_id) {
    static char buf[64];
    if (type == 0x03 || (type == 0x01 && raw_idx >= 0)) return NULL;
    if (type == 0x01) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "@%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "@ref/0x%08x", data);
        return buf;
    }
    if (type == 0x02) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "?%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "?ref/0x%08x", data);
        return buf;
    }
    if (type == 0x04) { float f; memcpy(&f, &data, 4); snprintf(buf, sizeof(buf), "%g", f); return buf; }
    if (type == 0x05) {
        uint32_t m = data >> 8, r = (data >> 4) & 3, u = data & 0xf;
        float v = (float)m / (float)(1 << (r * 2));
        const char *us = (const char*[]){"px","dp","sp","pt","in","mm"}[u < 6 ? u : 0];
        snprintf(buf, sizeof(buf), v == (float)(int)v ? "%.1f%s" : "%g%s", v, us);
        return buf;
    }
    if (type == 0x06) {
        uint32_t m = data >> 8, r = (data >> 4) & 3, u = data & 0xf;
        float v = (float)m / (float)(1 << (r * 2));
        snprintf(buf, sizeof(buf), "%g%s", v, u ? "%p" : "%");
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
            // Dynamic ARSC lookup first (covers ALL framework + app attrs)
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
    if (type >= 0x13 && type <= 0x16) { snprintf(buf, sizeof(buf), "#%08x", data); return buf; }
    if (type == 0x11) {
        if (an) {
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

char *axml_get_xml(axml_ctx *ctx) {
    if (!ctx) return NULL;
    if (ctx->xml) return ctx->xml;

    size_t cap = 4096, len = 0;
    ctx->xml = malloc(cap);
    if (!ctx->xml) return NULL;
    ctx->xml[0] = '\0';

    append(&ctx->xml, &len, &cap, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

    int depth = 0;
    int ns_stack[64];
    int ns_depth = 0;

    for (int i = 0; i < ctx->event_count; i++) {
        switch (ctx->event_types[i]) {
        case 0: // start ns
            if (ns_depth < 64) ns_stack[ns_depth++] = i;
            break;
        case 1: // end ns
            if (ns_depth > 0) ns_depth--;
            break;
        case 2: { // start elem
            indent(&ctx->xml, &len, &cap, depth);
            append(&ctx->xml, &len, &cap, "<");
            const char *name = get_str(ctx, ctx->event_name[i]);
            if (!name) name = "unknown";
            const char *ns = get_str(ctx, ctx->event_ns[i]);
            if (ns && ns[0]) {
                const char *p = get_ns_prefix(ns);
                if (p) {
                    append_fmt(&ctx->xml, &len, &cap, "%s:", p);
                }
            }
            append(&ctx->xml, &len, &cap, name);

            axml_element *e = &ctx->elements[i];
            int has_attrs = e->attr_count > 0;
            for (int a = 0; a < e->attr_count; a++) {
                const char *an = get_str(ctx, e->attr_name[a]);
                if (!an) continue;
                append(&ctx->xml, &len, &cap, " ");
                const char *ans = get_str(ctx, e->attr_ns[a]);
                if (ans && ans[0]) {
                    const char *p = get_ns_prefix(ans);
                    if (p) append_fmt(&ctx->xml, &len, &cap, "%s:", p);
                }
                append(&ctx->xml, &len, &cap, an);
                append(&ctx->xml, &len, &cap, "=\"");

                // Compute attr resource ID from pool index + resmap
                uint32_t attr_rid = 0xFFFFFFFF;
                int ni = e->attr_name[a];
                if (ctx->has_resmap && ni >= 0 && ni < ctx->resmap.count)
                    attr_rid = ctx->resmap.ids[ni];
                const char *fs = fmt_typed(e->attr_type[a], e->attr_data[a], e->attr_raw[a], an, ctx->arsc, attr_rid);
                if (fs) {
                    char *esc = escape_xml(fs);
                    append(&ctx->xml, &len, &cap, esc ? esc : "");
                    free(esc);
                } else if (e->attr_raw[a] >= 0) {
                    const char *vs = get_str(ctx, e->attr_raw[a]);
                    if (vs) { char *esc = escape_xml(vs); append(&ctx->xml, &len, &cap, esc ? esc : ""); free(esc); }
                }
                append(&ctx->xml, &len, &cap, "\"");
            }

            // Emit namespaces after attributes (apktool style)
            for (int j = 0; j < ns_depth; j++) {
                int ei = ns_stack[j];
                const char *p = get_str(ctx, ctx->event_ns[ei]);
                const char *u = get_str(ctx, ctx->event_name[ei]);
                if (p && u) {
                    if (has_attrs) { append(&ctx->xml, &len, &cap, "\n"); indent(&ctx->xml, &len, &cap, depth + 1); }
                    append_fmt(&ctx->xml, &len, &cap, " xmlns:%s=\"%s\"", p, u);
                }
            }
            if (ns_depth == 0 && ns && ns[0]) {
                if (has_attrs) { append(&ctx->xml, &len, &cap, "\n"); indent(&ctx->xml, &len, &cap, depth + 1); }
                append_fmt(&ctx->xml, &len, &cap, " xmlns=\"%s\"", ns);
            }

            if (i + 1 < ctx->event_count && ctx->event_types[i + 1] == 3) {
                append(&ctx->xml, &len, &cap, " />\n");
                i++;
            } else {
                append(&ctx->xml, &len, &cap, ">\n");
                depth++;
            }
            break;
        }
        case 3: // end elem
            depth--;
            indent(&ctx->xml, &len, &cap, depth);
            append(&ctx->xml, &len, &cap, "</");
            const char *name = get_str(ctx, ctx->event_name[i]);
            if (!name) name = "unknown";
            const char *ns = get_str(ctx, ctx->event_ns[i]);
            if (ns && ns[0]) {
                const char *p = get_ns_prefix(ns);
                if (p)
                    append_fmt(&ctx->xml, &len, &cap, "%s:", p);
            }
            append_fmt(&ctx->xml, &len, &cap, "%s>\n", name);
            break;
        case 4: { // text
            const char *txt = get_str(ctx, ctx->event_name[i]);
            if (txt) {
                indent(&ctx->xml, &len, &cap, depth);
                char *esc = escape_xml(txt);
                append(&ctx->xml, &len, &cap, esc ? esc : "");
                free(esc);
                append(&ctx->xml, &len, &cap, "\n");
            }
            break;
        }
        }
    }

    return ctx->xml;
}
