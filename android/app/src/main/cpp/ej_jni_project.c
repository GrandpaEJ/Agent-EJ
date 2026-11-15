/* Whole-APK pipelines: decode to a project tree, rebuild + sign it, merge a
 * split set (anti-split), repair a tampered APK and kill signature checks. */
#include "ej_jni_util.h"
#include "formats.h"
#include "sigkill.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const char *a, *b; } args2_t;

static int get2(JNIEnv *env, jstring ja, jstring jb, args2_t *o) {
    o->a = ja ? (*env)->GetStringUTFChars(env, ja, NULL) : NULL;
    o->b = jb ? (*env)->GetStringUTFChars(env, jb, NULL) : NULL;
    return o->a && o->b;
}

static void put2(JNIEnv *env, jstring ja, jstring jb, args2_t *o) {
    if (o->a) (*env)->ReleaseStringUTFChars(env, ja, o->a);
    if (o->b) (*env)->ReleaseStringUTFChars(env, jb, o->b);
}

/* A scratch path under the app's private dir, unique per call. */
static void scratch(char *buf, size_t cap, const char *tag) {
    static unsigned seq;
    snprintf(buf, cap, "%s/%s%ld_%u", ej_ctx()->data_dir, tag, (long)getpid(), ++seq);
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_apkDecode(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    args2_t a;
    jstring res;
    if (!get2(env, jin, jout, &a)) res = ej_jresult(env, -1, "bad arguments");
    else {
        mkdir(a.b, 0755);
        int rc = apk_decode(a.a, a.b);
        res = ej_jresult(env, rc, rc == 0 ? "decoded: smali, manifest, resources" : "decode failed");
    }
    put2(env, jin, jout, &a);
    return res;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_apkBuild(JNIEnv *env, jobject thiz, jstring jdir, jstring jout) {
    (void)thiz;
    args2_t a;
    jstring res;
    if (!get2(env, jdir, jout, &a)) res = ej_jresult(env, -1, "bad arguments");
    else if (!ej_keys_ready()) res = ej_jresult(env, -1, "debug key unavailable");
    else {
        ej_ctx_t *c = ej_ctx();
        int rc = apk_build(a.a, a.b, c->key_path, c->cert_path);
        res = ej_jresult(env, rc, rc == 0 ? "built, aligned and signed" : "build failed");
    }
    put2(env, jdir, jout, &a);
    return res;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_apkMerge(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    args2_t a;
    jstring res;
    if (!get2(env, jin, jout, &a)) res = ej_jresult(env, -1, "bad arguments");
    else if (!ej_keys_ready()) res = ej_jresult(env, -1, "debug key unavailable");
    else {
        ej_ctx_t *c = ej_ctx();
        int rc = apk_merge(a.a, a.b, c->key_path, c->cert_path);
        res = ej_jresult(env, rc, rc == 0 ? "splits merged into one signed APK" : "merge failed");
    }
    put2(env, jin, jout, &a);
    return res;
}

JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_apkRepair(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    args2_t a;
    jstring res;
    if (!get2(env, jin, jout, &a)) res = ej_jresult(env, -1, "bad arguments");
    else if (!ej_keys_ready()) res = ej_jresult(env, -1, "debug key unavailable");
    else {
        char tmp[700], report[384] = {0};
        scratch(tmp, sizeof(tmp), "repair");
        int n = apk_deobf_repair(a.a, tmp, report, sizeof(report));
        if (n < 0) res = ej_jresult(env, -1, "repair failed");
        else {
            ej_ctx_t *c = ej_ctx();
            int rc = apk_sign_pipeline(tmp, a.b, c->key_path, c->cert_path);
            char msg[480];
            for (char *p = report; *p; p++) if (*p == '\n') *p = ';';
            snprintf(msg, sizeof(msg), "%d entr%s repaired%s%s", n, n == 1 ? "y" : "ies",
                     report[0] ? ": " : "", report);
            res = ej_jresult(env, rc, rc == 0 ? msg : "signing failed");
        }
        unlink(tmp);
    }
    put2(env, jin, jout, &a);
    return res;
}

/* Heuristic class/member renaming on a decoded tree (analysis aid). */
JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_deobfRename(JNIEnv *env, jobject thiz, jstring jdir) {
    (void)thiz;
    const char *dir = (*env)->GetStringUTFChars(env, jdir, NULL);
    if (!dir) return ej_jresult(env, -1, "bad arguments");
    char report[256] = {0};
    int n = deobf_heuristic_apply(dir, report, sizeof(report));
    (*env)->ReleaseStringUTFChars(env, jdir, dir);
    char msg[320];
    snprintf(msg, sizeof(msg), "%d file(s) rewritten %s", n < 0 ? 0 : n, report);
    return ej_jresult(env, n < 0 ? -1 : 0, n < 0 ? "deobfuscation failed" : msg);
}

/* APK -> decode -> patch every signature-check pattern -> rebuild + sign. */
JNIEXPORT jstring JNICALL
Java_org_agentej_android_Native_sigKill(JNIEnv *env, jobject thiz, jstring jin, jstring jout) {
    (void)thiz;
    args2_t a;
    jstring res;
    if (!get2(env, jin, jout, &a)) { put2(env, jin, jout, &a); return ej_jresult(env, -1, "bad arguments"); }
    if (!ej_keys_ready()) { put2(env, jin, jout, &a); return ej_jresult(env, -1, "debug key unavailable"); }
    ej_ctx_t *c = ej_ctx();
    char tree[700];
    scratch(tree, sizeof(tree), "sigkill");
    mkdir(tree, 0755);
    if (apk_decode(a.a, tree) != 0) {
        res = ej_jresult(env, -1, "decode failed");
    } else {
        sigkill_opts o;
        memset(&o, 0, sizeof(o));
        o.src_dir = tree;
        o.orig_apk = a.a;
        o.do_scan = o.do_stub = o.do_pkg = 1;
        o.do_call = o.do_sigobj = o.do_checkapi = o.do_zipcert = 1;
        sigkill_report r;
        sigkill_report_init(&r);
        int rc = sigkill_run(&o, &r);
        if (rc == 0 && apk_build(tree, a.b, c->key_path, c->cert_path) != 0) rc = -2;
        char msg[256];
        snprintf(msg, sizeof(msg), "%d check(s) found; %d stubbed, %d pkg, %d call site(s) patched",
                 r.checks_found, r.methods_stubbed, r.pkg_sites_patched, r.call_sites_patched);
        res = ej_jresult(env, rc, rc == 0 ? msg : rc == -2 ? "rebuild failed" : "no original certificate found");
        sigkill_report_free(&r);
    }
    ej_rm_rf(tree);
    put2(env, jin, jout, &a);
    return res;
}
