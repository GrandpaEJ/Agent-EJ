/* Translation mode: the <string> resources of a resources.arsc per locale,
 * edits written back into the table (and the APK, signed). */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The table: entry of the archive, or the file itself when entry is "". */
static arsc_tr *open_tr(JNIEnv *env, jstring jarc, jstring jentry) {
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = (*env)->GetStringUTFChars(env, jentry, NULL);
    arsc_tr *t = NULL;
    if (arc && entry) {
        size_t len = 0;
        uint8_t *d = entry[0] ? ej_read_zip_entry(arc, entry, &len) : ej_read_file(arc, &len, 0);
        if (d && len) t = arsc_tr_open(d, len);  /* the model copies what it keeps */
        free(d);
    }
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    return t;
}

static jstring take(JNIEnv *env, char *s) {
    jstring r = ej_jstr(env, s ? s : "");
    free(s);
    return r;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_arscLocales(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry) {
    (void)thiz;
    arsc_tr *t = open_tr(env, jarc, jentry);
    jstring r = take(env, t ? arsc_tr_locales(t) : NULL);
    arsc_tr_free(t);
    return r;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_arscStringsIn(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry,
                                              jstring jqual) {
    (void)thiz;
    arsc_tr *t = open_tr(env, jarc, jentry);
    const char *qual = (*env)->GetStringUTFChars(env, jqual, NULL);
    jstring r = take(env, t && qual ? arsc_tr_dump(t, qual) : NULL);
    if (qual) (*env)->ReleaseStringUTFChars(env, jqual, qual);
    arsc_tr_free(t);
    return r;
}

/* Writes the edited table: into a signed copy of the APK at out, or to out
 * for a bare resources.arsc. */
static int write_out(arsc_tr *t, const char *arc, const char *entry, const char *out, char *why, size_t wn) {
    size_t n = 0;
    uint8_t *data = arsc_tr_encode(t, &n);
    if (!data) { snprintf(why, wn, "cannot encode the table"); return -1; }
    int rc;
    if (!entry[0]) {
        rc = ej_write_file(out, data, n);
        snprintf(why, wn, rc == 0 ? "saved" : "cannot write %s", out);
        free(data);
        return rc;
    }
    char bin[700], tmp[700];
    snprintf(bin, sizeof(bin), "%s/arscsave%ld.arsc", ej_ctx()->data_dir, (long)getpid());
    snprintf(tmp, sizeof(tmp), "%s/arscsave%ld.zip", ej_ctx()->data_dir, (long)getpid());
    const char *names[1] = {entry}, *files[1] = {bin};
    if ((rc = ej_write_file(bin, data, n)) != 0) snprintf(why, wn, "cannot write the table");
    else if (!ej_keys_ready()) rc = -1, snprintf(why, wn, "debug key unavailable");
    else if (apk_replace_entries(arc, tmp, names, files, 1) != 0) rc = -1, snprintf(why, wn, "cannot write the new APK");
    else if ((rc = apk_sign_pipeline(tmp, out, ej_ctx()->key_path, ej_ctx()->cert_path)) != 0)
        snprintf(why, wn, "signing failed rc=%d", rc);
    else snprintf(why, wn, "saved, signed");
    unlink(bin);
    unlink(tmp);
    free(data);
    return rc;
}

/* ids/texts are parallel: the new text of each string in qual. */
JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_arscTranslate(JNIEnv *env, jobject thiz, jstring jarc, jstring jentry,
                                              jstring jqual, jintArray jids, jobjectArray jtexts,
                                              jstring jout) {
    (void)thiz;
    arsc_tr *t = open_tr(env, jarc, jentry);
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = (*env)->GetStringUTFChars(env, jentry, NULL);
    const char *qual = (*env)->GetStringUTFChars(env, jqual, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    jint *ids = (*env)->GetIntArrayElements(env, jids, NULL);
    int n = (*env)->GetArrayLength(env, jids);
    char why[300] = "cannot read the resource table";
    int rc = -1, changed = 0;
    if (t && arc && entry && qual && out && ids) {
        rc = 0;
        for (int i = 0; i < n && rc == 0; i++) {
            jstring js = (jstring)(*env)->GetObjectArrayElement(env, jtexts, i);
            const char *s = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
            int r = s ? arsc_tr_set(t, (uint32_t)ids[i], qual, s) : -1;
            if (r < 0) rc = -1, snprintf(why, sizeof(why), "cannot set %08x in %s", (unsigned)ids[i], qual[0] ? qual : "default");
            changed += r > 0;
            if (s) (*env)->ReleaseStringUTFChars(env, js, s);
            (*env)->DeleteLocalRef(env, js);
        }
        if (rc == 0) rc = write_out(t, arc, entry, out, why, sizeof(why));
        if (rc == 0) {
            char done[340];
            snprintf(done, sizeof(done), "%d string%s %s", changed, changed == 1 ? "" : "s", why);
            snprintf(why, sizeof(why), "%s", done);
        }
    }
    if (ids) (*env)->ReleaseIntArrayElements(env, jids, ids, JNI_ABORT);
    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    if (qual) (*env)->ReleaseStringUTFChars(env, jqual, qual);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    arsc_tr_free(t);
    return ej_jresult(env, rc, why);
}
