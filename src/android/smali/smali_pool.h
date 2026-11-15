#ifndef SMALI_POOL_H
#define SMALI_POOL_H

#include "smali_types.h"

void smali_pool_init(smali_pool_strings_t *p);
void smali_pool_free(smali_pool_strings_t *p);
uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str);
uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str);
void smali_pool_sort_strings(smali_pool_strings_t *p);
/* DEX string order: <0, 0, >0 comparing by UTF-16 code unit (MUTF-8 input). */
int smali_utf16_cmp(const char *a, const char *b);
/* Final gate before serialization: string_ids strictly ascending in UTF-16
 * order with no duplicates, and type_ids strictly ascending by string index
 * with every descriptor present in the string pool. 0 ok, -1 + error. */
int smali_validate_pool(smali_ctx_def_t *ctx);
void smali_pool_build_all(smali_ctx_def_t *ctx);

void smali_ctx_free(smali_ctx_def_t *ctx);

/* Hash table for O(1) string pool lookups */
#define POOL_HASH_SIZE 4096

struct smali_pool_entry {
    uint32_t idx;
    smali_pool_entry_t *next;
};

#endif
