#ifndef SMALI_TYPES_H
#define SMALI_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define MAX_LABELS 128
#define MAX_INSNS 2048
#define MAX_FIELDS 256
#define MAX_METHODS 256
#define MAX_ANNOT_ELEMS 32
#define MAX_ANNOTS 32

#define VALUE_TYPE_BYTE    0x00
#define VALUE_TYPE_SHORT   0x02
#define VALUE_TYPE_CHAR    0x03
#define VALUE_TYPE_INT     0x04
#define VALUE_TYPE_LONG    0x06
#define VALUE_TYPE_FLOAT   0x10
#define VALUE_TYPE_DOUBLE  0x11
#define VALUE_TYPE_METHOD_TYPE 0x15
#define VALUE_TYPE_METHOD_HANDLE 0x16
#define VALUE_TYPE_STRING  0x17
#define VALUE_TYPE_TYPE    0x18
#define VALUE_TYPE_FIELD   0x19
#define VALUE_TYPE_METHOD  0x1a
#define VALUE_TYPE_ENUM    0x1b
#define VALUE_TYPE_ARRAY   0x1c
#define VALUE_TYPE_ANNOT   0x1d
#define VALUE_TYPE_NULL    0x1e
#define VALUE_TYPE_BOOL    0x1f

typedef struct {
    char *name;
    uint32_t offset;
} smali_label_t;

typedef struct {
    uint8_t op;
    uint8_t fmt;
    uint8_t kind;
    uint32_t vA;
    uint32_t vB;
    uint32_t vC;
    uint32_t regs[5];
    char *ref_str;
    int64_t lit;
    char *label_target;
    int line_number;

    char **payload_targets;
    uint32_t payload_targets_count;
    int32_t *payload_keys;
    uint8_t *payload_data;
    uint32_t payload_data_len;
    uint16_t payload_element_width;
} smali_insn_t;

typedef struct smali_annot_val smali_annot_val_t;

typedef struct smali_annotation_elem {
    char *name;
    char *type;
    int value_type;
    int64_t value_int;
    double value_double;
    char *value_str;
    smali_annot_val_t *arr_head;
    smali_annot_val_t *arr_tail;
    uint32_t array_count;
    char *annot_type;
    struct smali_annotation_elem *sub_elems;
    int annot_elem_count;
} smali_annotation_elem_t;

struct smali_annot_val {
    smali_annot_val_t *next;
    smali_annotation_elem_t elem;
};

typedef struct {
    int visibility;
    char *type;
    smali_annotation_elem_t elems[MAX_ANNOT_ELEMS];
    uint32_t elem_count;
} smali_annotation_t;

typedef struct {
    uint32_t access_flags;
    char *name;
    char *type;
    int has_init_value;
    int value_type;
    int64_t value_int;
    double value_double;
    char *value_str;
    uint32_t *array_vals;
    uint32_t array_count;
    smali_annotation_t *annots;
    uint32_t annot_count;
    uint32_t annot_cap;
} smali_field_def_t;

typedef struct {
    char *type;
    char *start_label;
    char *end_label;
    char *handler_label;
} smali_catch_t;

typedef struct {
    uint32_t access_flags;
    char *name;
    char *signature;
    uint32_t registers_count;
    uint32_t locals_count;
    uint32_t ins_count;
    smali_insn_t *insns;
    uint32_t insns_count;
    uint32_t insns_cap;
    smali_label_t *labels;
    uint32_t labels_count;
    uint32_t labels_cap;
    smali_catch_t *catches;
    uint32_t catches_count;
    uint32_t catches_cap;
    int prologue_set;
    int epilogue_set;
    char **param_names;
    uint32_t param_name_count;
    char **local_names;
    char **local_types;
    char **local_sigs;
    uint32_t *local_regs;
    uint32_t local_count;
    smali_annotation_t *annots;
    uint32_t annot_count;
    uint32_t annot_cap;
    smali_annotation_t **param_annots;
    uint32_t *param_annot_counts;
} smali_method_def_t;

typedef struct {
    char *descriptor;
    char *super_class;
    char *source_file;
    char **interfaces;
    uint32_t interface_count;
    smali_field_def_t *static_fields;
    uint32_t static_field_count;
    uint32_t static_field_cap;
    smali_field_def_t *instance_fields;
    uint32_t instance_field_count;
    uint32_t instance_field_cap;
    smali_method_def_t *direct_methods;
    uint32_t direct_method_count;
    uint32_t direct_method_cap;
    smali_method_def_t *virtual_methods;
    uint32_t virtual_method_count;
    uint32_t virtual_method_cap;
    uint32_t access_flags;
    smali_annotation_t *annots;
    uint32_t annot_count;
    uint32_t annot_cap;
    smali_annotation_t *field_annots;
    uint32_t field_annot_count;
    uint32_t field_annot_cap;
} smali_class_def_t;

typedef struct smali_pool_entry smali_pool_entry_t;

typedef struct {
    char **strings;
    uint32_t count;
    uint32_t cap;
    smali_pool_entry_t **buckets;
    uint32_t bucket_count;
} smali_pool_strings_t;

typedef struct {
    uint32_t string_idx;
} smali_pool_type_t;

typedef struct {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t params_off;
    uint32_t param_count;
    uint32_t *param_type_indices;
} smali_pool_proto_t;

typedef struct {
    uint32_t class_type_idx;
    uint32_t type_idx;
    uint32_t name_idx;
} smali_pool_field_t;

typedef struct {
    uint32_t class_type_idx;
    uint32_t proto_idx;
    uint32_t name_idx;
} smali_pool_method_t;

typedef struct {
    smali_class_def_t *classes;
    uint32_t class_count;
    uint32_t class_cap;

    smali_pool_strings_t strings;
    smali_pool_strings_t types;
    smali_pool_strings_t protos;
    smali_pool_strings_t fields;
    smali_pool_strings_t methods;
} smali_ctx_def_t;

#endif
