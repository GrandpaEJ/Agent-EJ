/* `zipcert` technique: some protectors open their own APK as a ZIP and parse
 * META-INF/CERT.RSA instead of asking PackageManager.  Re-signing replaces
 * that entry, so retarget those lookups to a copy of the ORIGINAL signature
 * block shipped as assets/sigkill/<name>. */
#include "sigkill_internal.h"
#include "formats.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define ZC_MAX 32

static int dry_run_replace(char **text, size_t *len, const char *oldv,
                    const char *newv, int dry);

struct zc_names {
    char name[ZC_MAX][96];
    int count;
};

static int zc_wanted(const char *base) {
    size_t n = strlen(base);
    if (n == 0) return 0;
    if (strcmp(base, "MANIFEST.MF") == 0) return 1;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return 0;
    char e[8];
    size_t el = strlen(dot + 1);
    if (el >= sizeof(e)) return 0;
    for (size_t i = 0; i < el; i++)
        e[i] = (char)tolower((unsigned char)dot[1 + i]);
    e[el] = '\0';
    return strcmp(e, "rsa") == 0 || strcmp(e, "dsa") == 0 ||
           strcmp(e, "ec") == 0 || strcmp(e, "sf") == 0;
}

static void zc_add(struct zc_names *z, const char *base) {
    if (z->count >= ZC_MAX) return;
    for (int i = 0; i < z->count; i++)
        if (strcmp(z->name[i], base) == 0) return;
    snprintf(z->name[z->count], sizeof(z->name[0]), "%s", base);
    z->count++;
}

static int zc_write(const char *assets, const char *base,
                    const void *data, size_t len, int dry) {
    if (dry) return 0;
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", assets, base);
    return sk_write_file(path, data, len);
}

static int zc_from_apk(const char *apk, const char *assets,
                       struct zc_names *z, int dry, sigkill_report *r) {
    zip_archive *za = zip_open(apk);
    if (!za) return -1;
    int n = zip_get_num_entries(za);
    int copied = 0;
    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strncmp(name, "META-INF/", 9) != 0) continue;
        const char *base = name + 9;
        if (strchr(base, '/') || !zc_wanted(base)) continue;
        size_t sz = 0;
        void *data = zip_extract_entry(za, i, &sz);
        if (!data) continue;
        if (zc_write(assets, base, data, sz, dry) == 0) copied++;
        zc_add(z, base);
        sk_detail(r, "zipcert", name, "%s copy to assets/sigkill", dry ? "would" : "did");
        free(data);
    }
    zip_close(za);
    return copied;
}

static int zc_from_tree(const char *tree, const char *assets,
                        struct zc_names *z, int dry, sigkill_report *r) {
    char meta[4096];
    snprintf(meta, sizeof(meta), "%s/META-INF", tree);
    DIR *d = opendir(meta);
    if (!d) return -1;
    struct dirent *ent;
    int copied = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!zc_wanted(ent->d_name)) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", meta, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        size_t len = 0;
        char *data = sk_read_file(path, &len);
        if (!data) continue;
        if (zc_write(assets, ent->d_name, data, len, dry) == 0) copied++;
        zc_add(z, ent->d_name);
        sk_detail(r, "zipcert", path, "%s copy to assets/sigkill", dry ? "would" : "did");
        free(data);
    }
    closedir(d);
    return copied;
}

struct zc_patch_ctx {
    const struct zc_names *z;
    const char *assets_rel;
    int dry;
    sigkill_report *r;
    int files;
};

static int zc_smali_cb(const char *path, void *ud) {
    struct zc_patch_ctx *ctx = ud;
    size_t pl = strlen(path);
    if (pl < 6 || strcmp(path + pl - 6, ".smali") != 0) return 0;
    if (!sk_has(path, "/smali")) return 0;

    size_t len = 0;
    char *text = sk_read_file(path, &len);
    if (!text) return 0;
    int hits = 0;
    for (int i = 0; i < ctx->z->count; i++) {
        char oldv[160], newv[200];
        snprintf(oldv, sizeof(oldv), "META-INF/%s", ctx->z->name[i]);
        snprintf(newv, sizeof(newv), "%s/%s", ctx->assets_rel, ctx->z->name[i]);
        int n = dry_run_replace(&text, &len, oldv, newv, ctx->dry);
        if (n > 0) {
            hits += n;
            sk_detail(ctx->r, "zipcert", path, "META-INF/%s -> %s x%d",
                      ctx->z->name[i], newv, n);
        }
    }
    if (hits > 0) {
        ctx->files++;
        ctx->r->zipcert_consts += hits;
        if (!ctx->dry) sk_write_file(path, text, len);
    }
    free(text);
    return 0;
}

/* Dry runs must not touch the file, so resolve replacements on a copy. */
static int dry_run_replace(char **text, size_t *len, const char *oldv,
                    const char *newv, int dry) {
    if (dry) {
        char *copy = strdup(*text);
        if (!copy) return 0;
        size_t clen = *len;
        int n = sk_replace_all(&copy, &clen, oldv, newv);
        free(copy);
        return n;
    }
    return sk_replace_all(text, len, oldv, newv);
}

int sk_zipcert_run(const char *tree, const char *orig_apk, int dry_run,
                   sigkill_report *r) {
    char assets[4096], rel[4096];
    snprintf(assets, sizeof(assets), "%s/assets/sigkill", tree);
    if (!dry_run) sk_mkdir_p(assets);

    struct zc_names z;
    memset(&z, 0, sizeof(z));
    int copied = 0;
    if (orig_apk && !sk_is_dir(orig_apk)) copied = zc_from_apk(orig_apk, assets, &z, dry_run, r);
    if (copied <= 0) copied = zc_from_tree(tree, assets, &z, dry_run, r);
    r->zipcert_files += copied < 0 ? 0 : copied;
    if (z.count == 0) {
        sk_detail(r, "zipcert", tree, "no META-INF signature files found");
        return 0;
    }

    snprintf(rel, sizeof(rel), "%s", "assets/sigkill");
    struct zc_patch_ctx ctx = { &z, rel, dry_run, r, 0 };
    sk_walk(tree, zc_smali_cb, &ctx);
    return 0;
}
