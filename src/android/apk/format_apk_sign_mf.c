#include "crypto.h"
#include "formats.h"
#include "format_apk_sign_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Phase 2: Generate MANIFEST.MF
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
        // Skip directories and existing META-INF files
        if (name[strlen(name) - 1] == '/') continue;
        if (strncmp(name, "META-INF/", 9) == 0) continue;

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
