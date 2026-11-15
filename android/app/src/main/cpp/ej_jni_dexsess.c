/* Dex editor session: one dex parsed once and kept open behind a handle, so
 * opening class after class does not unzip and re-parse the whole dex on
 * every tap. Kotlin keeps at most one session open at a time. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* Dex editor save (hammer). edits are parallel arrays: the dex entry each
 * edited class lives in ("" for a bare .dex file), its class_def index (-1
 * for a class new to that dex) and its smali text. Every touched dex is
 * rebuilt; for an APK the dexes are swapped in and the result signed to out,
 * for a bare .dex the rebuilt dex is written to out. */
static int patch_one(const char *arc, const char *entry, int n, const char **ents, const jint *idx,
                     const char **texts, const char *out_dex) {
    size_t len = 0;
    uint8_t *data = entry[0] ? ej_read_zip_entry(arc, entry, &len) : ej_read_file(arc, &len, 0);
    if (!data) return -1;
    uint32_t *skip = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(n ? n : 1));
    const char **mine = (const char **)malloc(sizeof(char *) * (size_t)(n ? n : 1));
    int ns = 0, nt = 0, rc = -1;
    if (skip && mine) {
        for (int i = 0; i < n; i++) {
            if (strcmp(ents[i], entry) != 0) continue;
            if (idx[i] >= 0) skip[ns++] = (uint32_t)idx[i];
            mine[nt++] = texts[i];
        }
        rc = smali_patch_dex(data, len, skip, ns, mine, nt, out_dex);
    }
    free(skip);
    free(mine);
    free(data);
    return rc;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexSave(JNIEnv *env, jobject thiz, jstring jarc, jobjectArray jents,
                                        jintArray jidx, jobjectArray jtexts, jstring jout) {
    (void)thiz;
    int n = (*env)->GetArrayLength(env, jents);
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    const char **ents = (const char **)calloc((size_t)n + 1, sizeof(char *));
    const char **texts = (const char **)calloc((size_t)n + 1, sizeof(char *));
    jint *idx = (*env)->GetIntArrayElements(env, jidx, NULL);
    char detail[300] = "out of memory";
    int rc = -1;

    for (int i = 0; ents && texts && i < n; i++) {
        jstring e = (jstring)(*env)->GetObjectArrayElement(env, jents, i);
        jstring t = (jstring)(*env)->GetObjectArrayElement(env, jtexts, i);
        ents[i] = (*env)->GetStringUTFChars(env, e, NULL);
        texts[i] = (*env)->GetStringUTFChars(env, t, NULL);
    }
    if (arc && out && ents && texts && idx && n > 0) {
        char dexes[16][700];
        const char *names[16], *files[16];
        int nd = 0;
        rc = 0;
        for (int i = 0; i < n && rc == 0; i++) {
            int seen = 0;
            for (int k = 0; k < nd; k++) seen |= strcmp(names[k], ents[i]) == 0;
            if (seen) continue;
            if (nd == 16) { rc = -1; snprintf(detail, sizeof(detail), "too many dex files"); break; }
            snprintf(dexes[nd], sizeof(dexes[nd]), "%s/dexsave%ld_%d.dex", ej_ctx()->data_dir, (long)getpid(), nd);
            names[nd] = ents[i];
            files[nd] = dexes[nd];
            rc = patch_one(arc, ents[i], n, ents, idx, texts, dexes[nd]);
            if (rc != 0) snprintf(detail, sizeof(detail), "%s: %s", ents[i][0] ? ents[i] : "dex", smali_last_error());
            nd++;
        }
        if (rc == 0 && !ents[0][0]) {
            size_t len = 0;
            uint8_t *d = ej_read_file(files[0], &len, 0);
            rc = d ? ej_write_file(out, d, len) : -1;
            free(d);
            snprintf(detail, sizeof(detail), rc == 0 ? "dex rebuilt" : "cannot write output");
        } else if (rc == 0) {
            char tmp[700];
            snprintf(tmp, sizeof(tmp), "%s/dexsave%ld.zip", ej_ctx()->data_dir, (long)getpid());
            if (!ej_keys_ready()) rc = -1, snprintf(detail, sizeof(detail), "debug key unavailable");
            else if (apk_replace_entries(arc, tmp, names, files, nd) != 0)
                rc = -1, snprintf(detail, sizeof(detail), "cannot write the new APK");
            else if ((rc = apk_sign_pipeline(tmp, out, ej_ctx()->key_path, ej_ctx()->cert_path)) != 0)
                snprintf(detail, sizeof(detail), "signing failed rc=%d", rc);
            else snprintf(detail, sizeof(detail), "%d dex rebuilt, signed", nd);
            unlink(tmp);
        }
        for (int k = 0; k < nd; k++) unlink(files[k]);
    }

    for (int i = 0; ents && texts && i < n; i++) {
        jstring e = (jstring)(*env)->GetObjectArrayElement(env, jents, i);
        jstring t = (jstring)(*env)->GetObjectArrayElement(env, jtexts, i);
        if (ents[i]) (*env)->ReleaseStringUTFChars(env, e, ents[i]);
        if (texts[i]) (*env)->ReleaseStringUTFChars(env, t, texts[i]);
    }
    free(ents);
    free(texts);
    if (idx) (*env)->ReleaseIntArrayElements(env, jidx, idx, JNI_ABORT);
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return ej_jresult(env, rc, detail);
}

/* Dex editor Strings tab: the whole string pool of one dex, in pool order. */
JNIEXPORT jobjectArray JNICALL
Java_org_agentej_android_Native_dexStrings(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    jobjectArray arr = NULL;
    if (arc) {
        size_t len = 0;
        uint8_t *data = (entry && entry[0]) ? ej_read_zip_entry(arc, entry, &len) : ej_read_file(arc, &len, 0);
        dex_ctx *ctx = data ? dex_parse(data, len) : NULL;
        uint32_t n = dex_string_count(ctx);
        const char **items = ctx ? (const char **)malloc(sizeof(char *) * (n ? n : 1)) : NULL;
        if (items) {
            for (uint32_t i = 0; i < n; i++) items[i] = dex_string_at(ctx, i);
            arr = ej_jstr_array(env, items, (int)n);
            free(items);
        }
        if (ctx) dex_free(ctx);
        free(data);
    }
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return arr;
}
