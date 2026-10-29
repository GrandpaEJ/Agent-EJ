/* The shared signing tail used after an APK's ZIP payload has been written:
 * v1 (JAR) sign, zipalign to 4 bytes, then v2/v3 sign, falling back to the
 * v1-only aligned APK if v2/v3 fails. With no key the payload is only aligned.
 * apk_build and apk_merge both end here so the chain lives in one place. */
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (n > 0) ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

int apk_sign_pipeline(const char *in_zip, const char *out_apk,
                      const char *key_path, const char *cert_path) {
    if (!key_path) {
        int rc = zipalign_file(in_zip, out_apk, 4);
        remove(in_zip);
        return rc;
    }

    rsa_key key;
    if (rsa_load_key(key_path, &key) != 0) { remove(in_zip); return -1; }

    uint8_t *custom_cert = NULL;
    size_t custom_cert_len = 0;
    if (cert_path) custom_cert = read_file(cert_path, &custom_cert_len);

    char v1[1024], aligned[1024];
    snprintf(v1, sizeof(v1), "%s.v1.zip", out_apk);
    snprintf(aligned, sizeof(aligned), "%s.aligned.zip", out_apk);

    int rc = apk_sign_v1(in_zip, v1, &key, 1, 1, custom_cert, custom_cert_len);
    remove(in_zip);
    if (rc != 0) { free(custom_cert); return rc; }

    if (zipalign_file(v1, aligned, 4) != 0) { remove(v1); free(custom_cert); return -1; }
    remove(v1);

    if (apk_sign_v2_v3(aligned, out_apk, &key, 1, 1, custom_cert, custom_cert_len) != 0)
        rename(aligned, out_apk);   /* keep the v1-only aligned APK */
    else
        remove(aligned);

    free(custom_cert);
    return 0;
}
