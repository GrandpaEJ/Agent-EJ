/* Recursive filename search. Walks with an explicit depth cap and stops at
 * `limit` hits; each hit is "relative/path\tisDir\tsize\tmtime". */
#include "ej_jni_util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define EJ_FIND_DEPTH 24

typedef struct {
    const char *needle;   /* lower-cased */
    size_t root_len;
    int limit;
    int count;
    char **hits;
} find_ctx;

static int icontains(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == needle[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

static void walk(find_ctx *c, const char *dir, int depth) {
    if (depth > EJ_FIND_DEPTH || c->count >= c->limit) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    char full[1024];
    while ((ent = readdir(d)) != NULL && c->count < c->limit) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name) >= (int)sizeof(full)) continue;
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        int is_dir = S_ISDIR(st.st_mode);
        if (icontains(ent->d_name, c->needle)) {
            char line[1200];
            snprintf(line, sizeof(line), "%s\t%d\t%lld\t%lld", full + c->root_len, is_dir,
                     (long long)st.st_size, (long long)st.st_mtime);
            c->hits[c->count] = ej_dup(line);
            if (c->hits[c->count]) c->count++;
        }
        if (is_dir) walk(c, full, depth + 1);
    }
    closedir(d);
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_findFiles(JNIEnv *env, jobject thiz, jstring jroot,
                                          jstring jquery, jint jlimit) {
    (void)thiz;
    const char *root = (*env)->GetStringUTFChars(env, jroot, NULL);
    const char *query = (*env)->GetStringUTFChars(env, jquery, NULL);
    jobjectArray out = NULL;
    int limit = jlimit > 0 && jlimit <= 2000 ? jlimit : 500;
    char needle[256];
    char base[1024];
    char **hits = calloc((size_t)limit, sizeof(char *));
    if (root && query && hits) {
        size_t i = 0;
        for (; query[i] && i < sizeof(needle) - 1; i++) needle[i] = (char)tolower((unsigned char)query[i]);
        needle[i] = '\0';
        snprintf(base, sizeof(base), "%s", root);
        size_t bl = strlen(base);
        while (bl > 1 && base[bl - 1] == '/') base[--bl] = '\0';
        find_ctx c = {needle, bl + 1, limit, 0, hits};
        walk(&c, base, 0);
        out = ej_jstr_array(env, (const char **)hits, c.count);
        for (int k = 0; k < c.count; k++) free(hits[k]);
    }
    free(hits);
    if (root) (*env)->ReleaseStringUTFChars(env, jroot, root);
    if (query) (*env)->ReleaseStringUTFChars(env, jquery, query);
    return out;
}
