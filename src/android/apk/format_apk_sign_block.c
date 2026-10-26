#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"
#include "formats.h"

// Forward declarations of our separated pair generators
uint8_t* generate_v2_pair(rsa_key *key, uint8_t *apk_digest, uint64_t *out_len, const uint8_t *cert, uint32_t cert_len, const uint8_t *pubkey, uint32_t pubkey_len);
uint8_t* generate_v3_pair(rsa_key *key, uint8_t *apk_digest, uint64_t *out_len, const uint8_t *cert, uint32_t cert_len, const uint8_t *pubkey, uint32_t pubkey_len);

static void put_u64(uint8_t **p, uint64_t v) { memcpy(*p, &v, 8); *p += 8; }
static void put_bytes(uint8_t **p, const uint8_t *data, uint32_t len) { memcpy(*p, data, len); *p += len; }

static void compute_chunked_hash(FILE *fp, size_t start, size_t end, sha256_ctx *main_ctx) {
    size_t len = end - start;
    if (len == 0) return;
    fseek(fp, start, SEEK_SET);
    
    size_t num_chunks = (len + 1048575) / 1048576;
    uint8_t *chunk_buf = malloc(1048576);
    
    for (size_t i = 0; i < num_chunks; i++) {
        size_t chunk_len = len - (i * 1048576);
        if (chunk_len > 1048576) chunk_len = 1048576;
        size_t r = fread(chunk_buf, 1, chunk_len, fp);
        if (r != chunk_len) break;
        
        uint8_t prefix[5];
        prefix[0] = 0xa5;
        uint32_t clen = (uint32_t)chunk_len;
        memcpy(prefix + 1, &clen, 4);
        
        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, prefix, 5);
        sha256_update(&ctx, chunk_buf, chunk_len);
        
        uint8_t chunk_hash[32];
        sha256_final(&ctx, chunk_hash);
        sha256_update(main_ctx, chunk_hash, 32);
    }
    free(chunk_buf);
}

#include "cert_der.h"
#include "pubkey_der.h"
#include "der.h"

