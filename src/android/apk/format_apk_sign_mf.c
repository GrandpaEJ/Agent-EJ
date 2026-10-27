#include "crypto.h"
#include "formats.h"
#include "format_apk_sign_internal.h"
#include "format_arsc_buf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strncasecmp

/* JAR manifest lines may not exceed 72 content bytes (jar spec); longer
 * values are wrapped onto continuation lines that begin with a single
 * space.  jarsigner writes exactly this, apksigner writes the same shape
 * at 70 bytes, and both verifiers accept both — probes in issue #30.
 * Unwrapped lines are rejected outright: a 619-byte Name line made
 * jarsigner throw "manifest line too long" and apksigner report
 * "No JAR signatures". */
#define V1_MANIFEST_LINE 72

/* Append "Name: <name>\r\n" wrapped at V1_MANIFEST_LINE content bytes. */
static int append_name_section(arsc_buf *b, const char *name) {
    size_t n = strlen(name);
    if (arsc_buf_append(b, "Name: ", 6) != 0) return -1;
    size_t first = n < (V1_MANIFEST_LINE - 6) ? n : (V1_MANIFEST_LINE - 6);
    if (arsc_buf_append(b, name, first) != 0) return -1;
    size_t pos = first;
    while (pos < n) {
        size_t chunk = (n - pos) < (V1_MANIFEST_LINE - 1) ? (n - pos)
                                                          : (V1_MANIFEST_LINE - 1);
        if (arsc_buf_append(b, "\r\n ", 3) != 0) return -1;
        if (arsc_buf_append(b, name + pos, chunk) != 0) return -1;
        pos += chunk;
    }
    return arsc_buf_append(b, "\r\n", 2);
}

/* Append "<attr>: <b64>\r\n\r\n" — one digest line plus the blank line
 * that ends a manifest or SF section. */
static int append_digest_line(arsc_buf *b, const char *attr,
                              const char *b64, size_t b64_len) {
    if (arsc_buf_append(b, attr, strlen(attr)) != 0) return -1;
    if (arsc_buf_append(b, ": ", 2) != 0) return -1;
    if (arsc_buf_append(b, b64, b64_len) != 0) return -1;
    return arsc_buf_append(b, "\r\n\r\n", 4);
}

/* 1 for the files the JAR spec calls signature-related, mirrored from JDK
 * SignatureFileVerifier.isSigningRelated(): MANIFEST.MF, and the .SF, .DSA,
 * .RSA or .EC blocks plus SIG-<name>.<ext> legacy blocks directly inside
 * META-INF/ (case-insensitive).  META-INF content files (services/,
 * *.version, build metadata) return 0: they are signed like any file. */
int apk_is_signature_file(const char *name) {
    if (strncasecmp(name, "META-INF/", 9) != 0) return 0;
    if (strchr(name + 9, '/')) return 0; // only directly inside META-INF/

    const char *base = name + 9;
    size_t n = strlen(base);
    if (n == 11 && strncasecmp(base, "MANIFEST.MF", 11) == 0) return 1;
    if (n >= 3) {
        if (strncasecmp(base + n - 3, ".SF", 3) == 0) return 1;
        if (strncasecmp(base + n - 3, ".EC", 3) == 0) return 1;
    }
    if (n >= 4) {
        if (strncasecmp(base + n - 4, ".DSA", 4) == 0) return 1;
        if (strncasecmp(base + n - 4, ".RSA", 4) == 0) return 1;
    }
    if (n > 4 && strncasecmp(base, "SIG-", 4) == 0) {
        const char *dot = strrchr(base, '.');
        if (dot && dot > base + 4) {
            size_t el = strlen(dot + 1);
            if (el >= 1 && el <= 3) {
                size_t i;
                for (i = 0; i < el; i++) {
                    char c = dot[1 + i];
                    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z')))
                        return 0;
                }
                return 1;
            }
        }
    }
    return 0;
}

// Phase 2: Generate MANIFEST.MF
char* generate_manifest(zip_archive *za, size_t *out_len, int use_sha256) {
    const char *attr = use_sha256 ? "SHA-256-Digest" : "SHA1-Digest";
    size_t hash_len = use_sha256 ? 32 : 20;
    static const char header[] = "Manifest-Version: 1.0\r\nBuilt-By: Agent-EJ\r\n\r\n";

    arsc_buf b;
    arsc_buf_init(&b);
    if (arsc_buf_append(&b, header, sizeof(header) - 1) != 0) goto fail;

    int num_entries = zip_get_num_entries(za);
    for (int i = 0; i < num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || name[0] == '\0') continue;
        // Skip directories and the signature files themselves; META-INF
        // content files are signed like every other entry (issue #30: they
        // used to be dropped from the output APK altogether).
        if (name[strlen(name) - 1] == '/') continue;
        if (apk_is_signature_file(name)) continue;

        size_t size = 0;
        void *data = zip_extract_entry(za, i, &size);
        if (!data) continue;

        uint8_t hash[32];
        if (use_sha256) sha256(data, size, hash);
        else sha1(data, size, hash);
        free(data);

        size_t b64_len = 0;
        char *b64 = base64_encode(hash, hash_len, &b64_len);
        if (!b64) goto fail;

        int rc = append_name_section(&b, name);
        if (rc == 0) rc = append_digest_line(&b, attr, b64, b64_len);
        free(b64);
        if (rc != 0) goto fail;
    }

    size_t len = b.len;      // content length, without the NUL below
    if (arsc_buf_u8(&b, 0) != 0) goto fail; // NUL for the SF parser
    *out_len = len;
    return (char *)b.data;

