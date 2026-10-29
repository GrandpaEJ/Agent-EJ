/* Tool binding for the native signature killer.  Accepts either a decoded
 * tree (patch in place / rebuild) or an APK (decode -> patch -> rebuild). */
#include "tools_internal.h"
#include "sigkill.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *arg_str(cJSON *a, const char *k1, const char *k2) {
    cJSON *o = cJSON_GetObjectItem(a, k1);
    if ((!o || !cJSON_IsString(o)) && k2) o = cJSON_GetObjectItem(a, k2);
    return (o && cJSON_IsString(o)) ? o->valuestring : NULL;
}

static int arg_int(cJSON *a, const char *k, int def) {
    cJSON *o = cJSON_GetObjectItem(a, k);
    if (!o) return def;
    if (cJSON_IsBool(o)) return cJSON_IsTrue(o) ? 1 : 0;
    if (cJSON_IsNumber(o)) return o->valueint;
    if (cJSON_IsString(o)) {
        const char *s = o->valuestring;
        return (strcmp(s, "1") == 0 || strcmp(s, "true") == 0) ? 1 : 0;
    }
    return def;
}

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void parse_mode(const char *mode, sigkill_opts *o) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", mode ? mode : "all");
    int any = 0;
    for (char *tok = strtok(buf, ", "); tok; tok = strtok(NULL, ", ")) {
        if (strcmp(tok, "scan") == 0)        { o->do_scan = 1; any = 1; }
        else if (strcmp(tok, "stub") == 0)   { o->do_stub = 1; any = 1; }
        else if (strcmp(tok, "pkg") == 0 ||
                 strcmp(tok, "runtime") == 0){ o->do_pkg = 1; any = 1; }
        else if (strcmp(tok, "const") == 0 ||
                 strcmp(tok, "static") == 0){ o->do_const = 1; any = 1; }
        else if (strcmp(tok, "call") == 0 ||
                 strcmp(tok, "wrap") == 0)  { o->do_call = 1; any = 1; }
        else if (strcmp(tok, "sigobj") == 0 ||
                 strcmp(tok, "sigapi") == 0) { o->do_sigobj = 1; any = 1; }
        else if (strcmp(tok, "checkapi") == 0 ||
                 strcmp(tok, "check") == 0)  { o->do_checkapi = 1; any = 1; }
        else if (strcmp(tok, "zipcert") == 0 ||
                 strcmp(tok, "archive") == 0) { o->do_zipcert = 1; any = 1; }
        else if (strcmp(tok, "native") == 0){ o->do_native = 1; any = 1; }
        else if (strcmp(tok, "all") == 0) {
            o->do_scan = o->do_stub = o->do_pkg = 1;
            o->do_call = o->do_sigobj = o->do_checkapi = o->do_zipcert = 1;
            any = 1;
        }
    }
    if (!any) {
        o->do_scan = o->do_stub = o->do_pkg = 1;
        o->do_call = o->do_sigobj = o->do_checkapi = o->do_zipcert = 1;
    }
}

/* apk_build treats cert= as raw DER; accept PEM/PKCS#7 here by
 * normalising to a temporary DER file. */
static const char *cert_for_signer(const char *cert, char *tmp, size_t cap) {
    if (!cert) return NULL;
    sigkill_cert c;
    memset(&c, 0, sizeof(c));
    if (sigkill_cert_load_file(&c, cert) != 0) return cert;
    snprintf(tmp, cap, "/tmp/opencode/sigcertXXXXXX");
    int fd = mkstemp(tmp);
    const char *use = cert;
    if (fd >= 0) {
        ssize_t n = write(fd, c.old_der, c.old_len);
        close(fd);
        if (n == (ssize_t)c.old_len) use = tmp;
        else unlink(tmp);
    }
    sigkill_cert_free(&c);
    return use;
}

