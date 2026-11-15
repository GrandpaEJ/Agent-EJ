/* AXML tools: decode binary XML to text and encode text back to binary. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load_source(const char *arc, const char *entry, size_t *len) {
    if (entry && entry[0]) return ej_read_zip_entry(arc, entry, len);
    return ej_read_file(arc, len, 0);
}

/* The resources.arsc that names this XML's ids, as MT does: the APK's own when
 * the XML is inside one, else one sitting in the same folder as the file. */
static uint8_t *load_arsc(const char *arc, const char *entry, size_t *len) {
    if (entry && entry[0]) return ej_read_zip_entry(arc, "resources.arsc", len);
    const char *slash = strrchr(arc, '/');
    size_t dl = slash ? (size_t)(slash - arc) + 1 : 0;
    char *p = malloc(dl + sizeof("resources.arsc"));
    if (!p) return NULL;
    memcpy(p, arc, dl);
    strcpy(p + dl, "resources.arsc");
    uint8_t *d = ej_read_file(p, len, 0);
    free(p);
    return d;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_axmlDecode(JNIEnv *env, jobject thiz, jstring jarc,
                                           jstring jentry) {
    (void)thiz;
    const char *arc = (*env)->GetStringUTFChars(env, jarc, NULL);
    const char *entry = jentry ? (*env)->GetStringUTFChars(env, jentry, NULL) : NULL;
    jstring out = NULL;

    if (arc) {
        size_t len = 0;
        uint8_t *data = load_source(arc, entry, &len);
        if (data && len > 0) {
            axml_ctx *ax = axml_parse(data, len);
            if (ax) {
                size_t alen = 0;
                uint8_t *adata = load_arsc(arc, entry, &alen);
                arsc_ctx *rs = adata && alen ? arsc_parse(adata, alen) : NULL;
                if (rs) axml_set_arsc(ax, rs);
                char *xml = axml_get_xml(ax);  /* owned by the context, do not free */
                if (xml) out = ej_jstr(env, xml);
                axml_free(ax);
                if (rs) arsc_free(rs);
                free(adata);
            }
        }
        free(data);
    }

    if (arc) (*env)->ReleaseStringUTFChars(env, jarc, arc);
    if (entry) (*env)->ReleaseStringUTFChars(env, jentry, entry);
    if (!out) out = (*env)->NewStringUTF(env, "");
    return out;
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_axmlEncode(JNIEnv *env, jobject thiz, jstring jxml, jstring jout) {
    (void)thiz;
    const char *xml = (*env)->GetStringUTFChars(env, jxml, NULL);
    const char *out = (*env)->GetStringUTFChars(env, jout, NULL);
    int rc = -1;
    if (xml && out) rc = axml_assemble(xml, out);
    if (xml) (*env)->ReleaseStringUTFChars(env, jxml, xml);
    if (out) (*env)->ReleaseStringUTFChars(env, jout, out);
    return rc;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_axmlRepair(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
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
            uint8_t *fixed = axml_repair(data, len, &out_len, changes, sizeof(changes));
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
