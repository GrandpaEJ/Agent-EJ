#ifndef DER_H
#define DER_H

#include <stddef.h>
#include <stdint.h>
#include "crypto.h"

// Build a DER SubjectPublicKeyInfo (rsaEncryption) from an RSA key.
// Returns a malloc'd buffer (caller frees) or NULL on invalid input.
uint8_t *der_spki_from_rsa(const rsa_key *key, size_t *out_len);

// Extract the DER SubjectPublicKeyInfo from an X.509 certificate (DER).
// Returns a malloc'd buffer (caller frees) or NULL if not found.
uint8_t *der_pubkey_from_cert(const uint8_t *cert, size_t cert_len, size_t *out_len);

// 1 if the certificate's public key matches the RSA signing key, else 0.
int der_cert_matches_key(const rsa_key *key, const uint8_t *cert, size_t cert_len);

// End of der

#endif
