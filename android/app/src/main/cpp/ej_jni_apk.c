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

/* One entry point for "fix this binary resource", dispatched by content. */JNIEXPORT jstring JNICALL
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

/* ---- info table -------------------------------------------------------- */

#include "crypto.h"
#include <sys/stat.h>

static int attr_value(const char *xml, const char *needle, char *out, size_t cap) {
    const char *p = strstr(xml, needle);
    if (!p) return 0;
    p += strlen(needle);
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = '\0';
    return n > 0;
}

static void file_sha256(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "-");
    FILE *f = fopen(path, "rb");
    if (!f) return;
    sha256_ctx ctx;
    sha256_init(&ctx);
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha256_update(&ctx, buf, n);
    fclose(f);
    uint8_t hash[32];
    sha256_final(&ctx, hash);
    size_t o = 0;
    for (int i = 0; i < 32 && o + 3 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "%02x", hash[i]);
}

static int tail_has(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long size = ftell(f);
    long window = size > 262144 ? 262144 : size;
    if (window < 32 || fseek(f, size - window, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    char *buf = (char *)malloc((size_t)window + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t got = fread(buf, 1, (size_t)window, f);
    fclose(f);
    buf[got] = '\0';
    int hit = memmem(buf, got, needle, strlen(needle)) != NULL;
    free(buf);
    return hit;
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_apkInfo(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    jobjectArray out = NULL;

    if (path) {
        struct stat st;
        int size = (stat(path, &st) == 0) ? (int)st.st_size : 0;
        zip_archive *za = zip_open(path);
        int entries = za ? zip_get_num_entries(za) : 0;

        char package[256] = "-", vname[128] = "-", vcode[32] = "-";
        char minsdk[32] = "-", maxsdk[32] = "-";
        int v1 = 0, dex_count = 0;

        if (za) {
            for (int i = 0; i < entries; i++) {
                const char *en = zip_get_entry_name(za, i);
                if (!en) continue;
                size_t el = strlen(en);
                if (el > 3 && !strcmp(en + el - 3, ".SF") && !strncmp(en, "META-INF/", 9)) v1 = 1;
                if (!strncmp(en, "classes", 7) && el > 4 && !strcmp(en + el - 4, ".dex")) dex_count++;
            }
            size_t mlen = 0;
            uint8_t *mdata = ej_read_zip_entry(path, "AndroidManifest.xml", &mlen);
            if (mdata && mlen) {
                axml_ctx *ax = axml_parse(mdata, mlen);
                if (ax) {
                    char *xml = axml_get_xml(ax); /* ctx-owned */
                    if (xml) {
                        attr_value(xml, "package=\"", package, sizeof(package));
                        attr_value(xml, "android:versionName=\"", vname, sizeof(vname));
                        attr_value(xml, "android:versionCode=\"", vcode, sizeof(vcode));
                        attr_value(xml, "android:minSdkVersion=\"", minsdk, sizeof(minsdk));
                        attr_value(xml, "android:targetSdkVersion=\"", maxsdk, sizeof(maxsdk));
                    }
                    axml_free(ax);
                }
                free(mdata);
            }
            zip_close(za);
        }

        int v2 = tail_has(path, "APK Sig Block 42");
        char sig[32];
        if (v2 && v1)
            snprintf(sig, sizeof(sig), "V1+V2/V3");
        else if (v2)
            snprintf(sig, sizeof(sig), "V2/V3");
        else if (v1)
            snprintf(sig, sizeof(sig), "V1");
        else
            snprintf(sig, sizeof(sig), "unsigned");

        char sha[80], fsize[32], cnt[32], dexbuf[16];
        file_sha256(path, sha, sizeof(sha));
        snprintf(fsize, sizeof(fsize), "%d", size);
        snprintf(cnt, sizeof(cnt), "%d", entries);
        snprintf(dexbuf, sizeof(dexbuf), "%d", dex_count);

        const char *lines[16];
        char storage[16][320];
        int n = 0;
#define EJ_ROW(fmt, ...) do { snprintf(storage[n], sizeof(storage[n]), fmt, __VA_ARGS__); lines[n] = storage[n]; n++; } while (0)
        EJ_ROW("Package\t%s", package);
        EJ_ROW("Version\t%s (%s)", vname, vcode);
        EJ_ROW("Min SDK\t%s", minsdk);
        EJ_ROW("Target SDK\t%s", maxsdk);
        EJ_ROW("Signature\t%s", sig);
        EJ_ROW("Size\t%s", fsize);
        EJ_ROW("Entries\t%s", cnt);
        EJ_ROW("DEX files\t%s", dexbuf);
        EJ_ROW("SHA-256\t%s", sha);
#undef EJ_ROW
        out = ej_jstr_array(env, lines, n);
    }

    if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
    return out;
}
