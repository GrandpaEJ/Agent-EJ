/* Signature-killer orchestrator: walks a decoded APK tree, applies the
 * selected smali/native techniques and injects the runtime hook class. */
#include "sigkill_internal.h"
#include "../apk/cert_der.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

struct smali_ctx {
    const sigkill_opts *o;
    sigkill_cert *c;
    sigkill_report *r;
};

static int smali_cb(const char *path, void *ud) {
    struct smali_ctx *ctx = ud;
    size_t pl = strlen(path);
    if (pl < 6 || strcmp(path + pl - 6, ".smali") != 0) return 0;
    ctx->r->files_scanned++;

    size_t len = 0;
    char *text = sk_read_file(path, &len);
    if (!text) return 0;
    int stubbed = 0;
    char *out = sk_process_smali(text, len, ctx->o, ctx->c, ctx->r, path,
                                 &stubbed);
    if (stubbed > 0) ctx->r->methods_stubbed += stubbed;
    if (out) {
        if (!ctx->o->dry_run) {
            if (sk_write_file(path, out, strlen(out)) != 0)
                sk_detail(ctx->r, "error", path, "write failed");
        }
        free(out);
    }
    free(text);
    return 0;
}

static int run_smali_tree(const char *root, struct smali_ctx *ctx) {
    DIR *d = opendir(root);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "smali", 5) != 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        if (sk_is_dir(path)) sk_walk(path, smali_cb, ctx);
    }
    closedir(d);
    return 0;
}

static int pick_smali_dir(const char *tree, char *out, size_t cap) {
    static const char *first[] = { "smali", "smali_classes" };
    for (size_t i = 0; i < sizeof(first) / sizeof(first[0]); i++) {
        snprintf(out, cap, "%s/%s", tree, first[i]);
        if (sk_is_dir(out)) return 0;
    }
    DIR *d = opendir(tree);
    if (!d) return -1;
    struct dirent *ent;
    int ok = -1;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "smali", 5) != 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", tree, ent->d_name);
        if (!sk_is_dir(path)) continue;
        snprintf(out, cap, "%s", path);
        ok = 0;
        break;
    }
    closedir(d);
    return ok;
}

static int inject_helper(const char *tree, sigkill_cert *c, sigkill_report *r) {
    char dir[4096];
    if (pick_smali_dir(tree, dir, sizeof(dir)) != 0) {
        sk_detail(r, "error", tree, "no smali directory for the hook class");
        return -1;
    }
    size_t len = 0;
    char *src = sk_helper_smali(c, &len);
    if (!src) return -1;
    char pkg[4096];
    snprintf(pkg, sizeof(pkg), "%s/com/agent/ej", dir);
    sk_mkdir_p(pkg);
    char path[4096];
    snprintf(path, sizeof(path), "%s/com/agent/ej/SigKiller.smali", dir);
    int rc = sk_write_file(path, src, len);
    free(src);
    if (rc != 0) {
        sk_detail(r, "error", path, "cannot write hook class");
        return -1;
    }
    r->helper_injected = 1;
    sk_detail(r, "helper", path, "runtime signature hook injected");
    return 0;
}

void sigkill_report_init(sigkill_report *r) {
    memset(r, 0, sizeof(*r));
}

void sigkill_report_free(sigkill_report *r) {
    if (!r) return;
    free(r->details);
    r->details = NULL;
    r->details_len = r->details_cap = 0;
}

int sigkill_run(const sigkill_opts *o, sigkill_report *r) {
    if (!o || !o->src_dir || !r) return -1;
    if (!sk_is_dir(o->src_dir)) return -1;

    int need_cert = o->do_pkg || o->do_const || o->do_stub || o->do_native ||
                    o->do_call || o->do_sigobj || o->do_checkapi ||
                    o->do_zipcert;
    sigkill_cert cert;
    memset(&cert, 0, sizeof(cert));
    if (sigkill_cert_load(&cert, o->orig_cert, o->orig_apk, o->src_dir) != 0 &&
        need_cert) {
        sk_detail(r, "error", o->src_dir,
                  "original certificate not found (pass orig_cert= or orig=)");
        return -1;
    }
    if (cert.old_der) {
        sk_detail(r, "cert", cert.old_source, "sha256=%s bytes=%zu",
                  cert.old_sha256, cert.old_len);
    }

    if (o->cert_path) {
        sigkill_cert target;
        memset(&target, 0, sizeof(target));
        if (sigkill_cert_load_file(&target, o->cert_path) == 0)
            sigkill_cert_set_target(&cert, target.old_der, target.old_len);
        sigkill_cert_free(&target);
    } else if (cert.old_der) {
        /* Default output signing key is the bundled test key. */
        sigkill_cert_set_target(&cert, cert_der, cert_der_len);
    }
    if (cert.new_der)
        sk_detail(r, "target", o->cert_path ? o->cert_path : "builtin testkey",
                  "sha256=%s bytes=%zu", cert.new_sha256, cert.new_len);

    if (cert.old_der) sigkill_compute_reps(&cert);

    struct smali_ctx ctx = { o, &cert, r };
    run_smali_tree(o->src_dir, &ctx);

    if (o->do_zipcert)
        sk_zipcert_run(o->src_dir, o->orig_apk, o->dry_run, r);

    if ((o->do_pkg || o->do_call || o->do_sigobj) && !o->dry_run && cert.sig_b64) {
        if (inject_helper(o->src_dir, &cert, r) != 0) {
            sigkill_cert_free(&cert);
            return -1;
        }
    }

    if (o->do_native && cert.old_der)
        sk_native_patch(o->src_dir, &cert, !o->dry_run, r);

    sigkill_cert_free(&cert);
    return 0;
}

void sigkill_rm_rf(const char *path) {
    if (!path) return;
    DIR *d = opendir(path);
    if (!d) { remove(path); return; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", path, ent->d_name);
        struct stat st;
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) sigkill_rm_rf(p);
        else remove(p);
    }
    closedir(d);
    rmdir(path);
}
