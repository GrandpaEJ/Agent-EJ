/* DEX -> smali source tree (one .smali per class), raw byte reads for the
 * hex viewer, and single-entry extraction from an archive. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void mkdirs_for(char *file) {
    for (char *p = file + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(file, 0755);
        *p = '/';
    }
}

/* ".class public final Lcom/a/B;" -> "com/a/B" (written into out). */
static int class_path(const char *smali, char *out, size_t cap) {
    const char *nl = strchr(smali, '\n');
    size_t n = nl ? (size_t)(nl - smali) : strlen(smali);
    const char *end = smali + n;
    while (end > smali && end[-1] != ';') end--;
    if (end == smali) return -1;
    const char *start = end - 1;
    while (start > smali && start[-1] != ' ') start--;
    if (*start != 'L') return -1;
    size_t len = (size_t)(end - 1 - (start + 1));
    if (len == 0 || len >= cap) return -1;
    memcpy(out, start + 1, len);
    out[len] = '\0';
    return 0;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexToSmaliDir(JNIEnv *env, jobject thiz,
                                              jstring jarc, jstring jentry, jstring jout) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;
    size_t len = 0;
    uint8_t *data = NULL;
    if (arc && out) data = entry && entry[0] ? ej_read_zip_entry(arc, entry, &len) : ej_read_file(arc, &len, 0);
    dex_ctx *dx = data ? dex_parse(data, len) : NULL;
    if (!dx) {
        res = ej_jresult(env, -1, data ? "not a valid DEX" : "cannot read DEX");
    } else {
        mkdir(out, 0755);
        uint32_t written = 0, failed = 0;
        for (uint32_t ci = 0;; ci++) {
            char *smali = dex_to_smali_class(dx, ci);
            if (!smali) break;
            char rel[900], file[1400];
            if (class_path(smali, rel, sizeof(rel)) == 0) {
                snprintf(file, sizeof(file), "%s/%s.smali", out, rel);
                mkdirs_for(file);
                if (ej_write_file(file, smali, strlen(smali)) == 0) written++;
                else failed++;
            } else {
                failed++;
            }
            free(smali);
        }
        dex_free(dx);
        char msg[96];
        snprintf(msg, sizeof(msg), "%u class(es) written%s", written, failed ? ", some failed" : "");
        res = ej_jresult(env, written ? 0 : -1, msg);
    }
    free(data);
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}

/* Up to `max` bytes from `off` of a plain file (hex viewer pages). */
JNIEXPORT jbyteArray JNICALL
Java_org_agentej_android_Native_readBytes(JNIEnv *env, jobject thiz, jstring jpath, jlong off, jint max) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (!f || max <= 0) { if (f) fclose(f); return NULL; }
    jbyteArray arr = NULL;
    uint8_t *buf = malloc((size_t)max);
    if (buf && fseeko(f, (off_t)off, SEEK_SET) == 0) {
        size_t n = fread(buf, 1, (size_t)max, f);
        arr = (*env)->NewByteArray(env, (jsize)n);
        if (arr) (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)buf);
    }
    free(buf);
    fclose(f);
    return arr;
}

/* One archive entry -> a plain file (creates parent folders). */
JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_extractEntry(JNIEnv *env, jobject thiz, jstring jarc, jstring jname, jstring jout) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *name = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    int rc = -1;
    if (arc && name && out) {
        size_t len = 0;
        uint8_t *data = ej_read_zip_entry(arc, name, &len);
        if (data || len == 0) {
            char path[1400];
            snprintf(path, sizeof(path), "%s", out);
            mkdirs_for(path);
            rc = ej_write_file(out, data ? data : (const uint8_t *)"", len);
        }
        free(data);
    }
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (name) (*env)->ReleaseStringUTFChars(env, jname, name);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return rc;
}
