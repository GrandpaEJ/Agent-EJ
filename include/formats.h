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
/* Compiles the decoded res values tree below `res_dir` back into `arsc_path`
 * and writes the result to `out_arsc`, so an edit made in one of the XML files
 * of a decoded values directory reaches the rebuilt APK.  Entries the tree
 * does not mention keep their original value.  The two counters are optional:
 * *out_changed counts entries that took a new value, *out_skipped counts
 * values that could not be represented and were left alone. */
int arsc_compile_xml(const char *arsc_path, const char *res_dir,
                     const char *out_arsc, int *out_changed, int *out_skipped);
const char *arsc_lookup_id(arsc_ctx *ctx, uint32_t res_id);
const char *arsc_get_type_name(arsc_ctx *ctx, uint32_t pkg_id, uint8_t type_id);
uint32_t arsc_reverse_lookup(arsc_ctx *ctx, const char *type_name, const char *key_name);
const char *arsc_attr_enum(arsc_ctx *ctx, uint32_t attr_id, int32_t value);
// Inverse of arsc_attr_enum(): 1 and *out if name is one of attr_id's values.
int arsc_attr_enum_value(arsc_ctx *ctx, uint32_t attr_id, const char *name, int32_t *out);

// DEX (reader)
dex_ctx *dex_parse(const uint8_t *data, size_t size);
char *dex_dump(dex_ctx *ctx);
char *dex_to_smali_class(dex_ctx *ctx, uint32_t class_idx);
void dex_free(dex_ctx *ctx);

/* Deobfuscation — repair pass. Each takes a (possibly tampered) format image
 * and returns a malloc'd, cleaned image of *out_size that standard tools can
 * parse, or NULL when the image is unrepairable. `changes` (optional, may be
 * NULL) receives a short "; "-separated summary of what was fixed. */
uint8_t *dex_repair(const uint8_t *in, size_t size, size_t *out_size,
                    char *changes, size_t changes_cap);
uint8_t *axml_repair(const uint8_t *in, size_t size, size_t *out_size,
                     char *changes, size_t changes_cap);
uint8_t *arsc_repair(const uint8_t *in, size_t size, size_t *out_size,
                     char *changes, size_t changes_cap);

/* Deobfuscation — name-restoring passes over a decoded smali tree (for
 * analysis/readability; renamed output is not meant to be rebuilt and run). */
typedef struct deobf_table deobf_table;
deobf_table *deobf_table_new(void);
void deobf_table_free(deobf_table *t);
void deobf_add_class(deobf_table *t, const char *obf_body, const char *new_body);
void deobf_add_member(deobf_table *t, const char *obf_class_body,
                      const char *obf_name, const char *new_name);
int deobf_rewrite_tree(const char *root, deobf_table *t, char *report, size_t cap);
/* Map mode: apply an R8/ProGuard mapping.txt. Returns files rewritten, or -1. */
int deobf_map_apply(const char *smali_root, const char *mapping_path,
                    char *report, size_t cap);
/* Heuristic mode: stable readable names for obfuscated classes/members. */
int deobf_heuristic_apply(const char *smali_root, char *report, size_t cap);

// Smali assembler
int smali_assemble(const char *src_dir, const char *out_dex);
/* Like smali_assemble, but reports how many DEX files were written (classes.dex, classes2.dex, ...). */
int smali_assemble_n(const char *src_dir, const char *out_dex, int *n_out);

// Smali flow graph
#include "smali_flow.h"
char* smali_flow_generate(smali_method_def_t *method, const char *method_name);

// APK
#include "crypto.h"
/* apk_sign_v1/apk_sign_v2_v3 error: embedded certificate does not match the signing key */
#define APK_SIGN_ERR_CERT_MISMATCH (-2)
int apk_sign_v1(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len);
int apk_sign_v2_v3(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len);
/* Consumes the written ZIP payload `in_zip` (removing it) and produces a
 * signed, aligned APK at `out_apk`: v1 -> zipalign -> v2/v3, or align-only
 * when key_path is NULL. Returns 0, or APK_SIGN_ERR_CERT_MISMATCH / -1. */
int apk_sign_pipeline(const char *in_zip, const char *out_apk, const char *key_path, const char *cert_path);
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
/* Counterpart of arsc_decode_apk(): regenerates resources.arsc from the res
 * values tree below src_dir so that an edit made in one of those files reaches
 * the rebuilt APK.  Returns 0 when the table was replaced or there was nothing
 * to recompile, -1 when the original table was kept. */
int apk_recompile_resources(const char *src_dir);
/* Native anti-split: merges a split APK set (a directory of APKs or an
 * .xapk/.apks/.apkm bundle) into one standalone, unsigned, aligned APK.
 * The caller signs the result. Returns 0 on success. */
int apk_merge_assemble(const char *input, const char *out_apk);
/* Native anti-split, end to end: assembles the merged APK and signs it
 * (default key when key_path is NULL is the caller's concern). Returns 0,
 * or APK_SIGN_ERR_CERT_MISMATCH / -1. */
int apk_merge(const char *input, const char *out_apk, const char *key_path, const char *cert_path);
/* Clears the split requirements (requiredSplitTypes / isSplitRequired) in a
 * base binary AndroidManifest so the merged APK installs on its own. Returns a
 * malloc'd copy of `size` bytes, or NULL. */
uint8_t *apk_fix_split_manifest(const uint8_t *axml, size_t size, size_t *out_size);
/* Deobfuscation (repair pass), APK-wide: copies in_apk to out_apk, replacing
 * each tampered DEX / binary XML / resource table with its repaired form. The
 * output is unsigned and aligned. `report` (optional) receives a newline-
 * separated per-entry summary. Returns the count of entries changed, or -1. */
int apk_deobf_repair(const char *in_apk, const char *out_apk,
                     char *report, size_t report_cap);

#endif