#include "crypto.h"
#include "formats.h"
#include "der.h"
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


static int extract_issuer_and_serial(const uint8_t *cert, size_t len, 
                                     const uint8_t **out_issuer, size_t *out_issuer_len, 
                                     const uint8_t **out_serial, size_t *out_serial_len) {
    size_t i = 0;
    if (i >= len || cert[i++] != 0x30) return -1;
    if (cert[i] & 0x80) i += (cert[i] & 0x7F) + 1; else i++;

    if (i >= len || cert[i++] != 0x30) return -1;
    if (cert[i] & 0x80) i += (cert[i] & 0x7F) + 1; else i++;

    if (i < len && cert[i] == 0xA0) { // version
        int len_bytes = 0;
        if (cert[i+1] & 0x80) {
            len_bytes = cert[i+1] & 0x7F;
            size_t v_len = 0;
            for(int b=0; b<len_bytes; b++) v_len = (v_len << 8) | cert[i+2+b];
            i += 2 + len_bytes + v_len;
        } else {
            i += 2 + cert[i+1];
        }
    }

    if (i >= len || cert[i] != 0x02) return -1; // serialNumber
    size_t serial_len = cert[i+1];
    int len_bytes = 0;
    if (serial_len & 0x80) {
        len_bytes = serial_len & 0x7F;
        serial_len = 0;
        for(int b=0; b<len_bytes; b++) serial_len = (serial_len << 8) | cert[i+2+b];
    }
    *out_serial = &cert[i];
    *out_serial_len = 2 + len_bytes + serial_len;
    i += 2 + len_bytes + serial_len;

    if (i >= len || cert[i] != 0x30) return -1; // signature algorithm
    size_t sig_alg_len = cert[i+1];
    len_bytes = 0;
    if (sig_alg_len & 0x80) {
        len_bytes = sig_alg_len & 0x7F;
        sig_alg_len = 0;
        for(int b=0; b<len_bytes; b++) sig_alg_len = (sig_alg_len << 8) | cert[i+2+b];
    }
    i += 2 + len_bytes + sig_alg_len;

    if (i >= len || cert[i] != 0x30) return -1; // issuer
    size_t issuer_len = cert[i+1];
    len_bytes = 0;
    if (issuer_len & 0x80) {
        len_bytes = issuer_len & 0x7F;
        issuer_len = 0;
        for(int b=0; b<len_bytes; b++) issuer_len = (issuer_len << 8) | cert[i+2+b];
    }
    *out_issuer = &cert[i];
    *out_issuer_len = 2 + len_bytes + issuer_len;
    return 0;
}

static void put_byte(uint8_t **p, uint8_t b) {
    *(*p)-- = b;
}

static void put_bytes(uint8_t **p, const uint8_t *b, size_t len) {
    *p -= len;
    memcpy(*p + 1, b, len);
}

static void put_len(uint8_t **p, size_t len) {
    if (len < 128) {
        put_byte(p, len);
    } else if (len < 256) {
        put_byte(p, len);
        put_byte(p, 0x81);
    } else if (len < 65536) {
        put_byte(p, len & 0xFF);
        put_byte(p, len >> 8);
        put_byte(p, 0x82);
    } else {
        put_byte(p, len & 0xFF);
        put_byte(p, (len >> 8) & 0xFF);
        put_byte(p, len >> 16);
        put_byte(p, 0x83);
    }
}

static void put_seq(uint8_t **p, size_t len) { put_len(p, len); put_byte(p, 0x30); }
static void put_set(uint8_t **p, size_t len) { put_len(p, len); put_byte(p, 0x31); }
static void put_cont(uint8_t **p, uint8_t tag, size_t len) { put_len(p, len); put_byte(p, tag); }

#include "cert_der.h"

// Phase 4: Generate CERT.RSA (PKCS#7 block)
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key, const uint8_t *custom_cert, uint32_t custom_cert_len, size_t *out_len) {
    uint8_t sf_hash[20];
    sha1(sf_data, sf_len, sf_hash);

    uint8_t signature[256];
    if (rsa_sign(key, sf_hash, signature) != 0) {
        return NULL;
    }

    const uint8_t *cert = custom_cert ? custom_cert : cert_der;
    uint32_t cert_len = custom_cert ? custom_cert_len : cert_der_len;

    const uint8_t *issuer, *serial;
    size_t issuer_len, serial_len;
    if (extract_issuer_and_serial(cert, cert_len, &issuer, &issuer_len, &serial, &serial_len) != 0) {
        return NULL;
    }

    uint8_t *buf = malloc(4096 + cert_len);
    if (!buf) return NULL;
    uint8_t *p = buf + 4096 + cert_len - 1;
    uint8_t *end = p;

    // signature: OCTET STRING
    put_bytes(&p, signature, 256); put_len(&p, 256); put_byte(&p, 0x04);
    
    // signatureAlgorithm: rsaEncryption
    uint8_t rsa_alg[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    put_bytes(&p, rsa_alg, sizeof(rsa_alg)); put_seq(&p, sizeof(rsa_alg));
    
    // digestAlgorithm: sha1
    uint8_t sha1_alg[] = {0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00};
    put_bytes(&p, sha1_alg, sizeof(sha1_alg)); put_seq(&p, sizeof(sha1_alg));

    // issuerAndSerialNumber
    uint8_t *is_end = p;
    put_bytes(&p, serial, serial_len);
    put_bytes(&p, issuer, issuer_len);
    put_seq(&p, is_end - p);

    // version 1
    put_byte(&p, 0x01); put_len(&p, 1); put_byte(&p, 0x02);

    // signerInfo SEQUENCE
    put_seq(&p, end - p);

    // SET of signerInfos
    put_set(&p, end - p);

    // certificates cont [0]
    uint8_t *cert_end = p;
    put_bytes(&p, cert, cert_len);
    put_cont(&p, 0xA0, cert_end - p);

    // encapContentInfo pkcs7-data
    uint8_t data_oid[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01};
    put_bytes(&p, data_oid, sizeof(data_oid)); put_seq(&p, sizeof(data_oid));

    // --- digestAlgorithms SET ---
    uint8_t *da_end = p;
    put_bytes(&p, sha1_alg, sizeof(sha1_alg)); put_seq(&p, sizeof(sha1_alg));
    put_set(&p, da_end - p);

    // version 1
    put_byte(&p, 0x01); put_len(&p, 1); put_byte(&p, 0x02);

    // SignedData SEQUENCE
    put_seq(&p, end - p);

    // [0] EXPLICIT
    put_cont(&p, 0xA0, end - p);

    // ContentInfo SEQUENCE
    uint8_t signed_oid[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02};
    put_bytes(&p, signed_oid, sizeof(signed_oid));
    
    put_seq(&p, end - p);

    *out_len = end - p;
    char *result = malloc(*out_len);
    memcpy(result, p + 1, *out_len);
    free(buf);
    return result;
}

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

    // Copy original files (except META-INF)
    int num_entries = zip_get_num_entries(za);
    for (int i = 0; i < num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strlen(name) == 0) continue;
        if (name[strlen(name) - 1] == '/') continue; // Skip directories
        if (strncmp(name, "META-INF/", 9) == 0) continue; // Skip old signatures

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
