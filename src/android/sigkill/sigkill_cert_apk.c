/* Certificate discovery from an APK or a decoded tree. */
#include "sigkill_internal.h"
#include "formats.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_sig_block_name(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    const char *ext = name + n - 4;
    if (ext[0] != '.') return 0;
    char e[4] = { (char)tolower((unsigned char)ext[1]),
                  (char)tolower((unsigned char)ext[2]),
                  (char)tolower((unsigned char)ext[3]), 0 };
    return strcmp(e, "rsa") == 0 || strcmp(e, "dsa") == 0 || strcmp(e, "ec") == 0;
}

int cert_from_apk(sigkill_cert *c, const char *apk_path) {
    zip_archive *za = zip_open(apk_path);
    if (!za) return -1;
    int rc = -1;
    int n = zip_get_num_entries(za);
    for (int i = 0; i < n && rc != 0; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strncmp(name, "META-INF/", 9) != 0) continue;
        if (!is_sig_block_name(name)) continue;
        size_t sz = 0;
        void *data = zip_extract_entry(za, i, &sz);
        if (!data) continue;
        char src[600];
        snprintf(src, sizeof(src), "%s!%s", apk_path, name);
        rc = sk_cert_from_blob(c, data, sz, src);
        free(data);
    }
    zip_close(za);
    return rc;
}

struct tree_ctx { sigkill_cert *c; int found; };

static int tree_file_cb(const char *path, void *ud) {
    struct tree_ctx *t = ud;
    if (t->found) return 0;
    if (!is_sig_block_name(path)) return 0;
    size_t len = 0;
    char *data = sk_read_file(path, &len);
    if (!data) return 0;
    if (sk_cert_from_blob(t->c, (const uint8_t *)data, len, path) == 0)
        t->found = 1;
    free(data);
    return 0;
}

int cert_from_tree(sigkill_cert *c, const char *tree_dir) {
    char meta[4096];
    struct tree_ctx t = { c, 0 };
    snprintf(meta, sizeof(meta), "%s/META-INF", tree_dir);
    if (sk_is_dir(meta)) sk_walk(meta, tree_file_cb, &t);
    if (t.found) return 0;
    /* Some trees keep the signature block at the root. */
    sk_walk(tree_dir, tree_file_cb, &t);
    return t.found ? 0 : -1;
}

