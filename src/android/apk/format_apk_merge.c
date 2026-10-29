/* Orchestrates a native anti-split merge: unions the base APK with its
 * configuration splits into one standalone, aligned APK (the caller signs it).
 *
 * Files are taken from the base first and from the splits second; the base
 * wins any path collision, so its AndroidManifest and DEX are kept while the
 * splits contribute their native libraries, density drawables and locale
 * assets. The
 * resource table is not copied but rebuilt by folding every split's
 * resources.arsc into the base table (see arsc_model_merge), and the base
 * manifest has its split requirements cleared so the result installs alone.
 * Entries are stored and 4-byte aligned (4 KiB for *.so) for mmap at runtime. */
#include "format_apk_merge.h"
#include "formats.h"
#include "format_arsc_model.h"
#include "format_arsc_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARSC_NAME     "resources.arsc"
#define MANIFEST_NAME "AndroidManifest.xml"

/* The set of entry names already written, so the base wins collisions. */
typedef struct { char **v; int n; } nameset;

static int seen(nameset *s, const char *name) {
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->v[i], name) == 0) return 1;
    return 0;
}
static void mark(nameset *s, const char *name) {
    char **g = realloc(s->v, (s->n + 1) * sizeof(char *));
    if (!g) return;
    s->v = g;
    s->v[s->n] = strdup(name);
    if (s->v[s->n]) s->n++;
}
static void nameset_free(nameset *s) {
    for (int i = 0; i < s->n; i++) free(s->v[i]);
    free(s->v);
}

/* The manifest and old signature are always replaced, never copied. */
static int skip_name(const char *n) {
    return strcmp(n, MANIFEST_NAME) == 0 || strncmp(n, "META-INF/", 9) == 0;
}
static int is_dir_entry(const char *n) {
    size_t l = strlen(n);
    return l && n[l - 1] == '/';
}

/* Writes one entry, page-aligning shared libraries and 4-aligning the rest. */
static void add_entry(zip_writer *zw, const char *name, const void *data, size_t sz) {
    size_t l = strlen(name);
    int align = (l > 3 && strcmp(name + l - 3, ".so") == 0) ? 4096 : 4;
    zip_writer_add(zw, name, data, sz, 0, align);
}

/* Extracts a named member of an open archive, or NULL when absent. */
static void *za_extract(zip_archive *za, const char *name, size_t *sz) {
    int n = zip_get_num_entries(za);
    for (int i = 0; i < n; i++) {
        const char *en = zip_get_entry_name(za, i);
        if (en && strcmp(en, name) == 0) return zip_extract_entry(za, i, sz);
    }
    return NULL;
}

/* Rebuilds resources.arsc from the base table plus every split that has one. */
static uint8_t *merge_arsc(apk_set *set, size_t *out_sz) {
    zip_archive *bz = zip_open(set->paths[set->base_index]);
    if (!bz) return NULL;
    size_t bsz = 0;
    uint8_t *bdata = za_extract(bz, ARSC_NAME, &bsz);
    zip_close(bz);
    if (!bdata) return NULL;
    arsc_model *base = arsc_model_load(bdata, bsz);
    free(bdata);
    if (!base) return NULL;

    for (int i = 0; i < set->count; i++) {
        if (i == set->base_index) continue;
        zip_archive *z = zip_open(set->paths[i]);
        if (!z) continue;
        size_t ssz = 0;
        uint8_t *sdata = za_extract(z, ARSC_NAME, &ssz);
        zip_close(z);
        if (!sdata) continue;
        arsc_model *s = arsc_model_load(sdata, ssz);
        free(sdata);
        if (s) { arsc_model_merge(base, s); arsc_model_free(s); }
    }
    uint8_t *enc = arsc_encode(base, out_sz);
    arsc_model_free(base);
    return enc;
}

/* Copies every eligible file of one archive into the writer. */
static void copy_archive(zip_writer *zw, zip_archive *za, nameset *ns) {
    int n = zip_get_num_entries(za);
    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || is_dir_entry(name) || skip_name(name) || seen(ns, name)) continue;
        size_t sz = 0;
        void *d = zip_extract_entry(za, i, &sz);
        if (!d) continue;
        add_entry(zw, name, d, sz);
        mark(ns, name);
        free(d);
    }
}

int apk_merge_assemble(const char *input, const char *out_apk) {
    apk_set set;
    if (apk_set_collect(input, &set) != 0) return -1;

    nameset ns = {0};
    size_t arsc_sz = 0;
    uint8_t *arsc = merge_arsc(&set, &arsc_sz);
    uint8_t *manifest = NULL;
    size_t msz = 0;
    int rc = -1;

    zip_archive *bz = zip_open(set.paths[set.base_index]);
    if (!bz) goto done;

    /* Patch the base manifest (falling back to the raw bytes on failure). */
    size_t raw_sz = 0;
    uint8_t *raw = za_extract(bz, MANIFEST_NAME, &raw_sz);
    if (raw) {
        manifest = apk_fix_split_manifest(raw, raw_sz, &msz);
        if (manifest) free(raw);
        else { manifest = raw; msz = raw_sz; }
    }

    zip_writer *zw = zip_writer_open(out_apk);
    if (!zw) { zip_close(bz); goto done; }

    if (manifest) { add_entry(zw, MANIFEST_NAME, manifest, msz); mark(&ns, MANIFEST_NAME); }
    if (arsc)     { add_entry(zw, ARSC_NAME, arsc, arsc_sz);    mark(&ns, ARSC_NAME); }

    copy_archive(zw, bz, &ns);   /* base wins collisions */
    zip_close(bz);
    for (int k = 0; k < set.count; k++) {
        if (k == set.base_index) continue;
        zip_archive *z = zip_open(set.paths[k]);
        if (!z) continue;
        copy_archive(zw, z, &ns);
        zip_close(z);
    }
    rc = zip_writer_close(zw);
done:
    free(arsc);
    free(manifest);
    nameset_free(&ns);
    apk_set_free(&set);
    return rc;
}

int apk_merge(const char *input, const char *out_apk,
              const char *key_path, const char *cert_path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.merged.zip", out_apk);
    if (apk_merge_assemble(input, tmp) != 0) { remove(tmp); return -1; }
    return apk_sign_pipeline(tmp, out_apk, key_path, cert_path);
}
