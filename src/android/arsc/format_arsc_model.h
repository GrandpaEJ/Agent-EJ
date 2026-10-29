#ifndef FORMAT_ARSC_MODEL_H
#define FORMAT_ARSC_MODEL_H

#include <stddef.h>
#include <stdint.h>

/* Size of the ResTable_config blob as aapt2 writes it (size field included). */
#define ARSC_CONFIG_BYTES 64

/* ResTable_entry flags */
#define ARSC_ENTRY_COMPLEX 0x0001u
#define ARSC_ENTRY_PUBLIC  0x0002u
#define ARSC_ENTRY_WEAK    0x0004u
#define ARSC_ENTRY_COMPACT 0x0008u

/* Res_value data types */
#define ARSC_TYPE_NULL       0x00u
#define ARSC_TYPE_REFERENCE  0x01u
#define ARSC_TYPE_ATTRIBUTE  0x02u
#define ARSC_TYPE_STRING     0x03u
#define ARSC_TYPE_FLOAT      0x04u
#define ARSC_TYPE_INT_DEC    0x10u
#define ARSC_TYPE_INT_HEX    0x11u
#define ARSC_TYPE_INT_BOOLEAN 0x12u

/* One ResTable_map item (an attribute of a complex entry). It carries a full
   Res_value, so it is 12 bytes on disk. */
typedef struct {
    uint32_t name;      /* resource id of the attribute */
    uint16_t size;      /* Res_value.size */
    uint8_t  res0;
    uint8_t  data_type; /* Res_value.dataType */
    uint32_t data;      /* Res_value.data */
} arsc_map_item;

/* One resource entry of one configuration. */
typedef struct {
    uint16_t flags;       /* ResTable_entry.flags */
    uint32_t key;         /* index in the package key string pool */
    uint32_t parent;      /* ResTable_map_entry.parent */
    uint8_t  data_type;   /* Res_value.dataType */
    uint8_t  present;     /* 0 when the entry is absent in this configuration */
    uint32_t data;        /* Res_value.data */
    uint32_t map_count;
    arsc_map_item *map;
} arsc_model_entry;

/* One ResTable_type chunk: a type in a single configuration. */
typedef struct {
    uint8_t  config[ARSC_CONFIG_BYTES];  /* raw ResTable_config, size field first */
    uint8_t  config_size;                /* how many bytes of `config` are valid */
    uint8_t  flags;                      /* ResTable_type.flags */
    uint16_t reserved;                   /* ResTable_type.reserved */
    uint32_t entry_count;
    arsc_model_entry *entries;
} arsc_model_variant;

/* One ResTable_typeSpec chunk plus every configuration of that type. */
typedef struct {
    uint8_t  id;
    uint8_t  res0;        /* ResTable_typeSpec.res0 */
    uint16_t res1;        /* ResTable_typeSpec.res1, aapt2 stores the config flags */
    uint32_t entry_count;
    uint32_t *spec_flags;      /* one flag word per entry, copied from the type spec */
    arsc_model_variant *variants;
    uint32_t variant_count;
} arsc_model_type;

/* One ResTable_package. */
typedef struct {
    uint8_t id;
    char name[128];
    uint32_t last_public_type;
    uint32_t last_public_key;
    uint32_t type_id_offset;
    int type_strings_utf8;
    int key_strings_utf8;
    char **type_strings;
    uint32_t type_string_count;
    char **key_strings;
    uint32_t key_string_count;
    arsc_model_type *types;
    uint32_t type_count;
} arsc_model_pkg;

/* A whole resource table. */
typedef struct {
    int utf8;                 /* encoding of the global string pool */
    char **values;
    uint32_t value_count;
    arsc_model_pkg *pkgs;
    uint32_t pkg_count;
} arsc_model;

arsc_model *arsc_model_new(void);
void arsc_model_free(arsc_model *m);
/* Reads a whole resources.arsc image into a fresh model. */
arsc_model *arsc_model_load(const uint8_t *data, size_t size);
arsc_model_pkg *arsc_model_add_pkg(arsc_model *m, uint8_t id, const char *name);
/* Appends to the global string pool, returns the index of the new string. */
uint32_t arsc_model_add_value(arsc_model *m, const char *s);
const char *arsc_model_value(const arsc_model *m, uint32_t index);
uint32_t arsc_pkg_add_type_string(arsc_model_pkg *p, const char *s);
uint32_t arsc_pkg_add_key(arsc_model_pkg *p, const char *s);
const char *arsc_pkg_type_string(const arsc_model_pkg *p, uint32_t index);
const char *arsc_pkg_key(const arsc_model_pkg *p, uint32_t index);
arsc_model_type *arsc_pkg_find_type(arsc_model_pkg *p, uint8_t id);
arsc_model_type *arsc_pkg_add_type(arsc_model_pkg *p, uint8_t id, uint32_t entry_count);
arsc_model_variant *arsc_type_add_variant(arsc_model_type *t, uint32_t entry_count,
                                          const uint8_t *config, uint8_t config_size, uint8_t flags);
/* Returns the entry at `index` or NULL when the index is out of range. */
arsc_model_entry *arsc_variant_entry(arsc_model_variant *v, uint32_t index);
/* True when the variant carries no qualifier (the default configuration). */
int arsc_variant_is_default(const arsc_model_variant *v);
/* Finds the entry of a resource id in the default configuration of its type. */
arsc_model_entry *arsc_model_find_entry(arsc_model *m, uint32_t res_id);
/* Points a resource at a new string in the global pool. */
int arsc_model_set_string(arsc_model *m, uint32_t res_id, const char *text);
/* Points a resource at a new integer value. */
int arsc_model_set_int(arsc_model *m, uint32_t res_id, int32_t value, uint8_t data_type);
/* Folds every configuration of `split` into `base` (native anti-split).
 * Returns the number of configurations added, or -1 on failure. */
int arsc_model_merge(arsc_model *base, const arsc_model *split);

#endif
