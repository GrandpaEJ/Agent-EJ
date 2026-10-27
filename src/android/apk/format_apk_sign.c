#include "crypto.h"
#include "formats.h"
#include "der.h"
#include "format_apk_sign_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cert_der.h"

// Phase 5: ZIP Injection (Full APK Signing process)
int apk_sign_v1(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len) {

    // Refuse to embed a certificate that does not match the signing key
    // (issue #13): the resulting APK would fail v1 verification.
    const uint8_t *use_cert = (custom_cert && custom_cert_len > 0) ? custom_cert : cert_der;
    uint32_t use_cert_len = (custom_cert && custom_cert_len > 0) ? custom_cert_len : cert_der_len;
    if (!der_cert_matches_key(key, use_cert, use_cert_len)) {
        fprintf(stderr, "apk_sign_v1: certificate does not match the RSA signing key; "
                        "supply a certificate matching the key via cert=/cert_path=\n");
        return APK_SIGN_ERR_CERT_MISMATCH;
    }

    zip_archive *za = zip_open(in_apk);
    if (!za) {
        printf("Failed to open input APK: %s\n", in_apk);
        return -1;
    }

    zip_writer *zw = zip_writer_open(out_apk);
    if (!zw) {
        printf("Failed to open output APK: %s\n", out_apk);
        zip_close(za);
        return -1;
    }

    // Generate Signature Files
    size_t mf_len = 0;
    char *mf_data = generate_manifest(za, &mf_len);
    if (!mf_data) {
        printf("Failed to generate MANIFEST.MF\n");
        zip_close(za); zip_writer_close(zw); return -1;
    }

    size_t sf_len = 0;
    char *sf_data = generate_signature_file(mf_data, mf_len, &sf_len, do_v2, do_v3);
    if (!sf_data) {
        printf("Failed to generate CERT.SF\n");
        free(mf_data); zip_close(za); zip_writer_close(zw); return -1;
    }

    size_t rsa_len = 0;
    char *rsa_data = generate_cert_rsa(sf_data, sf_len, key, custom_cert, custom_cert_len, &rsa_len);
    if (!rsa_data) {
        printf("Failed to generate CERT.RSA\n");
        free(mf_data); free(sf_data); zip_close(za); zip_writer_close(zw); return -1;
    }

    // Write META-INF files first
    zip_writer_add(zw, "META-INF/MANIFEST.MF", mf_data, mf_len, 1, 0);
    zip_writer_add(zw, "META-INF/CERT.SF", sf_data, sf_len, 1, 0);
    zip_writer_add(zw, "META-INF/CERT.RSA", rsa_data, rsa_len, 0, 0);

    // Copy original files: the signature files themselves are replaced by
    // the ones just written, but META-INF content (services/, *.version,
    // ...) is regular payload and must survive (issue #30).
    int num_entries = zip_get_num_entries(za);
    for (int i = 0; i < num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strlen(name) == 0) continue;
        if (name[strlen(name) - 1] == '/') continue; // Skip directories
        if (apk_is_signature_file(name)) continue;   // Skip old signatures

        size_t size = 0;
        void *data = zip_extract_entry(za, i, &size);
        if (data) {
            int compress = zip_entry_is_compressed(za, i);
            zip_writer_add(zw, name, data, size, compress, 0);
            free(data);
        }
    }

    // Cleanup
    free(mf_data);
    free(sf_data);
    free(rsa_data);
    zip_writer_close(zw);
    zip_close(za);

    return 0;
}
