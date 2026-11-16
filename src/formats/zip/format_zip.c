#include "format_zip_internal.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

zip_archive *zip_open(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 22) { close(fd); return NULL; }
    uint8_t *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) { close(fd); return NULL; }

    /* scan back for the EOCD; an unsigned offset must not step below 0, or a
     * small non-zip file (search == 0) wraps around and reads far out of the map */
    size_t eocd_off = st.st_size - 22;
    size_t search = st.st_size > 65557 ? st.st_size - 65557 : 0;
    int found = 0;
    for (;;) {
        if (r32(data + eocd_off) == EOCD_SIG) { found = 1; break; }
        if (eocd_off == search) break;
        eocd_off--;
    }
    if (!found) { munmap(data, st.st_size); close(fd); return NULL; }

    eocd_t *eocd = (eocd_t *)(data + eocd_off);
    int n = eocd->num_total;
    zip_archive *za = malloc(sizeof(zip_archive));
    if (!za) { munmap(data, st.st_size); close(fd); return NULL; }
    za->fd = fd;
    za->size = st.st_size;
    za->data = data;
    za->num_entries = n;
    za->entries = NULL;

    if (n > 0) {
        za->entries = calloc(n, sizeof(struct zip_entry));
        if (!za->entries) { zip_close(za); return NULL; }
        uint32_t off = eocd->cd_offset;
        for (int i = 0; i < n; i++) {
            if ((size_t)off + sizeof(cdfh_t) > st.st_size) break;
            cdfh_t *c = (cdfh_t *)(data + off);
            if (c->sig != CDFH_SIG) break;
            struct zip_entry *e = &za->entries[i];
            e->lh_offset = c->lh_offset;
            e->comp_size = c->comp_size;
            e->uncomp_size = c->uncomp_size;
            e->method = c->method;
            e->crc32 = c->crc32;
            e->name_len = c->name_len;
            e->name = malloc(c->name_len + 1);
            if (e->name) {
                memcpy(e->name, (uint8_t *)c + sizeof(cdfh_t), c->name_len);
                e->name[c->name_len] = '\0';
            }
            off += sizeof(cdfh_t) + c->name_len + c->extra_len + c->comment_len;
        }
    }
    return za;
}

void zip_close(zip_archive *za) {
    if (!za) return;
    if (za->entries) {
        for (int i = 0; i < za->num_entries; i++)
            free(za->entries[i].name);
        free(za->entries);
    }
    if (za->data) munmap(za->data, za->size);
    if (za->fd >= 0) close(za->fd);
    free(za);
}

int zip_get_num_entries(zip_archive *za) {
    return za ? za->num_entries : 0;
}

const char *zip_get_entry_name(zip_archive *za, int index) {
    if (!za || index < 0 || index >= za->num_entries) return NULL;
    return za->entries[index].name;
}

size_t zip_get_entry_size(zip_archive *za, int index) {
    if (!za || index < 0 || index >= za->num_entries) return 0;
    return za->entries[index].uncomp_size;
}

int zip_entry_is_compressed(zip_archive *za, int index) {
    if (!za || index < 0 || index >= za->num_entries) return 0;
    return za->entries[index].method == 8;
}
