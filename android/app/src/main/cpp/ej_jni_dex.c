/* DEX tools: class listing from the header tables, class -> smali, and the
 * smali tree -> dex assembler. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *load_dex(const char *arc, const char *entry, size_t *len) {
    if (entry && entry[0]) return ej_read_zip_entry(arc, entry, len);
    return ej_read_file(arc, len, 0);
}

/* Pull string #idx straight out of the string table (no table build). */
static char *string_at(const uint8_t *dex, size_t size, uint32_t str_off, uint32_t str_count,
                       uint32_t idx) {
    if (idx >= str_count) return NULL;
    size_t slot = (size_t)str_off + (size_t)idx * 4;
    if (slot + 4 > size) return NULL;
    size_t p = rd32(dex + slot);
    if (p >= size) return NULL;
    while (p < size && (dex[p] & 0x80)) p++;  /* uleb128 utf16 length */
    p++;
    if (p >= size) return NULL;
    size_t q = p;
    while (q < size && dex[q] != 0) q++;
    size_t n = q - p;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, dex + p, n);
    out[n] = '\0';
    return out;
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_dexClasses(JNIEnv *env, jobject thiz, jstring jarc,
                                           jstring jentry) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    jobjectArray arr = NULL;

    if (arc) {
        size_t len = 0;
        uint8_t *dex = load_dex(arc, entry, &len);
        if (dex && len > 0x70 && memcmp(dex, "dex\n", 4) == 0) {
            uint32_t str_count = rd32(dex + 0x38), str_off = rd32(dex + 0x3C);
            uint32_t type_count = rd32(dex + 0x40), type_off = rd32(dex + 0x44);
            uint32_t class_count = rd32(dex + 0x60), class_off = rd32(dex + 0x64);
            if (class_count < 200000) {
                const char **items = (const char **)calloc(class_count ? class_count : 1,
                                                          sizeof(char *));
                int kept = 0;
                for (uint32_t i = 0; items && i < class_count; i++) {
                    size_t cdef = (size_t)class_off + (size_t)i * 32;
                    if (cdef + 32 > len) break;
                    uint32_t class_idx = rd32(dex + cdef);
                    if (class_idx >= type_count) continue;
                    size_t toff = (size_t)type_off + (size_t)class_idx * 4;
                    if (toff + 4 > len) break;
                    char *desc = string_at(dex, len, str_off, str_count, rd32(dex + toff));
                    items[kept++] = desc ? desc : ej_dup("?");
                }
                if (items) {
                    arr = ej_jstr_array(env, items, kept);
                    for (int i = 0; i < kept; i++) free((void *)items[i]);
                    free(items);
                }
            }
        }
        free(dex);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return arr;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexSmali(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry,
                                         jint index) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    jstring out = NULL;

    if (arc && index >= 0) {
        size_t len = 0;
        uint8_t *data = load_dex(arc, entry, &len);
        if (data) {
            dex_ctx *ctx = dex_parse(data, len);
            if (ctx) {
                char *smali = dex_to_smali_class(ctx, (uint32_t)index);
                if (smali) {
                    out = ej_jstr(env, smali);
                    free(smali);
                }
                dex_free(ctx);
            }
            free(data);
        }
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return out ? out : (*env)->NewStringUTF(env, "");
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexDump(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    jstring out = NULL;

    if (arc) {
        size_t len = 0;
        uint8_t *data = load_dex(arc, entry, &len);
        if (data) {
            dex_ctx *ctx = dex_parse(data, len);
            if (ctx) {
                char *dump = dex_dump(ctx);
                if (dump) {
                    out = ej_jstr(env, dump);
                    free(dump);
                }
                dex_free(ctx);
            }
            free(data);
        }
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return out ? out : (*env)->NewStringUTF(env, "");
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_smaliAssemble(JNIEnv *env, jobject thiz, jstring jsrc,
                                              jstring jout) {
    (void)thiz;
    const char *src = (*env)->GetStringUTFChars(env, jsrc, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;
    if (src && out) {
        int n = 0;
        int rc = smali_assemble_n(src, out, &n);
        char detail[128];
        snprintf(detail, sizeof(detail), "%d dex written", n);
        res = ej_jresult(env, rc, detail);
    }
    if (src) (*env)->ReleaseStringUTFChars(env, jsrc, src);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return res ? res : ej_jresult(env, -1, "bad arguments");
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexRepair(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    const char *in = (*env)->GetStringUTFChars(env, jin, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jstring res = NULL;
    if (in && out) {
        size_t len = 0;
        uint8_t *data = ej_read_file(in, &len, 0);
        char changes[256] = {0};
        if (data && len > 0) {
            size_t out_len = 0;
            uint8_t *fixed = dex_repair(data, len, &out_len, changes, sizeof(changes));
            if (fixed && ej_write_file(out, fixed, out_len) == 0)
                res = ej_jresult(env, 0, changes[0] ? changes : "already valid");
            else
                res = ej_jresult(env, -1, "repair failed");
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
