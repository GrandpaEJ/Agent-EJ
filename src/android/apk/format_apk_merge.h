#ifndef FORMAT_APK_MERGE_H
#define FORMAT_APK_MERGE_H

/* The set of APKs that make up one split app, resolved from a directory of
 * APKs or an .xapk/.apks/.apkm bundle. `paths[base_index]` is the base APK;
 * every other entry is a configuration/feature split. When `tmpdir[0]` is set,
 * the APKs were extracted there and the directory is removed on free. */
typedef struct {
    char **paths;
    int count;
    int base_index;
    char tmpdir[1024];
} apk_set;

/* Resolves `input` into an apk_set. Returns 0 on success. */
int apk_set_collect(const char *input, apk_set *set);
void apk_set_free(apk_set *set);

#endif