fail:
    arsc_buf_free(&b);
    return NULL;
}

// Phase 3: Generate CERT.SF
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len, int do_v2, int do_v3, int use_sha256) {
    const char *attr = use_sha256 ? "SHA-256-Digest" : "SHA1-Digest";
    const char *main_attr = use_sha256 ? "SHA-256-Digest-Manifest" : "SHA1-Digest-Manifest";
    size_t hash_len = use_sha256 ? 32 : 20;

    static const char sf_header[] = "Signature-Version: 1.0\r\nCreated-By: Agent-EJ\r\n";
    static const char x_both[]    = "X-Android-APK-Signed: 2, 3\r\n";
    static const char x_v3[]      = "X-Android-APK-Signed: 3\r\n";
    static const char x_v2[]      = "X-Android-APK-Signed: 2\r\n";

    arsc_buf b;
    arsc_buf_init(&b);
    if (arsc_buf_append(&b, sf_header, sizeof(sf_header) - 1) != 0) goto fail;
    if (do_v2 && do_v3) {
        if (arsc_buf_append(&b, x_both, sizeof(x_both) - 1) != 0) goto fail;
    } else if (do_v3) {
        if (arsc_buf_append(&b, x_v3, sizeof(x_v3) - 1) != 0) goto fail;
    } else if (do_v2) {
        if (arsc_buf_append(&b, x_v2, sizeof(x_v2) - 1) != 0) goto fail;
    }

    // Hash the entire manifest over its raw stored bytes — the verifiers
    // hash the stored bytes too (wrapping included, probe-verified).
    uint8_t man_hash[32];
    if (use_sha256) sha256(manifest, manifest_len, man_hash);
    else sha1(manifest, manifest_len, man_hash);
    size_t b64_len = 0;
    char *man_b64 = base64_encode(man_hash, hash_len, &b64_len);
    if (!man_b64) goto fail;

    int rc = arsc_buf_append(&b, main_attr, strlen(main_attr));
    if (rc == 0) rc = arsc_buf_append(&b, ": ", 2);
    if (rc == 0) rc = arsc_buf_append(&b, man_b64, b64_len);
    free(man_b64);
    if (rc == 0) rc = arsc_buf_append(&b, "\r\n\r\n", 4);
    if (rc != 0) goto fail;

    // One SF section per manifest section.  The section digest covers the
    // manifest's raw section bytes (wrapping included — proven against the
    // reference jars).  The section name is copied byte-for-byte from the
    // manifest so a wrapped Name line stays wrapped: the Manifest parser
    // unwraps it to the same logical name on both sides, while an
    // unwrapped 600-byte line in the SF would be rejected as too long.
    const char *p = strstr(manifest, "Name: ");
    while (p) {
        const char *end = strstr(p, "\r\n\r\n");
        if (!end) break;
        end += 4; // include the \r\n\r\n of the section

        // Locate the digest line: walk past the name line and any
        // continuation lines (they start with a space).
        const char *dl = strstr(p, "\r\n");
        if (!dl || dl >= end) break;
        dl += 2;
        while (dl < end && dl[0] == ' ') {
            dl = strstr(dl, "\r\n");
            if (!dl || dl >= end) break;
            dl += 2;
        }
        if (!dl || dl >= end) break;

        // name portion = [p, dl): the Name line(s) including their CRLFs
        if (arsc_buf_append(&b, p, (size_t)(dl - p)) != 0) goto fail;

        // digest over the exact manifest section bytes
        uint8_t sec_hash[32];
        if (use_sha256) sha256(p, (size_t)(end - p), sec_hash);
        else sha1(p, (size_t)(end - p), sec_hash);
        char *sec_b64 = base64_encode(sec_hash, hash_len, &b64_len);
        if (!sec_b64) goto fail;
        rc = append_digest_line(&b, attr, sec_b64, b64_len);
        free(sec_b64);
        if (rc != 0) goto fail;

        p = strstr(end, "Name: ");
    }

    size_t len = b.len;
    if (arsc_buf_u8(&b, 0) != 0) goto fail;
    *out_len = len;
    return (char *)b.data;

fail:
    arsc_buf_free(&b);
    return NULL;
}
