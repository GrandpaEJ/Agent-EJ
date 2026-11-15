/* Dex editor save (MT's hammer): rebuild every touched dex with the
 * editor's changes (edited, added and deleted classes, renames) and write
 * them back into a signed copy of the APK. Also the rename helper the
 * editor uses to show renamed classes before they are compiled. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A Java String[] as UTF-8 C strings (calloc'd array; NULL entries on failure). */
static const char **ej_jstrs_get(JNIEnv *env, jobjectArray arr) {
    int n = arr ? (*env)->GetArrayLength(env, arr) : 0;
    const char **out = (const char **)calloc((size_t)n + 1, sizeof(char *));
    for (int i = 0; out && i < n; i++) {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
        out[i] = s ? (*env)->GetStringUTFChars(env, s, NULL) : NULL;
        (*env)->DeleteLocalRef(env, s);
    }
    return out;
}

static void ej_jstrs_release(JNIEnv *env, jobjectArray arr, const char **strs) {
    int n = arr && strs ? (*env)->GetArrayLength(env, arr) : 0;
    for (int i = 0; i < n; i++) {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
        if (s && strs[i]) (*env)->ReleaseStringUTFChars(env, s, strs[i]);
        (*env)->DeleteLocalRef(env, s);
    }
    free(strs);
}

/* Dex editor save (hammer). edits are parallel arrays: the dex entry each
 * change lives in ("" for a bare .dex file), its class_def index and its
 * smali text. idx >= 0 with text replaces that class, with "" deletes it;
 * idx -1 adds a new class; idx -2 only marks the dex for rebuilding (a rename
 * touches every dex). Renames (from/to descriptors) apply to every class.
 * For an APK the rebuilt dexes are swapped in and the result signed to out,
 * for a bare .dex the rebuilt dex is written to out. */
typedef struct {
    const char **from, **to;
    int n;
} renames;

static int patch_one(const char *arc, const char *entry, int n, const char **ents, const jint *idx,
                     const char **texts, const renames *rn, const char *out_dex) {
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
            if (texts[i][0]) mine[nt++] = texts[i];
        }
        rc = smali_patch_dex(data, len, skip, ns, mine, nt, rn->from, rn->to, rn->n, out_dex);
    }
    free(skip);
    free(mine);
    free(data);
    return rc;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_dexSave(JNIEnv *env, jobject thiz, jstring jarc, jobjectArray jents,
                                        jintArray jidx, jobjectArray jtexts, jobjectArray jfrom,
                                        jobjectArray jto, jstring jout) {
    (void)thiz;
    renames rn = {ej_jstrs_get(env, jfrom), ej_jstrs_get(env, jto), (*env)->GetArrayLength(env, jfrom)};
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
            rc = patch_one(arc, ents[i], n, ents, idx, texts, &rn, dexes[nd]);
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
    ej_jstrs_release(env, jfrom, rn.from);
    ej_jstrs_release(env, jto, rn.to);
    if (idx) (*env)->ReleaseIntArrayElements(env, jidx, idx, JNI_ABORT);
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return ej_jresult(env, rc, detail);
}

/* Applies the editor's renames to one class text (for display). */
JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_smaliRename(JNIEnv *env, jobject thiz, jstring jtext, jobjectArray jfrom,
                                            jobjectArray jto) {
    (void)thiz;
    const char *text = (*env)->GetStringUTFChars(env, jtext, NULL);
    const char **from = ej_jstrs_get(env, jfrom), **to = ej_jstrs_get(env, jto);
    char *r = text && from && to ? smali_rename(text, from, to, (*env)->GetArrayLength(env, jfrom)) : NULL;
    jstring out = r ? (*env)->NewStringUTF(env, r) : jtext;
    free(r);
    ej_jstrs_release(env, jfrom, from);
    ej_jstrs_release(env, jto, to);
    if (text) (*env)->ReleaseStringUTFChars(env, jtext, text);
    return out;
}
