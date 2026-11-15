#ifndef SMALI_TYPELIST_H
#define SMALI_TYPELIST_H

#include "smali_buf.h"
#include "smali_types.h"
#include "smali_blobset.h"
#include <stdint.h>

/* Registry of the type_list items written into a DEX.
 *
 * A type_list is a list of type indices: the parameter list of a proto, or
 * the interface list of a class. Identical lists are shared. Lists are first
 * collected, then written in one run sorted the way smali.jar sorts them
 * (element by element, a shorter prefix first). */
typedef struct {
    smali_blobset_t lists;   /* the u16 type indices of each distinct list */
    uint32_t first_offset;   /* offset of the first list written */
} smali_typelist_reg_t;

void smali_typelist_reg_init(smali_typelist_reg_t *reg);
void smali_typelist_reg_free(smali_typelist_reg_t *reg);

/* Registers a list; returns its id, or UINT32_MAX for an empty list (which
 * is not written and is referenced as offset 0). */
uint32_t smali_typelist_add(smali_typelist_reg_t *reg, const uint16_t *types, uint32_t count);

/* Writes every registered list, sorted, into buf. */
void smali_typelist_flush(smali_typelist_reg_t *reg, smali_buf_t *buf);

/* File offset of a list once flushed (0 for UINT32_MAX). */
uint32_t smali_typelist_offset(const smali_typelist_reg_t *reg, uint32_t id);

static inline uint32_t smali_typelist_count(const smali_typelist_reg_t *reg) {
    return reg->lists.count;
}

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
