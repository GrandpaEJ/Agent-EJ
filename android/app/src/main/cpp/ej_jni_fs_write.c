/* Filesystem writes: create, delete, rename, copy. */
#include "ej_jni_util.h"
#include "formats.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

int ej_rm_rf(const char *path) {
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
        if (ej_rm_rf(full) != 0) rc = -1;
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
    int rc = ej_rm_rf(path);
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
