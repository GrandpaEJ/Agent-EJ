#ifndef FORMAT_APK_SIGN_INTERNAL_H
#define FORMAT_APK_SIGN_INTERNAL_H

/* Internal API of the v1 (JAR) APK signer: the manifest and signature file
 * builders and the PKCS#7 block.  Only the files of the v1 signer consume
 * these; the public entry points (apk_sign_v1/apk_sign_v2_v3) stay in
 * include/formats.h. */

#include <stddef.h>
#include <stdint.h>
#include "formats.h"
#include "crypto.h"

/* Build the MANIFEST.MF of the input APK: every non-directory entry is
 * listed except the signature files themselves (the MANIFEST.MF and the
 * .SF/.DSA/.RSA/.EC files inside META-INF/).  Returns a NUL-terminated
 * buffer the caller must free(); *out_len is the byte length without the
 * NUL. */
char* generate_manifest(zip_archive *za, size_t *out_len);

/* Build CERT.SF over the manifest produced by generate_manifest(): the hash
 * of the whole manifest plus one digest per manifest section, hashed over
 * the exact section bytes.  Returns a NUL-terminated buffer the caller must
 * free(); *out_len is the byte length without the NUL. */
char* generate_signature_file(const char *manifest, size_t manifest_len,
                              size_t *out_len, int do_v2, int do_v3);

/* Build the PKCS#7 block (CERT.RSA) over the signature file: the RSA
 * signature of the SF hash plus the signing certificate.  `custom_cert` is
 * used when present, the built-in certificate otherwise.  Returns a buffer
 * the caller must free(); *out_len is the block length. */
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key,
                        const uint8_t *custom_cert, uint32_t custom_cert_len,
                        size_t *out_len);

#endif
