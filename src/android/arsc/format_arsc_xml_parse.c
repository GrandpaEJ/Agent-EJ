/* Element scanner of the XML resource compiler.
 *
 * The decoded res values files are small and follow one shape,
 *
 *     <string name="title">Hello</string>
 *     <color name="tint">#ff00ff00</color>
 *
 * so a single pass over the tags is enough; a full XML parser would be dead
 * weight here.  Markup the compiler cannot turn into a scalar — a bag such as
 * a style or an array, a self closing marker, the rows of public.xml — is
 * skipped and its entry therefore keeps its original value.
 */

#include "format_arsc_xml_internal.h"
#include "format_arsc_model.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many elements of one (tag, key) pair have been compiled so far.  The
 * decoder writes one element per entry in entry order, and a degenerate table
 * may give several entries the same key, so the Nth element has to reach the
 * Nth entry: without this, every duplicate would land on the first one. */
struct key_count {
    char *tag;
    char *key;
    uint32_t n;
};

static uint32_t next_occurrence(struct key_count **list, uint32_t *count,
                                const char *tag, const char *key) {
    for (uint32_t i = 0; i < *count; i++) {
        if (!strcmp((*list)[i].tag, tag) && !strcmp((*list)[i].key, key)) {
            return (*list)[i].n++;
        }
    }
    struct key_count *grown = realloc(*list, (*count + 1) * sizeof(struct key_count));
    if (!grown) return 0;
    *list = grown;
    grown[*count].tag = strdup(tag);
    grown[*count].key = strdup(key);
    /* Occurrence zero is handed out here, so the next one starts at one. */
    grown[*count].n = 1;
    if (!grown[*count].tag || !grown[*count].key) return 0;
    (*count)++;
    return 0;
}

static void free_counts(struct key_count *list, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        free(list[i].tag);
        free(list[i].key);
    }
    free(list);
}

static const struct { const char *ent; char ch; } XML_ENTITIES[] = {
    {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
    {"&quot;", '"'}, {"&apos;", '\''}
};

/* Undoes esc_xml() of format_apk_decode_arsc.c in place: the five predefined
 * entities plus numeric character references, re-encoded as UTF-8. */
static void xml_unescape(char *s) {
    char *w = s, *r = s;
    while (*r) {
        if (*r == '&') {
            int hit = 0;
            for (size_t i = 0; i < sizeof(XML_ENTITIES) / sizeof(XML_ENTITIES[0]); i++) {
                size_t n = strlen(XML_ENTITIES[i].ent);
                if (!strncmp(r, XML_ENTITIES[i].ent, n)) {
                    *w++ = XML_ENTITIES[i].ch;
                    r += n;
                    hit = 1;
                    break;
                }
            }
            if (hit) continue;
            if (r[1] == '#') {
                int hex = (r[2] == 'x' || r[2] == 'X');
                long cp = strtol(r + (hex ? 3 : 2), NULL, hex ? 16 : 10);
                if (cp > 0) {
                    if (cp < 0x80) {
                        *w++ = (char)cp;
                    } else if (cp < 0x800) {
                        *w++ = (char)(0xC0 | (cp >> 6));
                        *w++ = (char)(0x80 | (cp & 0x3F));
                    } else {
                        *w++ = (char)(0xE0 | (cp >> 12));
                        *w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                        *w++ = (char)(0x80 | (cp & 0x3F));
                    }
                    while (*r && *r != ';') r++;
                    if (*r == ';') r++;
                    continue;
                }
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/* Reads a whole file into a NUL terminated buffer, or NULL. */
static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 32 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[sz] = '\0';
    return buf;
}

void arsc_xml_compile_file(const char *path, arsc_model *m, arsc_ctx *ctx,
                           const char *qual, int *changed, int *skipped) {
    char *buf = read_text(path);
    if (!buf) return;

    struct key_count *counts = NULL;
    uint32_t count = 0;

    char *p = buf;
    while ((p = strchr(p, '<')) != NULL) {
        if (p[1] == '?' || p[1] == '!') {           /* prolog, doctype, comment */
            char *gt = strchr(p, '>');
            if (!gt) break;
            p = gt + 1;
            continue;
        }
        if (p[1] == '/') {                          /* closing tag */
            char *gt = strchr(p, '>');
            if (!gt) break;
            p = gt + 1;
            continue;
        }
        char *gt = strchr(p, '>');
        if (!gt) break;

        char *tn = p + 1;                           /* tag name */
        char *te = tn;
        while (*te && !isspace((unsigned char)*te) && *te != '>' && *te != '/') te++;
        char tag[64];
        size_t tlen = (size_t)(te - tn);
        if (tlen == 0 || tlen >= sizeof(tag)) { p = gt + 1; continue; }
        memcpy(tag, tn, tlen);
        tag[tlen] = '\0';

        char key[512] = {0};                        /* name="..." attribute */
        char *na = strstr(te, "name=\"");
        if (na && na < gt) {
            char *vs = na + 6;
            char *ve = strchr(vs, '"');
            if (ve && ve < gt) {
                size_t klen = (size_t)(ve - vs);
                if (klen < sizeof(key)) {
                    memcpy(key, vs, klen);
                    key[klen] = '\0';
                }
            }
        }

        /* The root element, self closing markers, public.xml rows and the
         * item children of a bag carry no scalar value of their own. */
        if (gt[-1] == '/' || !key[0] || key[0] == '@' ||
            !strcmp(tag, "item") || !strcmp(tag, "public")) {
            p = gt + 1;
            continue;
        }

        char *text = gt + 1;
        char closer[80];
        snprintf(closer, sizeof(closer), "</%s>", tag);
        char *end = strstr(text, closer);
        if (!end) { p = gt + 1; continue; }

        /* Nested markup means a bag (style, array): not a scalar value. */
        if (memchr(text, '<', (size_t)(end - text))) {
            p = end + strlen(closer);
            continue;
        }

        *end = '\0';
        xml_unescape(text);
        uint32_t occ = next_occurrence(&counts, &count, tag, key);
        arsc_model_entry *e = arsc_xml_find_entry(m, tag, qual, key, occ);
        if (!e) {
            (*skipped)++;
        } else if (arsc_xml_set_value(m, ctx, e, text, skipped)) {
            (*changed)++;
        }
        *end = '<';                                 /* restore for strstr() */
        p = end + strlen(closer);
    }
    free_counts(counts, count);
    free(buf);
}
