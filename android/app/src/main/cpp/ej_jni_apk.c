/* APK tools: analysis summary and the v1 -> align -> v2/v3 signing pipeline. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_apkAnalyze(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    jstring out = NULL;
    if (path) {
        char *res = apk_analyze(path);
        if (res) {
            out = ej_jstr(env, res);
            apk_free(res);
        }
    }
    if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
    return out ? out : (*env)->NewStringUTF(env, "");
}

static int copy_file(const char *from, const char *to) {
    size_t len = 0;
    uint8_t *data = ej_read_file(from, &len, 0);
    if (!data) return -1;
    int rc = ej_write_file(to, data, len);
    free(data);
    return rc;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_signApk(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    const char *in = (*env)->GetStringUTFChars(env, jin, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;

    if (in && out) {
        ej_ctx_t *ctx = ej_ctx();
        if (!ej_keys_ready()) {
            res = ej_jresult(env, -1, "debug key unavailable");
        } else {
            /* apk_sign_pipeline consumes its payload, so sign a scratch copy */
            char payload[1500];
            snprintf(payload, sizeof(payload), "%s/ej_sign%ld.zip", ctx->data_dir, (long)getpid());
            if (copy_file(in, payload) == 0) {
                int rc = apk_sign_pipeline(payload, out, ctx->key_path, ctx->cert_path);
                char detail[256];
                if (rc == 0)
                    snprintf(detail, sizeof(detail), "v1+v2+v3 signed");
                else
                    snprintf(detail, sizeof(detail), "pipeline rc=%d", rc);
                res = ej_jresult(env, rc, detail);
            } else {
                res = ej_jresult(env, -1, "cannot read input apk");
            }
        }
    }

    if (in) (*env)->ReleaseStringUTFChars(env, jin, in);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}

/* One entry point for "fix this binary resource", dispatched by content. */
JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_repairFile(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    const char *in = (*env)->GetStringUTFChars(env, jin, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;

    if (in && out) {
        size_t len = 0;
        uint8_t *data = ej_read_file(in, &len, 0);
        char changes[256] = {0};
        if (data && len > 8) {
            size_t out_len = 0;
            uint8_t *fixed = NULL;
            const char *kind = "unknown";
            if (memcmp(data, "dex\n", 4) == 0) {
                fixed = dex_repair(data, len, &out_len, changes, sizeof(changes));
                kind = "dex";
            } else if (len > 8 && data[0] == 0x02 && data[1] == 0x00) {
                fixed = arsc_repair(data, len, &out_len, changes, sizeof(changes));
                kind = "arsc";
            } else {
                fixed = axml_repair(data, len, &out_len, changes, sizeof(changes));
                kind = "axml";
            }
            if (fixed && ej_write_file(out, fixed, out_len) == 0) {
                char detail[320];
                snprintf(detail, sizeof(detail), "%s: %s", kind, changes[0] ? changes : "no change");
                res = ej_jresult(env, 0, detail);
            } else {
                res = ej_jresult(env, -1, "repair failed");
            }
            free(fixed);
        } else {
            res = ej_jresult(env, -1, "cannot read input");
        }
        free(data);
    }

    if (in) (*env)->ReleaseStringUTFChars(env, jin, in);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}
