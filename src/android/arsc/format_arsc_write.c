#include "format_arsc_write.h"
#include "format_arsc_fmt.h"
#include <stdlib.h>
#include <string.h>

#define RES_TABLE_TYPE         0x0002
#define RES_TABLE_PACKAGE_TYPE 0x0200
#define PACKAGE_HEADER_SIZE    288
#define PACKAGE_NAME_SLOTS    128

/* Writes the header of a ResTable_package chunk. */
static int write_package_header(arsc_buf *b, const arsc_model_pkg *pk, uint32_t size,
                                 uint32_t type_strings, uint32_t key_strings) {
    if (arsc_buf_u16(b, RES_TABLE_PACKAGE_TYPE) != 0) return -1;
    if (arsc_buf_u16(b, PACKAGE_HEADER_SIZE) != 0) return -1;
    if (arsc_buf_u32(b, size) != 0) return -1;
    /* The package id is a 32 bit field right after the chunk header. */
    if (arsc_buf_u32(b, pk->id) != 0) return -1;
    uint16_t name[PACKAGE_NAME_SLOTS];
    memset(name, 0, sizeof(name));
    arsc_utf8_to_utf16(pk->name, name, PACKAGE_NAME_SLOTS);
    for (int i = 0; i < PACKAGE_NAME_SLOTS; i++) {
        if (arsc_buf_u16(b, name[i]) != 0) return -1;
    }
    if (arsc_buf_u32(b, type_strings) != 0) return -1;
    if (arsc_buf_u32(b, pk->last_public_type) != 0) return -1;
    if (arsc_buf_u32(b, key_strings) != 0) return -1;
    if (arsc_buf_u32(b, pk->last_public_key) != 0) return -1;
    if (arsc_buf_u32(b, pk->type_id_offset) != 0) return -1;
    return 0;
}

/* Serialises one package: its two string pools and every type it holds. */
static int write_package(arsc_buf *out, const arsc_model_pkg *pk) {
    arsc_buf types_pool, key_pool, chunks;
    arsc_buf_init(&types_pool);
    arsc_buf_init(&key_pool);
    arsc_buf_init(&chunks);
    int rc = -1;

    if (arsc_write_pool(&types_pool, pk->type_strings, pk->type_string_count,
                        pk->type_strings_utf8) != 0) goto done;
    if (arsc_write_pool(&key_pool, pk->key_strings, pk->key_string_count,
                        pk->key_strings_utf8) != 0) goto done;

    for (uint32_t i = 0; i < pk->type_count; i++) {
        const arsc_model_type *t = &pk->types[i];
        if (arsc_write_type_spec(&chunks, t) != 0) goto done;
        for (uint32_t v = 0; v < t->variant_count; v++) {
            if (arsc_write_type(&chunks, t, &t->variants[v]) != 0) goto done;
        }
    }

    uint32_t type_strings = PACKAGE_HEADER_SIZE;
    uint32_t key_strings = type_strings + (uint32_t)types_pool.len;
    uint32_t size = key_strings + (uint32_t)key_pool.len + (uint32_t)chunks.len;

    if (write_package_header(out, pk, size, type_strings, key_strings) != 0) goto done;
    if (arsc_buf_append(out, types_pool.data, types_pool.len) != 0) goto done;
    if (arsc_buf_append(out, key_pool.data, key_pool.len) != 0) goto done;
    if (arsc_buf_append(out, chunks.data, chunks.len) != 0) goto done;
    rc = 0;

done:
    arsc_buf_free(&types_pool);
    arsc_buf_free(&key_pool);
    arsc_buf_free(&chunks);
    return rc;
}

uint8_t *arsc_encode(const arsc_model *m, size_t *out_size) {
    if (!m || !out_size) return NULL;
    arsc_buf out;
    arsc_buf_init(&out);

    if (arsc_buf_u16(&out, RES_TABLE_TYPE) != 0) goto fail;
    if (arsc_buf_u16(&out, 12) != 0) goto fail;
    if (arsc_buf_u32(&out, 0) != 0) goto fail;
    if (arsc_buf_u32(&out, m->pkg_count) != 0) goto fail;

    if (arsc_write_pool(&out, m->values, m->value_count, m->utf8) != 0) goto fail;
    for (uint32_t i = 0; i < m->pkg_count; i++) {
        if (write_package(&out, &m->pkgs[i]) != 0) goto fail;
    }
    if (arsc_buf_patch32(&out, 4, (uint32_t)out.len) != 0) goto fail;

    *out_size = out.len;
    return out.data;

fail:
    arsc_buf_free(&out);
    *out_size = 0;
    return NULL;
}
