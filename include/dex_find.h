#ifndef DEX_FIND_H
#define DEX_FIND_H

#include <stdint.h>

/* Dex editor search (dex_find in formats.h). kind: what to match; flags:
 * DEX_FIND_CASE / _REGEX / _SHALLOW. The callback gets the class index, the
 * 0-based smali line of the hit (0 for class-name hits) and the line text;
 * it returns non-zero to stop. */
enum { DEX_FIND_SMALI, DEX_FIND_CLASS, DEX_FIND_FIELD, DEX_FIND_METHOD, DEX_FIND_STRING, DEX_FIND_INTEGER };
enum { DEX_FIND_CASE = 1, DEX_FIND_REGEX = 2, DEX_FIND_SHALLOW = 4 };
typedef int (*dex_find_fn)(void *user, uint32_t class_idx, uint32_t line, const char *text);

#endif