static char *report_json(const sigkill_report *r, const char *mode,
                         const char *output, int rc) {
    cJSON *res = cJSON_CreateObject();
    if (rc != 0) {
        cJSON_AddNumberToObject(res, "status", 1);
        cJSON_AddBoolToObject(res, "ok", 0);
        cJSON_AddStringToObject(res, "error",
            rc == -2 ? "signature killer patched the tree but signing the output APK failed"
                     : "signature killer failed (no original certificate, or tree unreadable)");
    } else {
        cJSON_AddNumberToObject(res, "status", 0);
        cJSON_AddBoolToObject(res, "ok", 1);
    }
    if (mode) cJSON_AddStringToObject(res, "mode", mode);
    if (output) cJSON_AddStringToObject(res, "output", output);

    cJSON *counts = cJSON_CreateObject();
    cJSON_AddNumberToObject(counts, "files_scanned", r->files_scanned);
    cJSON_AddNumberToObject(counts, "methods_scanned", r->methods_scanned);
    cJSON_AddNumberToObject(counts, "checks_found", r->checks_found);
    cJSON_AddNumberToObject(counts, "methods_stubbed", r->methods_stubbed);
    cJSON_AddNumberToObject(counts, "pkg_sites_patched", r->pkg_sites_patched);
    cJSON_AddNumberToObject(counts, "call_sites_patched", r->call_sites_patched);
    cJSON_AddNumberToObject(counts, "sigobj_sites_patched", r->sigobj_sites_patched);
    cJSON_AddNumberToObject(counts, "checkapi_sites_patched", r->checkapi_sites_patched);
    cJSON_AddNumberToObject(counts, "zipcert_files", r->zipcert_files);
    cJSON_AddNumberToObject(counts, "zipcert_consts", r->zipcert_consts);
    cJSON_AddNumberToObject(counts, "consts_replaced", r->consts_replaced);
    cJSON_AddNumberToObject(counts, "const_files", r->const_files);
    cJSON_AddNumberToObject(counts, "native_files_scanned", r->native_files_scanned);
    cJSON_AddNumberToObject(counts, "native_files_patched", r->native_files_patched);
    cJSON_AddNumberToObject(counts, "native_hits", r->native_hits);
    cJSON_AddBoolToObject(counts, "helper_injected", r->helper_injected != 0);
    cJSON_AddBoolToObject(counts, "findings_truncated", r->findings_truncated != 0);
    cJSON_AddItemToObject(res, "counts", counts);

    if (r->details && r->details_len) {
        cJSON *findings = NULL;
        if (r->details[r->details_len - 1] == ']') {
            findings = cJSON_Parse(r->details);
        } else {
            char *fixed = malloc(r->details_len + 2);
            if (fixed) {
                memcpy(fixed, r->details, r->details_len);
                fixed[r->details_len] = ']';
                fixed[r->details_len + 1] = '\0';
                findings = cJSON_Parse(fixed);
                free(fixed);
            }
        }
        if (findings) cJSON_AddItemToObject(res, "findings", findings);
    }
    char *out = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return out;
}

char *execute_sigkill(cJSON *args) {
    const char *path = arg_str(args, "path", "input");
    if (!path) return strdup("{\"status\": 1, \"error\": \"Missing path\"}");

    const char *out = arg_str(args, "out", "output");
    const char *orig = arg_str(args, "orig", "orig_apk");
    const char *orig_cert = arg_str(args, "orig_cert", "orig_cert_path");
    const char *key = arg_str(args, "key", "key_path");
    const char *cert = arg_str(args, "cert", "cert_path");
    const char *mode = arg_str(args, "mode", NULL);
    const char *extra = arg_str(args, "names", NULL);
    int dry = arg_int(args, "dry_run", 0) || arg_int(args, "scan_only", 0);
    int aggressive = arg_int(args, "aggressive", 0);

    char default_out[4096];
    int input_is_dir = is_dir(path);
    if (!out && !input_is_dir) {
        snprintf(default_out, sizeof(default_out), "%s.sigkill.apk", path);
        out = default_out;
    }

    char tmp[4096] = "";
    const char *tree = path;
    int cleanup = 0;
    if (!input_is_dir) {
        snprintf(tmp, sizeof(tmp), "/tmp/opencode/sigkillXXXXXX");
        if (!mkdtemp(tmp))
            return strdup("{\"status\": 1, \"error\": \"Cannot create temp dir\"}");
        if (apk_decode(path, tmp) != 0) {
            sigkill_rm_rf(tmp);
            return strdup("{\"status\": 1, \"error\": \"Failed to decode APK\"}");
        }
        tree = tmp;
        cleanup = 1;
        if (!orig) orig = path;
    }

    sigkill_opts o;
    memset(&o, 0, sizeof(o));
    o.src_dir = tree;
    o.orig_apk = orig;
    o.orig_cert = orig_cert;
    o.cert_path = cert;
    o.extra_names = extra;
    o.dry_run = dry;
    o.aggressive = aggressive;
    parse_mode(mode, &o);
    if (o.do_const) {
        /* static rewrite and runtime-original hooks are opposite strategies */
        o.do_pkg = o.do_call = o.do_sigobj = o.do_checkapi = o.do_zipcert = 0;
    }

    sigkill_report rep;
    sigkill_report_init(&rep);
    int rc = sigkill_run(&o, &rep);

    char out_buf[4096];
    const char *output = NULL;
    if (rc == 0 && !dry && out) {
        char default_key[4096];
        if (!key && resolve_default_key_path(default_key, sizeof(default_key)))
            key = default_key;
        char cert_tmp[64] = "";
        const char *sign_cert = cert_for_signer(cert, cert_tmp, sizeof(cert_tmp));
        snprintf(out_buf, sizeof(out_buf), "%s", out);
        if (apk_build(tree, out_buf, key, sign_cert) != 0) rc = -2;
        else output = out_buf;
        if (cert_tmp[0]) unlink(cert_tmp);
    }

    char *json = report_json(&rep, mode ? mode : "all", output, rc);
    sigkill_report_free(&rep);
    if (cleanup) sigkill_rm_rf(tmp);
    return json;
}
