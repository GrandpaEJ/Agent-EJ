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
 * .SF/.DSA/.RSA/.EC files inside META-INF/).  `use_sha256` selects the
 * digest (SHA-256-Digest vs SHA1-Digest attributes).  Returns a
 * NUL-terminated buffer the caller must free(); *out_len is the byte
 * length without the NUL. */
char* generate_manifest(zip_archive *za, size_t *out_len, int use_sha256);

/* Build CERT.SF over the manifest produced by generate_manifest(): the hash
 * of the whole manifest plus one digest per manifest section, hashed over
 * the exact section bytes.  `use_sha256` must match the manifest it
 * covers.  Returns a NUL-terminated buffer the caller must free();
 * *out_len is the byte length without the NUL. */
char* generate_signature_file(const char *manifest, size_t manifest_len,
                              size_t *out_len, int do_v2, int do_v3,
                              int use_sha256);

/* 1 for the files the JAR spec calls signature-related: MANIFEST.MF, the
 * .SF/.DSA/.RSA/.EC blocks and SIG-* legacy blocks directly under META-INF/
 * (case-insensitive), mirrored from JDK SignatureFileVerifier.
 * isSigningRelated().  META-INF content files (services/, *.version, ...)
 * return 0: they are signed like any other entry. */
int apk_is_signature_file(const char *name);

/* Build the PKCS#7 block (CERT.RSA) over the signature file: the RSA
 * signature of the SF hash plus the signing certificate.  `custom_cert` is
 * used when present, the built-in certificate otherwise.  `use_sha256`
 * must match the digest declared in the signature file (SF hash, PKCS#7
 * digestAlgorithm OID and the signing padding all follow it).  Returns a
 * buffer the caller must free(); *out_len is the block length. */
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key,
                        const uint8_t *custom_cert, uint32_t custom_cert_len,
                        size_t *out_len, int use_sha256);

/* 1 when the v1 manifest must be signed with SHA-256, 0 for SHA-1.
 * Mirrors apksigner's digest choice, probed against build-tools 35.0.0:
 *   minSdkVersion >= 18 -> SHA-256  (jarsigner-verifiable, no warnings)
 *   minSdkVersion <  18 -> SHA-1    (SHA-256 fails apksigner verify on
 *                                    API 15-17; SHA-1 verifies at every
 *                                    level, see issue #30 probes)
 * Defaults: no AndroidManifest.xml at all means a JAR-like input that
 * Android would refuse anyway -> SHA-256; an unreadable manifest or a
 * missing uses-sdk (Android defaults minSdk to 1) -> SHA-1, the choice
 * that installs on every Android release. */
int apk_v1_use_sha256(zip_archive *za);

#endif
