/* Dex editor session: one dex parsed once and kept open behind a handle, so
 * opening class after class does not unzip and re-parse the whole dex on
 * every tap. Kotlin keeps at most one session open at a time. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;   /* dex bytes; dex_ctx points into them */
    dex_ctx *ctx;
} dex_session;

JNIEXPORT jlong JNICALL
Java_org_agentej_android_Native_dexOpen(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    dex_session *s = NULL;

    if (arc) {
        size_t len = 0;
        uint8_t *data = (entry && entry[0]) ? ej_read_zip_entry(arc, entry, &len)
                                            : ej_read_file(arc, &len, 0);
        dex_ctx *ctx = data ? dex_parse(data, len) : NULL;
        if (ctx && (s = (dex_session *)malloc(sizeof(*s))) != NULL) {
            s->data = data;
            s->ctx = ctx;
        } else {
            if (ctx) dex_free(ctx);
            free(data);
        }
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return (jlong)(intptr_t)s;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexClassSmali(JNIEnv *env, jobject thiz, jlong h, jint index) {
    (void)thiz;
    dex_session *s = (dex_session *)(intptr_t)h;
    jstring out = NULL;
    if (s && index >= 0) {
        char *smali = dex_to_smali_class(s->ctx, (uint32_t)index);
        if (smali) {
            out = ej_jstr(env, smali);
            free(smali);
        }
    }
    return out ? out : (*env)->NewStringUTF(env, "");
}

JNIEXPORT void JNICALL
Java_org_agentej_android_Native_dexClose(JNIEnv *env, jobject thiz, jlong h) {
    (void)env; (void)thiz;
    dex_session *s = (dex_session *)(intptr_t)h;
    if (!s) return;
    dex_free(s->ctx);
    free(s->data);
    free(s);
}

/* Dex editor search over one dex: "classIdx\tline\ttext" per hit, at most
 * limit; null when the query is invalid (bad integer or regex). */
typedef struct {
    char **items;
    int n, cap;
} find_acc;

static int find_hit(void *u, uint32_t cls, uint32_t line, const char *text) {
    find_acc *a = (find_acc *)u;
    size_t len = strlen(text) + 24;
    char *row = (char *)malloc(len);
    if (!row) return 1;
    snprintf(row, len, "%u\t%u\t%s", cls, line, text);
    a->items[a->n++] = row;
    return a->n >= a->cap;
}

JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_dexFind(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry,
                                        jint kind, jstring jquery, jint flags, jstring jscope,
                                        jint limit) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    const char *query = (*env)->GetStringUTFChars(env, jquery, NULL);
    const char *scope = jscope ? (*env)->GetStringUTFChars(env, jscope, NULL) : NULL;
    jobjectArray arr = NULL;
    find_acc acc = { NULL, 0, limit > 0 ? limit : 1000 };

    if (arc && query && (acc.items = (char **)calloc((size_t)acc.cap, sizeof(char *)))) {
        size_t len = 0;
        uint8_t *data = (entry && entry[0]) ? ej_read_zip_entry(arc, entry, &len) : ej_read_file(arc, &len, 0);
        dex_ctx *ctx = data ? dex_parse(data, len) : NULL;
        int rc = ctx ? dex_find(ctx, kind, query, flags, scope, find_hit, &acc) : 0;
        if (rc == 0) arr = ej_jstr_array(env, (const char **)acc.items, acc.n);
        for (int i = 0; i < acc.n; i++) free(acc.items[i]);
        free(acc.items);
        if (ctx) dex_free(ctx);
        free(data);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    if (query) (*env)->ReleaseStringUTFChars(env, jquery, query);
    if (scope) (*env)->ReleaseStringUTFChars(env, jscope, scope);
    return arr;
}
