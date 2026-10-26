#ifndef SMALI_TYPELIST_H
#define SMALI_TYPELIST_H

#include "smali_buf.h"
#include "smali_types.h"
#include <stdint.h>

/* Registry of the type_list items written into a DEX.
 *
 * A type_list is a list of type indices: the parameter list of a proto, or
 * the interface list of a class. The DEX format lets several references
 * share one list; this registry reuses the offset of an identical list that
 * was already written, so the file carries a single copy of it. */
typedef struct {
    uint32_t offset;   /* offset of the type_list in the file */
    uint32_t count;    /* number of type indices */
    uint16_t *types;   /* the type indices (owned by the registry) */
} smali_typelist_t;

typedef struct {
    smali_typelist_t *items;
    uint32_t count;
    uint32_t cap;
    uint32_t first_offset;  /* offset of the first list that was written */
} smali_typelist_reg_t;

void smali_typelist_reg_init(smali_typelist_reg_t *reg);
void smali_typelist_reg_free(smali_typelist_reg_t *reg);

/* Writes the type list into buf, or reuses an identical list that was
 * already written. Returns the offset of the type_list, or 0 for an empty
 * list (which is not written at all). */
uint32_t smali_typelist_write(smali_typelist_reg_t *reg, smali_buf_t *buf,
                               const uint16_t *types, uint32_t count);

/* Parses the parameter list of a proto signature ("(ILjava/lang/String;)V")
 * into type indices. Returns the number of parameters and stores the indices
 * in a freshly allocated array (the caller frees it). */
uint32_t smali_typelist_from_proto(smali_pool_strings_t *types_pool,
                                   const char *sig, uint16_t **out_types);

/* Collects the type indices of a list of type descriptors (the interfaces of
 * a class). Returns the number of entries and stores the indices in a
 * freshly allocated array (the caller frees it). */
uint32_t smali_typelist_from_descriptors(smali_pool_strings_t *types_pool,
                                         char *const *descriptors, uint32_t count,
                                         uint16_t **out_types);

#endif
