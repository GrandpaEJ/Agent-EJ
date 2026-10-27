/* XML -> resources.arsc compiler.
 *
 * decode_apk writes every resource value out as a res values tree, but
 * build_apk used to copy the original resources.arsc back into the APK, so
 * each edit made in those files was silently lost.  This closes the loop: it
 * reads the decoded tree, patches the matching entries of the original table
 * and writes a new resources.arsc.
 *
 * The table is loaded with arsc_model_load() and written back with
 * arsc_encode(), so everything the XML does not mention survives untouched —
 * a tree that was not edited re-encodes byte for byte (proven on real APKs by
 * tests/test_arsc_compile_xml.sh).  Entries are patched in place and never
 * rebuilt, so bags, references and configurations this compiler does not
 * understand are carried through exactly as they were.
 */

#include "format_arsc_xml_internal.h"
#include "format_arsc_model.h"
#include "format_arsc_write.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The decode root holds resources.arsc next to the res directory it wrote. */
static int read_arsc(const char *path, uint8_t **out, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    uint8_t *data = malloc((size_t)sz);
    if (!data || fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = data;
    *out_size = (size_t)sz;
    return 0;
}

/* Compiles every res values file below `res_dir` into `m`. */
static void compile_tree(const char *res_dir, arsc_model *m, arsc_ctx *ctx,
                         int *changed, int *skipped) {
    DIR *rd = opendir(res_dir);
    if (!rd) return;
    struct dirent *de;
    while ((de = readdir(rd)) != NULL) {
        /* Only the values directories hold resource definitions; the res
         * qualifier is the part after "values" ("-land", "-fr-rCA-..."). */
        if (strncmp(de->d_name, "values", 6) != 0) continue;
        const char *qual = de->d_name + 6;
        char sub[4096];
        snprintf(sub, sizeof(sub), "%s/%s", res_dir, de->d_name);
        DIR *sd = opendir(sub);
        if (!sd) continue;
        struct dirent *fe;
        while ((fe = readdir(sd)) != NULL) {
            size_t n = strlen(fe->d_name);
            if (n < 5 || strcmp(fe->d_name + n - 4, ".xml") != 0) continue;
            /* public.xml only records ids, which the table already encodes. */
            if (strcmp(fe->d_name, "public.xml") == 0) continue;
            char fp[8192];
            snprintf(fp, sizeof(fp), "%s/%s", sub, fe->d_name);
            arsc_xml_compile_file(fp, m, ctx, qual, changed, skipped);
        }
        closedir(sd);
    }
    closedir(rd);
}

int arsc_compile_xml(const char *arsc_path, const char *res_dir,
                     const char *out_arsc, int *out_changed, int *out_skipped) {
    if (out_changed) *out_changed = 0;
    if (out_skipped) *out_skipped = 0;

    uint8_t *data = NULL;
    size_t size = 0;
    if (read_arsc(arsc_path, &data, &size) != 0) return -1;

    arsc_model *m = arsc_model_load(data, size);
    /* The old parser is kept alongside for @type/name lookups; unlike the
     * model it points straight into `data`, so `data` outlives it. */
    arsc_ctx *ctx = arsc_parse(data, size);
    if (!m) {
        free(data);
        if (ctx) arsc_free(ctx);
        return -1;
    }

    int changed = 0, skipped = 0;
    compile_tree(res_dir, m, ctx, &changed, &skipped);

    size_t enc_size = 0;
    uint8_t *enc = arsc_encode(m, &enc_size);
    arsc_model_free(m);
    if (ctx) arsc_free(ctx);
    free(data);
    if (!enc) return -1;

    FILE *out = fopen(out_arsc, "wb");
    if (!out) { free(enc); return -1; }
    size_t written = fwrite(enc, 1, enc_size, out);
    fclose(out);
    free(enc);
    if (written != enc_size) return -1;

    if (out_changed) *out_changed = changed;
    if (out_skipped) *out_skipped = skipped;
    return 0;
}
