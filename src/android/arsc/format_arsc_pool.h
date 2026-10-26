#ifndef FORMAT_ARSC_POOL_H
#define FORMAT_ARSC_POOL_H

#include <stddef.h>
#include <stdint.h>

/* Reads the string at `index` of a ResStringPool chunk. Returns malloc'ed
   UTF-8 bytes or NULL when the index is out of range. */
char *arsc_pool_get(const uint8_t *pool, uint32_t index);
/* Copies the UTF-16 package name of a ResTable_package header into `out`. */
void arsc_package_name(const uint8_t *pkg, char *out, size_t out_sz);

#endif
