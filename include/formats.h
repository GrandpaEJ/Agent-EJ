#ifndef FORMATS_H
#define FORMATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct zip_archive zip_archive;
typedef struct axml_ctx axml_ctx;
typedef struct dex_ctx dex_ctx;
#include "dex_find.h"
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
int zip_writer_add_apk(zip_writer *zw, const char *name, const void *data, size_t size);
int zip_writer_close(zip_writer *zw);
int zipalign_file(const char *in_path, const char *out_path, int alignment);
/* Raw copy of one entry (keeps its compression); stored entries aligned. */
int zip_copy_entry(zip_writer *zw, zip_archive *za, int index, int alignment);

// AXML
axml_ctx *axml_parse(const uint8_t *data, size_t size);
char *axml_get_xml(axml_ctx *ctx);
void axml_free(axml_ctx *ctx);
int axml_assemble(const char *src_xml, const char *out_axml);
void axml_set_arsc(axml_ctx *ctx, const struct arsc_ctx *arsc);
void axml_assemble_set_arsc(struct arsc_ctx *arsc);
/* Encodes XML text to binary AXML (malloc'd), naming @type/name refs through
 * arsc (may be NULL). NULL on a parse error or an unknown reference; see
 * axml_assemble_error() for which. */
uint8_t *axml_assemble_text(const char *xml, struct arsc_ctx *arsc, size_t *out_size);
const char *axml_assemble_error(void);

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
/* Translation view (MT's Translation mode) over the <string> resources.
 * Qualifiers are res directory suffixes: "" default, "-fr", "-zh-rCN". */
typedef struct arsc_tr arsc_tr;
arsc_tr *arsc_tr_open(const uint8_t *data, size_t size);
/* Every configuration the string type has, one per line (malloc'd). */
char *arsc_tr_locales(arsc_tr *t);
/* One line per plain string: "id\tkey\tdefault\t[=value in qual]", fields
 * escaped (\\ \n \t \r); the last field is empty when qual has no value. */
char *arsc_tr_dump(arsc_tr *t, const char *qual);
/* The same for another value type ("color", "dimen", "bool", "integer"):
 * colours, dimensions, booleans and integers are written as text ("#ff2196f3",
 * "16dp", "true", "12") and read back by arsc_tr_set. */
char *arsc_tr_locales_of(arsc_tr *t, const char *type);
char *arsc_tr_dump_of(arsc_tr *t, const char *type, const char *qual);
/* Sets id's text in qual, adding the entry or a new "-ll[-rCC]" language when
 * missing. 1 changed, 0 already that text, -1 not a translatable string. */
int arsc_tr_set(arsc_tr *t, uint32_t id, const char *qual, const char *text);
uint8_t *arsc_tr_encode(arsc_tr *t, size_t *out_size);
void arsc_tr_free(arsc_tr *t);
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
/* Number of class_defs in the dex. */
uint32_t dex_class_count(dex_ctx *ctx);
/* String pool: count and entry idx (decoded, "" when missing). */
uint32_t dex_string_count(dex_ctx *ctx);
const char *dex_string_at(dex_ctx *ctx, uint32_t idx);

/* Dex editor search, see dex_find.h. scope "com/a" limits to that package
 * (and sub-packages unless DEX_FIND_SHALLOW). Returns -1 on a bad query
 * (unparsable integer, invalid regex). */
int dex_find(dex_ctx *ctx, int kind, const char *query, int flags, const char *scope,
             dex_find_fn fn, void *user);

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
/* Dex editor save: rebuild one dex with classes replaced by edited smali.
 * Every class of `dex` is rendered and re-assembled, except the class_def
 * indices in skip[], whose edited text is texts[] (n_texts entries; a text
 * whose class is new to the dex adds it). Writes out_dex. Returns 0, -1 on a
 * smali error (reason in smali_last_error()), -2 when the result no longer
 * fits one dex. A skipped index with no text deletes that class. The
 * n_ren renames (smali_rename) apply to every class, rendered or edited. */
int smali_patch_dex(const uint8_t *dex, size_t len, const uint32_t *skip, int n_skip,
                    const char *const *texts, int n_texts, const char *const *ren_from,
                    const char *const *ren_to, int n_ren, const char *out_dex);
/* Class/package rename over smali text, applied in order. A class entry is a
 * descriptor ("La/b/C;": matches "La/b/C" followed by ';' or '<'); a package
 * entry ends in '/' ("La/b/": a prefix, so sub-packages move too). Returns a
 * malloc'd copy of text with every match rewritten, NULL when out of memory. */
char *smali_rename(const char *text, const char *const *from, const char *const *to, int n);
const char *smali_last_error(void);

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
/* Copies in_apk to out_apk (unsigned), with entries names[i] replaced by
 * files[i] (added when missing). Old signature files are dropped. */
int apk_replace_entries(const char *in_apk, const char *out_apk, const char *const *names,
                        const char *const *files, int n);
int apk_sign_pipeline(const char *in_zip, const char *out_apk, const char *key_path, const char *cert_path);
/* Signature schemes an APK carries: APK_SIG_V1 (a META-INF .SF file) and the IDs
 * found in its APK Signing Block. 0 when unsigned, -1 when unreadable. */
#define APK_SIG_V1    1
#define APK_SIG_V2    2
#define APK_SIG_V3    4
#define APK_SIG_V31   8
#define APK_SIG_STAMP 16
int apk_sig_schemes(const char *path);
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