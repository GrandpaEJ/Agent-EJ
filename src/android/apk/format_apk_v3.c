#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"
#include "formats.h"
#include "crypto.h"
#include "formats.h"

static void put_u32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }
static void put_u64(uint8_t **p, uint64_t v) { memcpy(*p, &v, 8); *p += 8; }
static void put_bytes(uint8_t **p, const uint8_t *data, uint32_t len) { memcpy(*p, data, len); *p += len; }
static void put_len_bytes(uint8_t **p, const uint8_t *data, uint32_t len) { put_u32(p, len); put_bytes(p, data, len); }

uint8_t* generate_v3_pair(rsa_key *key, uint8_t *final_apk_digest, uint64_t *out_len, const uint8_t *cert, uint32_t cert_len, const uint8_t *pubkey, uint32_t pubkey_len) {
    uint32_t digests_len = 4 + 4 + 4 + 32; // 44
    uint32_t certs_len = 4 + cert_len; // 4 + cert
    uint32_t attr_len = 0; // 0
    
    uint32_t signed_data_v3_len = (4 + digests_len) + (4 + certs_len) + 4 + 4 + (4 + attr_len);
    
    uint32_t sig_algo_id = 0x0103;
    uint32_t sig_len = (uint32_t)(key->bits / 8);
    uint32_t signatures_seq_len = 4 + 4 + 4 + sig_len;
    
    uint32_t pubkey_item_len = 4 + pubkey_len;
    
    uint32_t signer_v3_len = (4 + signed_data_v3_len) + 4 + 4 + (4 + signatures_seq_len) + pubkey_item_len;
    uint32_t signers_seq_v3_len = 4 + signer_v3_len;
    uint64_t v3_id = 0xf05368c0;
    uint64_t v3_pair_len = 8 + 4 + 4 + signers_seq_v3_len;
    
    *out_len = v3_pair_len;
    uint8_t *pair = malloc(v3_pair_len);
    uint8_t *p = pair;
    
    put_u64(&p, v3_pair_len - 8);
    put_u32(&p, v3_id);
    put_u32(&p, signers_seq_v3_len);
    put_u32(&p, signer_v3_len);
    
    put_u32(&p, signed_data_v3_len);
    
    // Build signed_data_v3 inside the pair buffer
    uint8_t *sd_v3 = p;
    put_u32(&p, digests_len);
    put_u32(&p, 4 + 4 + 32);
    put_u32(&p, sig_algo_id);
    put_len_bytes(&p, final_apk_digest, 32);
    
    put_u32(&p, certs_len);
    put_len_bytes(&p, cert, cert_len);
    
    put_u32(&p, 24); // minSdk (Android 7.0)
    put_u32(&p, 0x7fffffff); // maxSdk
    
    put_u32(&p, attr_len);
    
    // Sign the signed data
    uint8_t rsa_hash[32];
    sha256(sd_v3, signed_data_v3_len, rsa_hash);
    uint8_t signature_v3[256];
    if (sig_len > sizeof(signature_v3)) return NULL;
    rsa_sign_sha256(key, rsa_hash, signature_v3);
    
    // Continue building pair
    put_u32(&p, 24); // minSdk outside signed_data
    put_u32(&p, 0x7fffffff); // maxSdk outside signed_data
    
    put_u32(&p, signatures_seq_len);
    put_u32(&p, 4 + 4 + sig_len);
    put_u32(&p, sig_algo_id);
    put_len_bytes(&p, signature_v3, sig_len);
    put_len_bytes(&p, pubkey, pubkey_len);
    
    return pair;
}
