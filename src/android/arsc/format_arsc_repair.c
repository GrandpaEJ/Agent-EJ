/* Repairs "anti-resource" tampering in a resources.arsc image — junk chunks,
 * lied-about chunk sizes and other decorations planted to crash aapt2/apktool
 * while the Android runtime ignores them.
 *
 * The repair is the proven round-trip: load the table into the mutable model
 * (which parses only the real type/typeSpec/package structure and skips the
 * decoration) and re-encode it canonically. The result is a clean table with
 * identical resources that every tool can read. If the image is too corrupt
 * for the model to load, no output is produced and the caller is told. */
#include "format_arsc_model.h"
#include "format_arsc_write.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t *arsc_repair(const uint8_t *in, size_t size, size_t *out_size,
                     char *changes, size_t changes_cap) {
    if (!in || size < 12 || !out_size) return NULL;
    if (changes && changes_cap) changes[0] = '\0';

    arsc_model *m = arsc_model_load(in, size);
    if (!m) {
        if (changes && changes_cap)
            snprintf(changes, changes_cap, "resource table too corrupt to load");
        return NULL;
    }
    size_t enc_sz = 0;
    uint8_t *enc = arsc_encode(m, &enc_sz);
    arsc_model_free(m);
    if (!enc) {
        if (changes && changes_cap)
            snprintf(changes, changes_cap, "re-encode failed");
        return NULL;
    }
    if (changes && changes_cap) {
        if (enc_sz == size && memcmp(enc, in, size) == 0)
            snprintf(changes, changes_cap, "no tampering found");
        else if (enc_sz == size)
            snprintf(changes, changes_cap, "table re-encoded canonically (same size)");
        else
            snprintf(changes, changes_cap,
                     "table re-encoded canonically (%zu -> %zu bytes)", size, enc_sz);
    }
    *out_size = enc_sz;
    return enc;
}
