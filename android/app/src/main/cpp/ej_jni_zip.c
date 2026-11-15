/* ZIP tools: list, read, extract, repack a tree and align. */
#include "ej_jni_util.h"
#include "formats.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_zipEntries(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return NULL;
    zip_archive *za = zip_open(path);
    if (!za) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return NULL;
    }
    int n = zip_get_num_entries(za);
    if (n < 0) n = 0;
    const char **items = (const char **)calloc((size_t)n + 1, sizeof(char *));
    int kept = 0;
    for (int i = 0; i < n && items; i++) {
        const char *en = zip_get_entry_name(za, i);
        if (!en) continue;
        char *line = (char *)malloc(strlen(en) + 48);
        if (!line) break;
        snprintf(line, strlen(en) + 48, "%s\t%zu\t%d", en, zip_get_entry_size(za, i),
                 zip_entry_is_compressed(za, i));
        items[kept++] = line;
    }
    jobjectArray arr = ej_jstr_array(env, items, kept);
    for (int i = 0; i < kept; i++) free((void *)items[i]);
    free(items);
    zip_close(za);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return arr;
}

JNIEXPORT jbyteArray JNICALL
Java_org_agentej_android_Native_readEntry(JNIEnv *env, jobject thiz, jstring jarc, jstring jname) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *name = (*env)->GetStringUTFChars(env, jname, NULL);
    jbyteArray out = NULL;
    if (arc && name) {
        size_t len = 0;
        uint8_t *data = ej_read_zip_entry(arc, name, &len);
        if (data) {
            out = (*env)->NewByteArray(env, (jsize)len);
            if (out) (*env)->SetByteArrayRegion(env, out, 0, (jsize)len, (const jbyte *)data);
            free(data);
        }
    }
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (name) (*env)->ReleaseStringUTFChars(env, jname, name);
    return out;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_extractAll(JNIEnv *env, jobject thiz, jstring jarc, jstring jout) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    int rc = -1;
    if (arc && out) {
        char tmp[1200];
        snprintf(tmp, sizeof(tmp), "%s", out);
        for (char *p = tmp + 1; *p; p++) {
            if (*p != '/') continue;
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        mkdir(tmp, 0755);
        zip_archive *za = zip_open(arc);
        if (za) {
            rc = zip_extract_all(za, out);
            zip_close(za);
        }
    }
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return rc;
}

static int pack_walk(zip_writer *zw, const char *root, const char *rel) {
    char dir_path[2048];
    if (rel[0])
        snprintf(dir_path, sizeof(dir_path), "%s/%s", root, rel);
    else
        snprintf(dir_path, sizeof(dir_path), "%s", root);

    DIR *d = opendir(dir_path);
    if (!d) return -1;
    int added = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (!strcmp(ent->d_name, ".git")) continue;

        char child_rel[2048];
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, ent->d_name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", root, child_rel);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            int sub = pack_walk(zw, root, child_rel);
            if (sub > 0) added += sub;
            continue;
        }
        size_t len = 0;
        uint8_t *data = ej_read_file(full, &len, 0);
        if (!data) continue;
        size_t nl = strlen(child_rel);
        int is_arsc = nl > 5 && strcasecmp(child_rel + nl - 5, ".arsc") == 0;
        int is_so = nl > 3 && strcasecmp(child_rel + nl - 3, ".so") == 0;
        if (is_arsc)
            zip_writer_add(zw, child_rel, data, len, 0, 4);
        else if (is_so)
            zip_writer_add(zw, child_rel, data, len, 0, 4096);
        else
            zip_writer_add(zw, child_rel, data, len, 1, 0);
        free(data);
        added++;
    }
    closedir(d);
    return added;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_packDir(JNIEnv *env, jobject thiz, jstring jdir, jstring jout,
                                        jboolean jsign) {
    (void)thiz;
    const char *dir = (*env)->GetStringUTFChars(env, jdir, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    int rc = -1;

    if (dir && out) {
        char payload[1400];
        snprintf(payload, sizeof(payload), "%s.payload.zip", out);
        zip_writer *zw = zip_writer_open(payload);
        if (zw) {
            int added = pack_walk(zw, dir, "");
            if (added >= 0 && zip_writer_close(zw) == 0) {
                if (jsign && ej_keys_ready()) {
                    ej_ctx_t *ctx = ej_ctx();
                    rc = apk_sign_pipeline(payload, out, ctx->key_path, ctx->cert_path);
                } else {
                    rc = zipalign_file(payload, out, 4);
                    remove(payload);
                }
            } else {
                remove(payload);
            }
        }
    }
    if (dir) (*env)->ReleaseStringUTFChars(env, jdir, dir);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return rc;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_zipalign(JNIEnv *env, jobject thiz, jstring jin, jstring jout,
                                         jint align) {
    (void)thiz;
    const char *in = (*env)->GetStringUTFChars(env, jin, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    int rc = -1;
    if (in && out) rc = zipalign_file(in, out, align > 0 ? align : 4);
    if (in) (*env)->ReleaseStringUTFChars(env, jin, in);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return rc;
}
