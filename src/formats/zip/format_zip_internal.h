#ifndef FORMAT_ZIP_INTERNAL_H
#define FORMAT_ZIP_INTERNAL_H

#include "formats.h"
#include <stdint.h>

#define EOCD_SIG 0x06054b50
#define CDFH_SIG 0x02014b50
#define LFH_SIG  0x04034b50

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t disk_num;
    uint16_t disk_start;
    uint16_t num_on_disk;
    uint16_t num_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
} eocd_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t ver_by;
    uint16_t ver_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t lh_offset;
} cdfh_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t ver_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
} lfh_t;

struct zip_entry {
    uint32_t lh_offset;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t method;
    uint32_t crc32;
    uint16_t name_len;
    char *name;
};

struct zip_archive {
    int fd;
    size_t size;
    uint8_t *data;
    int num_entries;
    struct zip_entry *entries;
};

// Inflate from format_zip_inflate.c
void *zip_inflate(const uint8_t *in, size_t in_len, size_t *out_size);

// Deflate (format_zip_deflate.c, format_zip_huff.c)
#define ZIP_HUFF_MAXSYM 286
uint8_t *zip_deflate(const uint8_t *in, size_t n, size_t *out_n);
void zip_huff_lengths(const uint32_t *freq, int n, int limit, uint8_t *len);
void zip_huff_codes(const uint8_t *len, int n, uint16_t *code);

#endif
