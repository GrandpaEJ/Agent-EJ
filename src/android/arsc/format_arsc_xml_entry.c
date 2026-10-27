/* Entry lookup and value conversion of the XML resource compiler.
 *
 * Resolution is done against the in-memory model rather than by resource id,
 * because a decoded value carries a type name and a configuration but no id:
 * <string name="title"> in res/values-fr has to reach the entry of the French
 * configuration, not the default one.
 */

#include "format_arsc_xml_internal.h"
#include "format_arsc_model.h"
#include "../apk/format_apk_config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Bytes of a ResChunk_header, which apk_build_config_str() expects in front of
 * the ResTable_config it reads (CFG_HDR_SIZE in format_apk_config.c). */
#define XML_CFG_HDR 20

/* Rebuilds the qualifier string that format_apk_decode_arsc.c derived for this
 * variant ("" for the default configuration, "-land" and friends otherwise).
 * apk_build_config_str() reads the config out of a type chunk, so a header is
 * synthesised in front of the raw config the model carries; the config's own
 * size field, which bounds every read inside, comes straight from the model.
 * Sharing this one implementation is what keeps the directory names the
 * decoder writes and the ones this compiler matches identical. */
static void variant_qualifier(const arsc_model_variant *v, char *out, size_t out_sz) {
    out[0] = '\0';
    if (!v || v->config_size < 4) return;
    uint8_t chunk[XML_CFG_HDR + ARSC_CONFIG_BYTES];
    memset(chunk, 0, sizeof(chunk));
    chunk[2] = (uint8_t)(XML_CFG_HDR + v->config_size); /* headerSize, little endian */
    memcpy(chunk + XML_CFG_HDR, v->config, v->config_size);
    apk_build_config_str(chunk, out, out_sz);
}

/* Locates the entry `key` of `type_name` in the configuration `qual` ("" for
 * the default configuration, "-land" and friends otherwise).  `occurrence` is
 * which of the entries sharing that key is wanted, counting from zero: a
 * degenerate table may give several entries of one configuration the same key,
 * and the decoder writes one element per entry in entry order, so the Nth
 * element has to reach the Nth entry rather than always the first.  Returns
 * NULL when the type, the configuration or that key does not exist. */
arsc_model_entry *arsc_xml_find_entry(arsc_model *m, const char *type_name,
                                      const char *qual, const char *key,
                                      uint32_t occurrence) {
    if (!m || !type_name || !qual || !key) return NULL;
    for (uint32_t p = 0; p < m->pkg_count; p++) {
        arsc_model_pkg *pk = &m->pkgs[p];
        for (uint32_t t = 0; t < pk->type_count; t++) {
            arsc_model_type *ty = &pk->types[t];
            if (ty->id == 0) continue;
            const char *tn = arsc_pkg_type_string(pk, ty->id - 1);
            if (!tn || strcmp(tn, type_name) != 0) continue;
            for (uint32_t v = 0; v < ty->variant_count; v++) {
                arsc_model_variant *va = &ty->variants[v];
                char q[160];
                variant_qualifier(va, q, sizeof(q));
                int hit = qual[0] ? (strcmp(q, qual) == 0)
                                  : arsc_variant_is_default(va);
                if (!hit) continue;
                uint32_t seen = 0;
                for (uint32_t i = 0; i < va->entry_count; i++) {
                    arsc_model_entry *e = &va->entries[i];
                    if (!e->present) continue;
                    const char *kn = arsc_pkg_key(pk, e->key);
                    if (!kn || strcmp(kn, key) != 0) continue;
                    if (seen == occurrence) return e;
                    seen++;
                }
                return NULL; /* the configuration matched, the key did not */
            }
        }
    }
    return NULL;
}

/* Stores the decoded text `text` in `e`.  The entry's own data type decides how
 * the text is read: format_apk_decode_arsc.c rendered the value according to
 * exactly that type, so reading it back the same way is the only faithful
 * answer.  A number-shaped string stays a string — gcm_defaultSenderId and
 * friends hold numeric text and must not become integers.
 *
 * Returns 1 when the entry changed and 0 when it already held that value,
 * because an unchanged entry must not be touched: appending a second copy of a
 * string to the global pool would alter the table and break the byte-for-byte
 * round trip.  *skipped counts values that could not be read back, which are
 * left exactly as they were. */
int arsc_xml_set_value(arsc_model *m, arsc_ctx *ctx, arsc_model_entry *e,
                       const char *text, int *skipped) {
    /* A bag (style, array, plurals): its children are items, not a scalar. */
    if (e->flags & ARSC_ENTRY_COMPLEX) { (*skipped)++; return 0; }

    switch (e->data_type) {
    case ARSC_TYPE_STRING: {
        const char *cur = arsc_model_value(m, e->data);
        if (cur && !strcmp(cur, text)) return 0;
        e->data = arsc_model_add_value(m, text);
        return 1;
    }
    case ARSC_TYPE_INT_BOOLEAN: {
        if (strcmp(text, "true") && strcmp(text, "false")) { (*skipped)++; return 0; }
        uint32_t v = !strcmp(text, "true") ? 0xFFFFFFFFu : 0;
        if (e->data == v) return 0;
        e->data = v;
        return 1;
    }
    case ARSC_TYPE_INT_DEC:
    case ARSC_TYPE_INT_HEX: {
        /* The decoder prints these with %d, so a large unsigned value comes
         * back negative; parse signed and keep the same bits. */
        char *end = NULL;
        long long v = strtoll(text, &end, 10);
        if (!end || *end != '\0' || !text[0]) { (*skipped)++; return 0; }
        if (e->data == (uint32_t)v) return 0;
        e->data = (uint32_t)v;
        return 1;
    }
    case ARSC_TYPE_REFERENCE: {
        uint32_t id = 0xFFFFFFFFu;
        if (!strncmp(text, "@ref/0x", 7)) {
            id = (uint32_t)strtoul(text + 7, NULL, 16);
        } else if (text[0] == '@') {
            const char *slash = strchr(text + 1, '/');
            if (slash && slash > text + 1 && slash[1]) {
                size_t tn_len = (size_t)(slash - (text + 1));
                char tn[64];
                if (tn_len < sizeof(tn)) {
                    memcpy(tn, text + 1, tn_len);
                    tn[tn_len] = '\0';
                    id = arsc_reverse_lookup(ctx, tn, slash + 1);
                }
            }
        }
        if (id == 0xFFFFFFFFu) { (*skipped)++; return 0; }
        if (e->data == id) return 0;
        e->data = id;
        return 1;
    }
    /* The colour data types, which the decoder renders as #rrggbb or
     * #aarrggbb: 0x13-0x18 are the classic ones and 0x1c-0x1f the later ones
     * real tables use.  The entry keeps its own type, which is what the app
     * was compiled against. */
    case 0x13: case 0x14: case 0x15: case 0x16:
    case 0x17: case 0x18:
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
        size_t n = text[0] == '#' ? strlen(text + 1) : 0;
        int hex = n > 0;
        for (size_t i = 0; i < n; i++)
            if (!isxdigit((unsigned char)text[1 + i])) { hex = 0; break; }
        if (!hex) { (*skipped)++; return 0; }
        uint32_t v = (uint32_t)strtoul(text + 1, NULL, 16);
        if (e->data == v) return 0;
        e->data = v;
        return 1;
    }
    default:
        /* Everything without a text form this compiler knows (dimension,
         * fraction, float, dynamic references) keeps its value: rewriting one
         * as another kind would corrupt it. */
        (*skipped)++;
        return 0;
    }
}
