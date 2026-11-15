/* Filesystem tools: everything the UI needs to browse and edit the device. */
#include "ej_jni_util.h"
#include "formats.h"

#include <android/asset_manager_jni.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char name[256];
    int is_dir;
    long long size;
    long long mtime;
} fs_item;

static int cmp_items(const void *va, const void *vb) {
    const fs_item *a = (const fs_item *)va;
    const fs_item *b = (const fs_item *)vb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

JNIEXPORT void JNICALL
Java_org_agentej_android_Native_init(JNIEnv *env, jobject thiz, jobject asset_mgr, jstring jdata) {
    (void)thiz;
    ej_ctx_t *ctx = ej_ctx();
    if (asset_mgr) ctx->assets = AAssetManager_fromJava(env, asset_mgr);
    const char *data = jdata ? (*env)->GetStringUTFChars(env, jdata, NULL) : NULL;
    if (data) {
        snprintf(ctx->data_dir, sizeof(ctx->data_dir), "%s", data);
        (*env)->ReleaseStringUTFChars(env, jdata, data);
    }
    mkdir(ctx->data_dir, 0755);
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_version(JNIEnv *env, jobject thiz) {
    (void)thiz;
    return ej_jstr(env, "agent-ej core 0.12.0 (android ndk)");
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_homePath(JNIEnv *env, jobject thiz) {
    (void)thiz;
    const char *candidates[] = {"/storage/emulated/0", "/sdcard", NULL};
    for (int i = 0; candidates[i]; i++) {
        DIR *d = opendir(candidates[i]);
        if (!d) continue;
        int ok = readdir(d) != NULL;
        closedir(d);
        if (ok) return ej_jstr(env, candidates[i]);
    }
    return ej_jstr(env, ej_ctx()->data_dir);
}

JNIEXPORT jboolean JNICALL
Java_org_agentej_android_Native_storageOk(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    DIR *d = opendir("/storage/emulated/0");
    if (!d) return JNI_FALSE;
    int ok = readdir(d) != NULL;
    closedir(d);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_listDir(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return NULL;

    DIR *d = opendir(path);
    if (!d) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return NULL;
    }

    fs_item *items = NULL;
    int n = 0, cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (n == cap) {
            int nc = cap ? cap * 2 : 64;
            fs_item *ni = (fs_item *)realloc(items, (size_t)nc * sizeof(fs_item));
            if (!ni) break;
            items = ni;
            cap = nc;
        }
        fs_item *it = &items[n];
        snprintf(it->name, sizeof(it->name), "%s", ent->d_name);
        char full[1400];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            it->is_dir = S_ISDIR(st.st_mode);
            it->size = (long long)st.st_size;
            it->mtime = (long long)st.st_mtime;
        } else {
            it->is_dir = (ent->d_type == DT_DIR);
            it->size = 0;
            it->mtime = 0;
        }
        n++;
    }
    closedir(d);
    (*env)->ReleaseStringUTFChars(env, jpath, path);

    if (n > 1) qsort(items, (size_t)n, sizeof(fs_item), cmp_items);

    const char **lines = (const char **)calloc((size_t)(n + 1), sizeof(char *));
    int kept = 0;
    for (int i = 0; i < n && lines; i++) {
        char *line = (char *)malloc(320);
        if (!line) break;
        snprintf(line, 320, "%s\t%d\t%lld\t%lld", items[i].name, items[i].is_dir,
                 items[i].size, items[i].mtime);
        lines[kept++] = line;
    }
    jobjectArray arr = ej_jstr_array(env, lines, kept);
    for (int i = 0; i < kept; i++) free((void *)lines[i]);
    free(lines);
    free(items);
    return arr;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_statPath(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    char buf[128] = "-1\t0\t0\t0";
    if (path) {
        struct stat st;
        if (stat(path, &st) == 0)
            snprintf(buf, sizeof(buf), "%d\t%lld\t%lld\t%d", S_ISDIR(st.st_mode) ? 1 : 0,
                     (long long)st.st_size, (long long)st.st_mtime, access(path, R_OK) == 0 ? 1 : 0);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    }
    return ej_jstr(env, buf);
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_readText(JNIEnv *env, jobject thiz, jstring jpath, jint jmax) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    jstring out = NULL;
    if (path) {
        size_t len = 0;
        size_t max = jmax > 0 ? (size_t)jmax : (size_t)(8 * 1024 * 1024);
        char *txt = (char *)ej_read_file(path, &len, max);
        if (txt && !memchr(txt, 0, len)) out = ej_jstr(env, txt);
        free(txt);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    }
    return out;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_writeText(JNIEnv *env, jobject thiz, jstring jpath, jstring jtext) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    const char *text = (*env)->GetStringUTFChars(env, jtext, NULL);
    int rc = -1;
    if (path && text) rc = ej_write_file(path, text, strlen(text));
    if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (text) (*env)->ReleaseStringUTFChars(env, jtext, text);
    return rc;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_mkdirs(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return -1;
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(tmp, 0755);
        *p = '/';
    }
    int rc = mkdir(tmp, 0755);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return rc;
}

static int rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        char full[1400];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        if (rm_rf(full) != 0) rc = -1;
    }
    closedir(d);
    if (rmdir(path) != 0) rc = -1;
    return rc;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_deletePath(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return -1;
    int rc = rm_rf(path);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return rc;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_renamePath(JNIEnv *env, jobject thiz, jstring jfrom, jstring jto) {
    (void)thiz;
    const char *from = (*env)->GetStringUTFChars(env, jfrom, NULL);
    const char *to = (*env)->GetStringUTFChars(env, jto, NULL);
    int rc = -1;
    if (from && to) rc = rename(from, to);
    if (from) (*env)->ReleaseStringUTFChars(env, jfrom, from);
    if (to) (*env)->ReleaseStringUTFChars(env, jto, to);
    return rc;
}

static int copy_file(const char *from, const char *to) {
    size_t len = 0;
    uint8_t *data = ej_read_file(from, &len, 0);
    if (!data) return -1;
    int rc = ej_write_file(to, data, len);
    free(data);
    return rc;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_copyPath(JNIEnv *env, jobject thiz, jstring jfrom, jstring jto) {
    (void)thiz;
    const char *from = (*env)->GetStringUTFChars(env, jfrom, NULL);
    const char *to = (*env)->GetStringUTFChars(env, jto, NULL);
    int rc = -1;
    if (from && to) {
        struct stat st;
        if (stat(from, &st) == 0) rc = S_ISDIR(st.st_mode) ? -2 : copy_file(from, to);
    }
    if (from) (*env)->ReleaseStringUTFChars(env, jfrom, from);
    if (to) (*env)->ReleaseStringUTFChars(env, jto, to);
    return rc;
}
