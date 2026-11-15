/* ARSC tools: string pool listing, in-place string patching, TOML dump. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *load_arsc(const char *arc, size_t *len) {
    uint8_t *data = ej_read_zip_entry(arc, "resources.arsc", len);
    if (data) return data;
    return ej_read_file(arc, len, 0);
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_arscStrings(JNIEnv *env, jobject thiz, jstring jarc, jint jlimit) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    jobjectArray arr = NULL;

    if (arc) {
        size_t len = 0;
        uint8_t *data = load_arsc(arc, &len);
        if (data && len > 12) {
            arsc_ctx *ctx = arsc_parse(data, len);
            if (ctx) {
                int limit = jlimit > 0 ? jlimit : 8000;
                const char **items = (const char **)calloc((size_t)limit, sizeof(char *));
                int kept = 0;
                for (int i = 0; i < limit && items; i++) {
                    const char *s = arsc_get_string(ctx, (uint32_t)i);
                    if (!s) break;
                    items[kept++] = ej_dup(s);
                }
                if (items) {
                    arr = ej_jstr_array(env, items, kept);
                    for (int i = 0; i < kept; i++) free((void *)items[i]);
                    free(items);
                }
                arsc_free(ctx);
            }
        }
        free(data);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    return arr;
}

/* Rewrite the archive with a different resources.arsc, keeping every other
 * entry byte-for-byte (stored entries stay stored, deflated stay deflated). */
static int replace_arsc(const char *arc, const uint8_t *arsc, size_t arsc_len) {
    char tmp[1400];
    snprintf(tmp, sizeof(tmp), "%s.rebuild%ld.zip", arc, (long)getpid());

    zip_archive *za = zip_open(arc);
    if (!za) return -1;
    zip_writer *zw = zip_writer_open(tmp);
    if (!zw) {
        zip_close(za);
        return -1;
    }

    int n = zip_get_num_entries(za);
    int rc = 0;
    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name) continue;
        if (strcmp(name, "resources.arsc") == 0) continue;
        size_t len = 0;
        void *data = zip_extract_entry(za, i, &len);
        if (!data) {
            rc = -1;
            break;
        }
        int compress = zip_entry_is_compressed(za, i);
        if (zip_writer_add(zw, name, data, len, compress, 0) != 0) rc = -1;
        free(data);
        if (rc != 0) break;
    }
    zip_close(za);

    if (rc == 0 && zip_writer_add(zw, "resources.arsc", arsc, arsc_len, 0, 4) != 0) rc = -1;
    if (zip_writer_close(zw) != 0) rc = -1;

    if (rc == 0) {
        char backup[1400];
        snprintf(backup, sizeof(backup), "%s.bak%ld", arc, (long)getpid());
        if (rename(arc, backup) != 0) {
            rc = -1;
        } else if (rename(tmp, arc) != 0) {
            rename(backup, arc);
            rc = -1;
        } else {
            remove(backup);
        }
    }
    if (rc != 0) remove(tmp);
    return rc;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_arscPatch(JNIEnv *env, jobject thiz, jstring jarc, jint index,
                                          jstring jvalue) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *value = (*env)->GetStringUTFChars(env, jvalue, NULL);
    jstring res = NULL;

    if (arc && value) {
        size_t len = 0;
        uint8_t *data = load_arsc(arc, &len);
        if (data && len > 12) {
            arsc_ctx *ctx = arsc_parse(data, len);
            if (ctx) {
                int rc = arsc_patch_string(ctx, (uint32_t)index, value);
                if (rc == 0) {
                    size_t out_len = 0;
                    uint8_t *built = arsc_build(ctx, &out_len);
                    if (built && out_len > 0) {
                        rc = replace_arsc(arc, built, out_len);
                        res = ej_jresult(env, rc, rc == 0 ? "patched" : "archive rewrite failed");
                        free(built);
                    } else {
                        res = ej_jresult(env, -1, "rebuild failed");
                    }
                } else {
                    res = ej_jresult(env, rc, "patch rejected");
                }
                arsc_free(ctx);
            } else {
                res = ej_jresult(env, -1, "not an arsc table");
            }
        } else {
            res = ej_jresult(env, -1, "resources.arsc missing");
        }
        free(data);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (value) (*env)->ReleaseStringUTFChars(env, jvalue, value);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_arscDumpToml(JNIEnv *env, jobject thiz, jstring jarc,
                                             jstring jout) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;

    if (arc && out) {
        size_t len = 0;
        uint8_t *data = load_arsc(arc, &len);
        if (data && len > 12) {
            ej_ctx_t *ctx = ej_ctx();
            char tmp[1400];
            snprintf(tmp, sizeof(tmp), "%s/arsc.%ld.bin", ctx->data_dir, (long)getpid());
            if (ej_write_file(tmp, data, len) == 0) {
                int rc = arsc_dump_toml(tmp, out);
                remove(tmp);
                res = ej_jresult(env, rc, rc == 0 ? out : "dump failed");
            } else {
                res = ej_jresult(env, -1, "temp write failed");
            }
        } else {
            res = ej_jresult(env, -1, "resources.arsc missing");
        }
        free(data);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}
