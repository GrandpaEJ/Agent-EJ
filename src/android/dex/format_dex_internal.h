#ifndef FORMAT_DEX_INTERNAL_H
#define FORMAT_DEX_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int count;
    int cap;
    char **strings;
} dex_strpool;

typedef struct {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t params_off;
} dex_proto;

typedef struct {
    uint16_t class_idx;
    uint16_t type_idx;
    uint32_t name_idx;
} dex_field;

typedef struct {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
} dex_method_id;

typedef struct {
    uint32_t method_idx;
    uint32_t access_flags;
    uint32_t code_off;
} dex_method_enc;

typedef struct {
    uint32_t field_idx;
    uint32_t access_flags;
} dex_field_enc;

typedef struct {
    uint32_t class_idx;
    uint32_t access_flags;
    uint32_t superclass_idx;
    uint32_t interfaces_off;
    uint32_t source_file_idx;
    uint32_t annotations_off;
    uint32_t class_data_off;
    uint32_t static_values_off;
    int direct_count;
    int virtual_count;
    int static_count;
    int instance_count;
    dex_method_enc *direct;
    dex_method_enc *virtual;
    dex_field_enc *static_fields;
    dex_field_enc *instance_fields;
} dex_class;

typedef struct {
    uint32_t *string_ids;
    uint32_t string_count;
    int *type_ids;
    uint32_t type_count;
    dex_proto *protos;
    uint32_t proto_count;
    dex_field *fields;
    uint32_t field_count;
    dex_method_id *methods;
    uint32_t method_count;
    dex_class *classes;
    uint32_t class_count;
    const uint8_t *data;
    size_t size;
    dex_strpool sp;
    // header offsets cached
    uint32_t string_ids_off;
    uint32_t type_ids_off;
    uint32_t proto_ids_off;
    uint32_t field_ids_off;
    uint32_t method_ids_off;
    uint32_t class_defs_off;
} dex_ctx;

// ULEB128 reader used by multiple format_dex_* modules.
int dex_read_uleb128(const uint8_t *data, uint32_t *val);

// String builder (used by smali output modules)
typedef struct { char **b; size_t *l, *c; } smali_sb;
int smali_sa(smali_sb *s, const char *str);
int smali_sf(smali_sb *s, const char *fmt, ...);

// Smali utilities shared across modules
const char *smali_aflags(uint32_t f);
char *smali_tsm(const char *d);
int dex_format_value(const uint8_t **pp, const uint8_t *end, char *buf, size_t sz, dex_ctx *ctx);
int dex_skip_value(const uint8_t **pp, const uint8_t *end);
void dex_static_value(dex_ctx *ctx, uint32_t values_off, int idx, char *buf, size_t sz);
const char *smali_res(dex_ctx *ctx, int k, uint32_t i);
void smali_mproto(dex_ctx *ctx, uint32_t mi, char *b, size_t z);
const char *smali_reg_name(uint32_t r, uint32_t regs, uint32_t ins_size);
int smali_uleb(const uint8_t *data, uint32_t *val, const uint8_t **next);
int smali_sleb(const uint8_t *data, int32_t *val, const uint8_t **next);

// Smali annotation output
void smali_write_annotations(smali_sb *s, dex_ctx *ctx, dex_class *c,
                              int target_type, uint32_t target_idx);
int smali_write_method_annot(smali_sb *s, dex_ctx *ctx, dex_class *c,
                              uint32_t method_idx);

// Smali method disassembly
int smali_dm(smali_sb *s, dex_ctx *ctx, dex_method_enc *me);
int smali_dm_abstract(smali_sb *s, dex_ctx *ctx, dex_method_enc *me);

#endif
