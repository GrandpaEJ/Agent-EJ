/* Resolves the input of a native anti-split merge into a concrete set of APK
 * files on disk: either a directory that already holds them, or an .xapk /
 * .apks / .apkm bundle (a ZIP of APKs) whose members are extracted to a
 * temporary directory. The base APK is told apart from the configuration
 * splits by name — bundletool uses `base.apk` + `split_*.apk`, APKPure uses
 * `<pkg>.apk` + `config.*.apk` — falling back to the largest APK. */
#include "format_apk_merge.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int is_split_name(const char *path) {
    const char *b = base_name(path);
    return strncmp(b, "split_", 6) == 0 || strncmp(b, "config.", 7) == 0;
}

static int ends_with(const char *s, const char *suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcasecmp(s + ls - lf, suf) == 0;
}

static void push(apk_set *set, const char *path) {
    char **g = realloc(set->paths, (set->count + 1) * sizeof(char *));
    if (!g) return;
    set->paths = g;
    set->paths[set->count] = strdup(path);
    if (set->paths[set->count]) set->count++;
}

/* Picks the base APK: an explicit base.apk, else the sole non-split, else the
 * largest file. */
static void choose_base(apk_set *set) {
    int non_split = -1, non_split_n = 0;
    long best = -1; int best_i = 0;
    for (int i = 0; i < set->count; i++) {
        if (strcmp(base_name(set->paths[i]), "base.apk") == 0) { set->base_index = i; return; }
        if (!is_split_name(set->paths[i])) { non_split = i; non_split_n++; }
        struct stat st;
        if (stat(set->paths[i], &st) == 0 && (long)st.st_size > best) { best = st.st_size; best_i = i; }
    }
    set->base_index = (non_split_n == 1) ? non_split : best_i;
}

static int collect_dir(const char *dir, apk_set *set) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !ends_with(e->d_name, ".apk")) continue;
        char p[2048];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        push(set, p);
    }
    closedir(d);
    return set->count ? 0 : -1;
}

/* Extracts every *.apk member of a bundle ZIP into a fresh temp directory. */
static int collect_bundle(const char *bundle, apk_set *set) {
    zip_archive *za = zip_open(bundle);
    if (!za) return -1;
    const char *tmp = getenv("TMPDIR");
    snprintf(set->tmpdir, sizeof(set->tmpdir), "%s/ejmergeXXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(set->tmpdir)) { set->tmpdir[0] = 0; zip_close(za); return -1; }

    int n = zip_get_num_entries(za), rc = -1;
    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strchr(name, '/') || !ends_with(name, ".apk")) continue;
        size_t sz = 0;
        void *buf = zip_extract_entry(za, i, &sz);
        if (!buf) continue;
        char p[2048];
        snprintf(p, sizeof(p), "%s/%s", set->tmpdir, base_name(name));
        FILE *fp = fopen(p, "wb");
        if (fp) { fwrite(buf, 1, sz, fp); fclose(fp); push(set, p); rc = 0; }
        free(buf);
    }
    zip_close(za);
    return set->count ? rc : -1;
}

int apk_set_collect(const char *input, apk_set *set) {
    if (!input || !set) return -1;
    memset(set, 0, sizeof(*set));
    struct stat st;
    if (stat(input, &st) != 0) return -1;
    int rc = S_ISDIR(st.st_mode) ? collect_dir(input, set) : collect_bundle(input, set);
    if (rc != 0 || set->count == 0) { apk_set_free(set); return -1; }
    choose_base(set);
    return 0;
}

void apk_set_free(apk_set *set) {
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        if (set->tmpdir[0]) remove(set->paths[i]);
        free(set->paths[i]);
    }
    free(set->paths);
    if (set->tmpdir[0]) rmdir(set->tmpdir);
    memset(set, 0, sizeof(*set));
}
