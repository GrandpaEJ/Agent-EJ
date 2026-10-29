#ifndef SIGKILL_H
#define SIGKILL_H

/* Native APK signature-verification killer.
 *
 * Five independent techniques, mirroring what MT Manager / NP Manager expose
 * as separate "signature killer" modes:
 *
 *   scan   - report every signature-check method and signature constant
 *   stub   - replace boolean/int/String signature-check bodies with a pass
 *   pkg    - inject a runtime hook that makes PackageInfo/SigningInfo report
 *            the ORIGINAL certificate again (works without knowing digests)
 *   const  - rewrite hardcoded certificate digests (MD5/SHA-1/SHA-256, hex,
 *            base64, toCharsString, hashCode) from the old to the new cert
 *   native - patch the same digests embedded in the lib and assets directories
 *
 * `pkg` and `const`/`native` are opposite strategies: `pkg` keeps the app
 * seeing the original signature, while `const` teaches the app that the new
 * signature is the real one.  Do not combine them for the same app.
 */

#include <stddef.h>
#include <stdint.h>

#define SIGKILL_MAX_REPS 40

typedef struct {
    char *oldv;          /* value to find */
    char *newv;          /* replacement (NULL = report only) */
    char  kind[20];      /* cert_hex, md5_b64, sha256_hex, hashcode, ... */
} sigkill_rep;

typedef struct {
    uint8_t *old_der;    /* original certificate DER */
    size_t   old_len;
    uint8_t *new_der;    /* certificate the APK will actually be signed with */
    size_t   new_len;
    char     old_source[512];
    char     old_sha256[65];
    char     new_sha256[65];
    char    *sig_b64;    /* base64 of old DER, embedded in the runtime hook */
    sigkill_rep reps[SIGKILL_MAX_REPS];
    int      rep_count;
} sigkill_cert;

typedef struct {
    const char *src_dir;      /* decoded APK tree (required) */
    const char *orig_apk;     /* optional APK to take the original cert from */
    const char *orig_cert;    /* optional DER/PEM certificate file */
    const char *cert_path;    /* cert that will be used to sign the output */
    const char *extra_names;  /* extra comma-separated method name fragments */
    int do_scan;
    int do_stub;
    int do_const;
    int do_pkg;
    int do_native;            /* patch digests in lib/ and assets/ */
    int aggressive;           /* stub methods that only look like checks */
    int dry_run;              /* report, never modify the tree */
} sigkill_opts;

typedef struct {
    int files_scanned;
    int methods_scanned;
    int checks_found;
    int methods_stubbed;
    int consts_replaced;
    int const_files;
    int pkg_sites_patched;
    int pkg_files;
    int helper_injected;
    int native_files_scanned;
    int native_files_patched;
    int native_hits;
    int findings_truncated;
    char *details;            /* JSON array of findings */
    size_t details_len, details_cap;
} sigkill_report;

/* Certificate discovery: explicit cert file, explicit original APK, or the
 * META-INF signature block of a decoded tree (in that order). */
int sigkill_cert_load(sigkill_cert *c, const char *orig_cert,
                      const char *orig_apk, const char *tree_dir);
int sigkill_cert_load_file(sigkill_cert *c, const char *path);
int sigkill_cert_set_target(sigkill_cert *c, const uint8_t *der, size_t len);
int sigkill_compute_reps(sigkill_cert *c);
void sigkill_cert_free(sigkill_cert *c);

void sigkill_report_init(sigkill_report *r);
void sigkill_report_free(sigkill_report *r);

/* Patch a decoded tree in place. Returns 0 on success, -1 on a hard error
 * (no original certificate for pkg/const, unreadable tree). */
int sigkill_run(const sigkill_opts *o, sigkill_report *r);

/* Recursive remove used by the APK one-shot wrapper. */
void sigkill_rm_rf(const char *path);

#endif /* SIGKILL_H */
