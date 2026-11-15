/* Copy an APK with some entries swapped for files on disk (the dex editor's
 * save writes the rebuilt classesN.dex back this way). The result is left
 * unsigned; old signature files are dropped since they no longer verify. */
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "format_apk_sign_internal.h"

static uint8_t *read_all(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = sz >= 0 ? malloc((size_t)sz + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); buf = NULL; }
    fclose(fp);
    if (buf) *len = (size_t)sz;
    return buf;
}

static int index_of(const char *const *names, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], name) == 0) return i;
    return -1;
}

/* The writer stores entries; keep them aligned (4, a page for .so) like zipalign. */
static int add(zip_writer *zw, const char *name, const void *data, size_t sz) {
    size_t l = strlen(name);
    int align = (l > 3 && strcmp(name + l - 3, ".so") == 0) ? 4096 : 4;
    return zip_writer_add(zw, name, data, sz, 0, align);
}

int apk_replace_entries(const char *in_apk, const char *out_apk, const char *const *names,
                        const char *const *files, int n) {
    zip_archive *za = zip_open(in_apk);
    if (!za) return -1;
    zip_writer *zw = zip_writer_open(out_apk);
    if (!zw) { zip_close(za); return -1; }

    int rc = 0;
    char *done = calloc((size_t)(n > 0 ? n : 1), 1);
    int total = zip_get_num_entries(za);
    for (int i = 0; i < total && rc == 0 && done; i++) {
        const char *name = zip_get_entry_name(za, i);
        size_t l = name ? strlen(name) : 0;
        if (!l || name[l - 1] == '/' || apk_is_signature_file(name)) continue;
        int r = index_of(names, n, name);
        size_t sz = 0;
        uint8_t *data = r >= 0 ? read_all(files[r], &sz) : zip_extract_entry(za, i, &sz);
        if (!data) { rc = -1; break; }
        if (r >= 0) done[r] = 1;
        rc = add(zw, name, data, sz) == 0 ? 0 : -1;
        free(data);
    }
    for (int r = 0; r < n && rc == 0 && done; r++) {
        if (done[r]) continue;                 /* new entry, e.g. a fresh classesN.dex */
        size_t sz = 0;
        uint8_t *data = read_all(files[r], &sz);
        rc = data && add(zw, names[r], data, sz) == 0 ? 0 : -1;
        free(data);
    }
    if (!done) rc = -1;
    free(done);
    zip_close(za);
    return zip_writer_close(zw) == 0 ? rc : -1;
}
