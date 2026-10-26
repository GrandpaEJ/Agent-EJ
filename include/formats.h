#ifndef FORMATS_H
#define FORMATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct zip_archive zip_archive;
typedef struct axml_ctx axml_ctx;
typedef struct dex_ctx dex_ctx;
typedef struct arsc_ctx arsc_ctx;

// ZIP
zip_archive *zip_open(const char *path);
void zip_close(zip_archive *za);
int zip_get_num_entries(zip_archive *za);
const char *zip_get_entry_name(zip_archive *za, int index);
size_t zip_get_entry_size(zip_archive *za, int index);
int zip_entry_is_compressed(zip_archive *za, int index);
void *zip_extract_entry(zip_archive *za, int index, size_t *out_size);
int zip_extract_all(zip_archive *za, const char *out_dir);
typedef struct zip_writer zip_writer;
zip_writer *zip_writer_open(const char *path);
int zip_writer_add(zip_writer *zw, const char *name, const void *data, size_t size, int compress, int alignment);
int zip_writer_add_raw(zip_writer *zw, const char *name, const void *comp_data, size_t comp_size, size_t uncomp_size, uint32_t crc32, int method, int alignment);
int zip_writer_close(zip_writer *zw);
int zipalign_file(const char *in_path, const char *out_path, int alignment);

// AXML
axml_ctx *axml_parse(const uint8_t *data, size_t size);
char *axml_get_xml(axml_ctx *ctx);
void axml_free(axml_ctx *ctx);
int axml_assemble(const char *src_xml, const char *out_axml);
void axml_set_arsc(axml_ctx *ctx, const struct arsc_ctx *arsc);
void axml_assemble_set_arsc(struct arsc_ctx *arsc);

// ARSC
arsc_ctx *arsc_parse(const uint8_t *data, size_t size);
const char *arsc_get_string(arsc_ctx *ctx, uint32_t index);
int arsc_patch_string(arsc_ctx *ctx, uint32_t index, const char *new_str);
uint8_t *arsc_build(arsc_ctx *ctx, size_t *out_size);
void arsc_free(arsc_ctx *ctx);
int arsc_dump_toml(const char *arsc_path, const char *toml_path);
int arsc_compile_toml(const char *arsc_path, const char *toml_path, const char *out_arsc);
const char *arsc_lookup_id(arsc_ctx *ctx, uint32_t res_id);
const char *arsc_get_type_name(arsc_ctx *ctx, uint32_t pkg_id, uint8_t type_id);
uint32_t arsc_reverse_lookup(arsc_ctx *ctx, const char *type_name, const char *key_name);
const char *arsc_attr_enum(arsc_ctx *ctx, uint32_t attr_id, int32_t value);

// DEX (reader)
dex_ctx *dex_parse(const uint8_t *data, size_t size);
char *dex_dump(dex_ctx *ctx);
char *dex_to_smali_class(dex_ctx *ctx, uint32_t class_idx);
void dex_free(dex_ctx *ctx);

// Smali assembler
int smali_assemble(const char *src_dir, const char *out_dex);
/* Like smali_assemble, but reports how many DEX files were written (classes.dex, classes2.dex, ...). */
int smali_assemble_n(const char *src_dir, const char *out_dex, int *n_out);

// Smali flow graph
#include "smali_flow.h"
char* smali_flow_generate(smali_method_def_t *method, const char *method_name);

// APK
#include "crypto.h"
int apk_sign_v1(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len);
int apk_sign_v2_v3(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len);
char* generate_manifest(struct zip_archive *za, size_t *out_len);
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len, int do_v2, int do_v3);
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key, const uint8_t *custom_cert, uint32_t custom_cert_len, size_t *out_len);
char *apk_analyze(const char *path);
void apk_free(char *result);

// ADB
int adb_install(const char *apk_path);
int adb_uninstall(const char *package);
char *adb_shell(const char *command);
char *adb_devices(void);
int adb_push_file(const char *local_path, const char *remote_path);
int adb_pull_file(const char *remote_path, const char *local_path);

int apk_decode(const char *apk_path, const char *out_dir);
int apk_build(const char *src_dir, const char *out_apk, const char *key_path, const char *cert_path);
int arsc_decode_apk(const char *out_dir);

#endif