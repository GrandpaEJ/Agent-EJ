#include "format_zip_internal.h"
#include "formats.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *my_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}



struct zip_entry_w {
    char *name;
    uint16_t name_len;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t method;
    uint32_t lh_offset;
};

struct zip_writer {
    FILE *fp;
    int num_entries;
    int cap_entries;
    struct zip_entry_w *entries;
};

static uint32_t crc32_table[256];
static int crc_initialized;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc_initialized = 1;
}

static uint32_t crc32_buf(const void *buf, size_t len) {
    if (!crc_initialized) crc32_init();
    uint32_t c = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        c = crc32_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}


zip_writer *zip_writer_open(const char *path) {
    zip_writer *zw = calloc(1, sizeof(zip_writer));
    if (!zw) return NULL;
    zw->fp = fopen(path, "wb");
    if (!zw->fp) { free(zw); return NULL; }
    zw->cap_entries = 64;
    zw->entries = calloc(zw->cap_entries, sizeof(struct zip_entry_w));
    if (!zw->entries) { fclose(zw->fp); free(zw); return NULL; }
    return zw;
}

int zip_writer_add_raw(zip_writer *zw, const char *name,
                       const void *comp_data, size_t comp_size, size_t uncomp_size, uint32_t crc32, int method, int alignment) {
    if (!zw || !name || !comp_data) return -1;

    if (zw->num_entries >= zw->cap_entries) {
        zw->cap_entries *= 2;
        struct zip_entry_w *tmp = realloc(zw->entries,
            zw->cap_entries * sizeof(struct zip_entry_w));
        if (!tmp) return -1;
        zw->entries = tmp;
    }

    uint16_t name_len = (uint16_t)strlen(name);
    long off = ftell(zw->fp);

    uint16_t extra_len = 0;
    if (alignment > 0) {
        long data_offset = off + 30 + name_len;
        int pad = (alignment - (data_offset % alignment)) % alignment;
        extra_len = pad;
    }

    lfh_t lfh;
    memset(&lfh, 0, sizeof(lfh));
    lfh.sig = LFH_SIG;
    lfh.ver_needed = 20;
    lfh.method = method;
    lfh.crc32 = crc32;
    lfh.comp_size = (uint32_t)comp_size;
    lfh.uncomp_size = (uint32_t)uncomp_size;
    lfh.name_len = name_len;
    lfh.extra_len = extra_len;

    fwrite(&lfh, sizeof(lfh), 1, zw->fp);
    fwrite(name, 1, name_len, zw->fp);
    if (extra_len > 0) {
        char pad_buf[4096] = {0};
        fwrite(pad_buf, 1, extra_len, zw->fp);
    }
    fwrite(comp_data, 1, comp_size, zw->fp);

    struct zip_entry_w *e = &zw->entries[zw->num_entries++];
    e->name = my_strdup(name);
    e->name_len = name_len;
    e->crc32 = crc32;
    e->comp_size = (uint32_t)comp_size;
    e->uncomp_size = (uint32_t)uncomp_size;
    e->method = method;
    e->lh_offset = (uint32_t)off;
    return 0;
}

int zip_writer_add(zip_writer *zw, const char *name,
                   const void *data, size_t size, int compress, int alignment) {
    (void)compress; // We always store for now
    uint32_t crc = crc32_buf(data, size);
    return zip_writer_add_raw(zw, name, data, size, size, crc, 0, alignment);
}

int zip_writer_close(zip_writer *zw) {
    if (!zw || !zw->fp) return -1;
    long cd_start = ftell(zw->fp);

    // Write Central Directory
    for (int i = 0; i < zw->num_entries; i++) {
        struct zip_entry_w *e = &zw->entries[i];
        cdfh_t cdfh;
        memset(&cdfh, 0, sizeof(cdfh));
        cdfh.sig = CDFH_SIG;
        cdfh.ver_by = 20;
        cdfh.ver_needed = 20;
        cdfh.method = e->method;
        cdfh.crc32 = e->crc32;
        cdfh.comp_size = e->comp_size;
        cdfh.uncomp_size = e->uncomp_size;
        cdfh.name_len = e->name_len;
        cdfh.lh_offset = e->lh_offset;
        fwrite(&cdfh, sizeof(cdfh), 1, zw->fp);
        fwrite(e->name, 1, e->name_len, zw->fp);
    }

    long cd_end = ftell(zw->fp);
    long cd_size = cd_end - cd_start;

    // Write EOCD
    eocd_t eocd;
    memset(&eocd, 0, sizeof(eocd));
    eocd.sig = EOCD_SIG;
    eocd.num_on_disk = (uint16_t)zw->num_entries;
    eocd.num_total = (uint16_t)zw->num_entries;
    eocd.cd_size = (uint32_t)cd_size;
    eocd.cd_offset = (uint32_t)cd_start;
    fwrite(&eocd, sizeof(eocd), 1, zw->fp);

    fclose(zw->fp);
    for (int i = 0; i < zw->num_entries; i++)
        free(zw->entries[i].name);
    free(zw->entries);
    free(zw);
    return 0;
}

/* Copies entry [index] of za as it is (compressed bytes, method, CRC), so a
 * deflated entry stays deflated. Stored entries get [alignment] (4096 for .so). */
int zip_copy_entry(zip_writer *zw, zip_archive *za, int index, int alignment) {
    if (index < 0 || index >= za->num_entries) return -1;
    struct zip_entry *e = &za->entries[index];
    if ((size_t)e->lh_offset + sizeof(lfh_t) > za->size) return -1;
    lfh_t *lfh = (lfh_t *)(za->data + e->lh_offset);
    size_t data_off = (size_t)e->lh_offset + sizeof(lfh_t) + lfh->name_len + lfh->extra_len;
    if (data_off + e->comp_size > za->size) return -1;
    const char *name = zip_get_entry_name(za, index);
    int align = 0;
    if (e->method == 0) {
        size_t n = strlen(name);
        align = n > 3 && strcmp(name + n - 3, ".so") == 0 ? 4096 : alignment;
    }
    return zip_writer_add_raw(zw, name, za->data + data_off, e->comp_size, e->uncomp_size, e->crc32, e->method, align);
}

int zipalign_file(const char *in_path, const char *out_path, int alignment) {
    zip_archive *za = zip_open(in_path);
    if (!za) { printf("zip_open failed\n"); return -1; }
    zip_writer *zw = zip_writer_open(out_path);
    if (!zw) { printf("zip_writer_open failed\n"); zip_close(za); return -1; }
    int rc = 0;
    int n = zip_get_num_entries(za);
    for (int i = 0; i < n && rc == 0; i++) rc = zip_copy_entry(zw, za, i, alignment) == 0 ? 0 : -1;
    zip_close(za);
    return zip_writer_close(zw) == 0 ? rc : -1;
}
