/* AXML -> text XML writer. */
#include "format_axml_internal.h"
#include "format_axml_enum.h"
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

/* Prefix the file itself declared for uri (START_NAMESPACE: ns=prefix,
 * name=uri). Matching on strstr(uri, "android") also caught res-auto, so
 * every app:* attribute was written back as android:*. */
static const char *get_ns_prefix(axml_ctx *ctx, const char *uri) {
    if (!uri) return NULL;
    for (int i = 0; i < ctx->event_count; i++) {
        if (ctx->event_types[i] != 0) continue;
        const char *u = get_str(ctx, ctx->event_name[i]);
        const char *p = get_str(ctx, ctx->event_ns[i]);
        if (u && p && p[0] && strcmp(u, uri) == 0) return p;
    }
    if (strcmp(uri, AXML_ANDROID_NS) == 0) return "android";
    return NULL;
}

// Hardcoded android enum/flag values for common attributes

/* MT-style layout: namespace declarations stay on the element line; one
 * attribute stays inline, several go one per line one level deeper. */
static void write_start(axml_ctx *ctx, int i, int depth, int *ns_stack, int ns_depth, int ns_shown,
                        char **out, size_t *len, size_t *cap) {
    indent(out, len, cap, depth);
    append(out, len, cap, "<");
    const char *name = get_str(ctx, ctx->event_name[i]);
    const char *ns = get_str(ctx, ctx->event_ns[i]);
    const char *p = ns && ns[0] ? get_ns_prefix(ctx, ns) : NULL;
    if (p) append_fmt(out, len, cap, "%s:", p);
    append(out, len, cap, name ? name : "unknown");

    for (int j = ns_shown; j < ns_depth; j++) {
        const char *pre = get_str(ctx, ctx->event_ns[ns_stack[j]]);
        const char *uri = get_str(ctx, ctx->event_name[ns_stack[j]]);
        if (pre && uri) append_fmt(out, len, cap, " xmlns:%s=\"%s\"", pre, uri);
    }
    if (ns_depth == 0 && ns && ns[0]) append_fmt(out, len, cap, " xmlns=\"%s\"", ns);

    axml_element *e = &ctx->elements[i];
    int shown = 0;
    for (int a = 0; a < e->attr_count; a++) if (get_str(ctx, e->attr_name[a])) shown++;
    for (int a = 0; a < e->attr_count; a++) {
        const char *an = get_str(ctx, e->attr_name[a]);
        if (!an) continue;
        if (shown > 1) { append(out, len, cap, "\n"); indent(out, len, cap, depth + 1); }
        else append(out, len, cap, " ");
        const char *ans = get_str(ctx, e->attr_ns[a]);
        const char *ap = ans && ans[0] ? get_ns_prefix(ctx, ans) : NULL;
        if (ap) append_fmt(out, len, cap, "%s:", ap);
        append_fmt(out, len, cap, "%s=\"", an);
        uint32_t attr_rid = 0xFFFFFFFF; /* attr resource id from the pool index + resmap */
        int ni = e->attr_name[a];
        if (ctx->has_resmap && ni >= 0 && ni < ctx->resmap.count) attr_rid = ctx->resmap.ids[ni];
        const char *v = axml_fmt_typed(e->attr_type[a], e->attr_data[a], e->attr_raw[a], an, ctx->arsc, attr_rid);
        if (!v && e->attr_raw[a] >= 0) v = get_str(ctx, e->attr_raw[a]);
        char *esc = escape_xml(v ? v : "");
        append(out, len, cap, esc ? esc : "");
        free(esc);
        append(out, len, cap, "\"");
    }
}

char *axml_get_xml(axml_ctx *ctx) {
    if (!ctx) return NULL;
    if (ctx->xml) return ctx->xml;
    size_t cap = 4096, len = 0;
    ctx->xml = malloc(cap);
    if (!ctx->xml) return NULL;
    ctx->xml[0] = '\0';
    append(&ctx->xml, &len, &cap, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

    int depth = 0, ns_stack[64], ns_depth = 0;
    int ns_shown = 0; /* stack entries already declared on an enclosing element */
    for (int i = 0; i < ctx->event_count; i++) {
        switch (ctx->event_types[i]) {
        case 0:
            if (ns_depth < 64) ns_stack[ns_depth++] = i;
            break;
        case 1:
            if (ns_depth > 0) ns_depth--;
            if (ns_shown > ns_depth) ns_shown = ns_depth;
            break;
        case 2:
            write_start(ctx, i, depth, ns_stack, ns_depth, ns_shown, &ctx->xml, &len, &cap);
            ns_shown = ns_depth;
            if (i + 1 < ctx->event_count && ctx->event_types[i + 1] == 3) {
                append(&ctx->xml, &len, &cap, " />\n");
                i++;
            } else {
                append(&ctx->xml, &len, &cap, ">\n");
                depth++;
            }
            break;
        case 3: {
            depth--;
            indent(&ctx->xml, &len, &cap, depth);
            append(&ctx->xml, &len, &cap, "</");
            const char *name = get_str(ctx, ctx->event_name[i]);
            const char *ns = get_str(ctx, ctx->event_ns[i]);
            const char *p = ns && ns[0] ? get_ns_prefix(ctx, ns) : NULL;
            if (p) append_fmt(&ctx->xml, &len, &cap, "%s:", p);
            append_fmt(&ctx->xml, &len, &cap, "%s>\n", name ? name : "unknown");
            break;
        }
        case 4: {
            const char *txt = get_str(ctx, ctx->event_name[i]);
            if (!txt) break;
            indent(&ctx->xml, &len, &cap, depth);
            char *esc = escape_xml(txt);
            append(&ctx->xml, &len, &cap, esc ? esc : "");
            free(esc);
            append(&ctx->xml, &len, &cap, "\n");
            break;
        }
        }
    }
    return ctx->xml;
}