// Generate APK Signing Block v2 and/or v3
int apk_sign_v2_v3(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len) {
    if (!do_v2 && !do_v3) return -1;
    FILE *in = fopen(in_apk, "rb");
    if (!in) return -1;
    
    fseek(in, 0, SEEK_END);
    long file_size = ftell(in);
    
    // Find EOCD
    long eocd_offset = -1;
    uint8_t eocd[22];
    for (long i = file_size - 22; i >= 0 && i >= file_size - 65536; i--) {
        fseek(in, i, SEEK_SET);
        size_t r = fread(eocd, 1, 22, in);
        if (r == 22 && eocd[0] == 0x50 && eocd[1] == 0x4b && eocd[2] == 0x05 && eocd[3] == 0x06) {
            eocd_offset = i;
            break;
        }
    }
    if (eocd_offset < 0) { fclose(in); return -1; }
    
    uint32_t cd_offset, cd_size;
    memcpy(&cd_size, eocd + 12, 4);
    memcpy(&cd_offset, eocd + 16, 4);
    
    size_t s1 = cd_offset;
    size_t s2 = cd_size;
    size_t s3 = file_size - eocd_offset;
    size_t num_chunks = ((s1 + 1048575) / 1048576) + ((s2 + 1048575) / 1048576) + ((s3 + 1048575) / 1048576);
    
    sha256_ctx main_ctx;
    sha256_init(&main_ctx);
    
    uint8_t top_prefix[5];
    top_prefix[0] = 0x5a;
    uint32_t nc = (uint32_t)num_chunks;
    memcpy(top_prefix + 1, &nc, 4);
    sha256_update(&main_ctx, top_prefix, 5);
    
    compute_chunked_hash(in, 0, cd_offset, &main_ctx);
    compute_chunked_hash(in, cd_offset, cd_offset + cd_size, &main_ctx);
    
    uint8_t eocd_prefix[5];
    eocd_prefix[0] = 0xa5;
    uint32_t elen = 22;
    memcpy(eocd_prefix + 1, &elen, 4);
    
    sha256_ctx eocd_ctx;
    sha256_init(&eocd_ctx);
    sha256_update(&eocd_ctx, eocd_prefix, 5);
    sha256_update(&eocd_ctx, eocd, 22);
    uint8_t eocd_chunk_hash[32];
    sha256_final(&eocd_ctx, eocd_chunk_hash);
    sha256_update(&main_ctx, eocd_chunk_hash, 32);
    
    uint8_t final_apk_digest[32];
    sha256_final(&main_ctx, final_apk_digest);
    
    // Pick the certificate/public key to embed, and refuse to sign unless it
    // matches the private key the signature is actually made with (issue #13).
    // With no custom certificate the built-in cert is used, but only when the
    // built-in public key matches the signing key.
    const uint8_t *use_cert = cert_der;
    uint32_t use_cert_len = cert_der_len;
    const uint8_t *use_pubkey = pubkey_der;
    uint32_t use_pubkey_len = pubkey_der_len;
    uint8_t *extracted_pubkey = NULL;

    if (custom_cert && custom_cert_len > 0) {
        use_cert = custom_cert;
        use_cert_len = custom_cert_len;
        size_t e_len = 0;
        extracted_pubkey = der_pubkey_from_cert(custom_cert, custom_cert_len, &e_len);
        if (!extracted_pubkey) {
            fprintf(stderr, "apk_sign_v2_v3: could not extract a public key from the supplied certificate\n");
            return -1;
        }
        use_pubkey = extracted_pubkey;
        use_pubkey_len = (uint32_t)e_len;
    }

    size_t key_spki_len = 0;
    uint8_t *key_spki = der_spki_from_rsa(key, &key_spki_len);
    if (!key_spki) {
        fprintf(stderr, "apk_sign_v2_v3: could not derive the public key from the RSA signing key\n");
        if (extracted_pubkey) free(extracted_pubkey);
        return -1;
    }
    if (key_spki_len != use_pubkey_len || memcmp(key_spki, use_pubkey, key_spki_len) != 0) {
        fprintf(stderr, "apk_sign_v2_v3: certificate does not match the RSA signing key; "
                        "supply a certificate matching the key via cert=/cert_path=\n");
        free(key_spki);
        free(extracted_pubkey);
        return APK_SIGN_ERR_CERT_MISMATCH;
    }
    free(key_spki);
    
    uint8_t *v2_pair = NULL, *v3_pair = NULL;
    uint64_t v2_len = 0, v3_len = 0;
    
    if (do_v2) v2_pair = generate_v2_pair(key, final_apk_digest, &v2_len, use_cert, use_cert_len, use_pubkey, use_pubkey_len);
    if (do_v3) v3_pair = generate_v3_pair(key, final_apk_digest, &v3_len, use_cert, use_cert_len, use_pubkey, use_pubkey_len);
    
    if (extracted_pubkey) free(extracted_pubkey);
    
    uint64_t block_len = 8 + 8 + 16;
    if (do_v2) block_len += v2_len;
    if (do_v3) block_len += v3_len;
    uint64_t block_size_field = block_len - 8;
    
    uint8_t *blk = malloc(block_len);
    uint8_t *p = blk;
    put_u64(&p, block_size_field);
    
    if (do_v2 && v2_pair) {
        put_bytes(&p, v2_pair, v2_len);
        free(v2_pair);
    }
    if (do_v3 && v3_pair) {
        put_bytes(&p, v3_pair, v3_len);
        free(v3_pair);
    }
    
    put_u64(&p, block_size_field);
    put_bytes(&p, (const uint8_t*)"APK Sig Block 42", 16);
    
    FILE *out = fopen(out_apk, "wb");
    fseek(in, 0, SEEK_SET);
    
    uint8_t *copy_buf = malloc(1048576);
    size_t rem = cd_offset;
    while (rem > 0) {
        size_t c = rem > 1048576 ? 1048576 : rem;
        size_t r = fread(copy_buf, 1, c, in);
        if (r != c) break;
        fwrite(copy_buf, 1, c, out);
        rem -= c;
    }
    
    fwrite(blk, 1, block_len, out);
    free(blk);
    
    rem = cd_size;
    while (rem > 0) {
        size_t c = rem > 1048576 ? 1048576 : rem;
        size_t r = fread(copy_buf, 1, c, in);
        if (r != c) break;
        fwrite(copy_buf, 1, c, out);
        rem -= c;
    }
    
    uint32_t new_cd_offset = cd_offset + block_len;
    memcpy(eocd + 16, &new_cd_offset, 4);
    fwrite(eocd, 1, 22, out);
    
    free(copy_buf);
    fclose(in);
    fclose(out);
    
    return 0;
}
