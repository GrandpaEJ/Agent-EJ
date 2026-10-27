#include "crypto.h"
#include "formats.h"
#include "format_apk_sign_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strncasecmp

// Phase 2: Generate MANIFEST.MF

/* Mirrors JDK SignatureFileVerifier.isSigningRelated(): files that live
 * directly inside META-INF/ (case-insensitive) and are MANIFEST.MF, end
 * with .SF/.DSA/.RSA/.EC (case-insensitive), or are SIG-<name>.<1-3 alnum>
 * legacy blocks.  Everything else under META-INF (services/, *.version,
 * build metadata) is regular content that must be signed like any file. */
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

char* generate_manifest(zip_archive *za, size_t *out_len) {
    int num_entries = zip_get_num_entries(za);
    size_t buf_cap = 256 + num_entries * 256;
    char *manifest = malloc(buf_cap);
    if (!manifest) return NULL;

    strcpy(manifest, "Manifest-Version: 1.0\r\nBuilt-By: Agent-X\r\n\r\n");
    size_t len = strlen(manifest);

    for (int i = 0; i < num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strlen(name) == 0) continue;
        // Skip directories and the signature files themselves; META-INF
        // content files are signed like every other entry (issue #30: they
        // used to be dropped from the output APK altogether).
        if (name[strlen(name) - 1] == '/') continue;
        if (apk_is_signature_file(name)) continue;

        size_t size = 0;
        void *data = zip_extract_entry(za, i, &size);
        if (!data) continue;

        uint8_t hash[20];
        sha1(data, size, hash);
        free(data);

        size_t b64_len = 0;
        char *b64 = base64_encode(hash, 20, &b64_len);

        int written = snprintf(manifest + len, buf_cap - len,
                               "Name: %s\r\nSHA1-Digest: %s\r\n\r\n", name, b64);
        if (written > 0) len += written;
        free(b64);
    }
    *out_len = len;
    return manifest;
}

// Phase 3: Generate CERT.SF
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len, int do_v2, int do_v3) {
    size_t buf_cap = manifest_len + 512;
    char *sf = malloc(buf_cap);
    if (!sf) return NULL;

    // Hash the entire manifest
    uint8_t man_hash[20];
    sha1(manifest, manifest_len, man_hash);
    size_t b64_len = 0;
    char *man_b64 = base64_encode(man_hash, 20, &b64_len);

    strcpy(sf, "Signature-Version: 1.0\r\nCreated-By: Agent-X\r\n");
    if (do_v2 && do_v3) {
        strcat(sf, "X-Android-APK-Signed: 2, 3\r\n");
    } else if (do_v3) {
        strcat(sf, "X-Android-APK-Signed: 3\r\n");
    } else if (do_v2) {
        strcat(sf, "X-Android-APK-Signed: 2\r\n");
    }
    size_t len = strlen(sf);

    len += snprintf(sf + len, buf_cap - len, "SHA1-Digest-Manifest: %s\r\n\r\n", man_b64);
    free(man_b64);

    // Parse the manifest line by line to hash individual sections
    // A section in MANIFEST.MF looks like:
    // Name: AndroidManifest.xml\r\nSHA1-Digest: ...\r\n\r\n
    // We need to hash exactly this string (including \r\n\r\n)

    const char *p = strstr(manifest, "Name: ");
    while (p) {
        const char *end = strstr(p, "\r\n\r\n");
        if (!end) break;
        end += 4; // Include the \r\n\r\n

        size_t section_len = end - p;
        uint8_t sec_hash[20];
        sha1(p, section_len, sec_hash);

        char *sec_b64 = base64_encode(sec_hash, 20, &b64_len);

        // Extract the name to write to SF
        const char *nl = strstr(p, "\r\n");
        size_t name_line_len = nl - p;
        char name_line[512] = {0};
        if (name_line_len < sizeof(name_line)) {
            strncpy(name_line, p, name_line_len);
        }

        len += snprintf(sf + len, buf_cap - len, "%s\r\nSHA1-Digest: %s\r\n\r\n", name_line, sec_b64);
        free(sec_b64);

        p = strstr(end, "Name: ");
    }

    *out_len = len;
    return sf;
}
