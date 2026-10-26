#include "format_arsc_model.h"
#include "format_arsc_fmt.h"
#include "format_arsc_pool.h"
#include <stdlib.h>
#include <string.h>

#define RES_STRING_POOL_TYPE     0x0001
#define RES_TABLE_TYPE           0x0002
#define RES_TABLE_PACKAGE_TYPE   0x0200
#define RES_TABLE_TYPE_TYPE      0x0201
#define RES_TABLE_TYPE_SPEC_TYPE 0x0202

/* Reads the entries of one ResTable_type chunk into the variant. */
static void load_entries(const uint8_t *chunk, uint32_t chunk_size, uint16_t header_size,
                         uint32_t entry_count, uint32_t entries_start, arsc_model_variant *v) {
    if (header_size + entry_count * 4 > chunk_size) return;
    if (entries_start > chunk_size) return;
    const uint8_t *offsets = chunk + header_size;
    const uint8_t *base = chunk + entries_start;
    uint32_t avail = chunk_size - entries_start;
    for (uint32_t i = 0; i < entry_count && i < v->entry_count; i++) {
        uint32_t eo = arsc_f32(offsets + i * 4);
        if (eo == 0xFFFFFFFFu || eo + 16 > avail) continue;
        const uint8_t *e = base + eo;
        arsc_model_entry *me = &v->entries[i];
        me->present = 1;
        me->flags = arsc_f16(e + 2);
        me->key = arsc_f32(e + 4);
        if (me->flags & ARSC_ENTRY_COMPLEX) {
            if (eo + 16 > avail) { me->present = 0; continue; }
            me->parent = arsc_f32(e + 8);
            uint32_t mc = arsc_f32(e + 12);
            me->map_count = mc;
            if (mc) {
                if (eo + 16 + (uint64_t)mc * 12 > avail) {
                    me->present = 0;
                    me->map_count = 0;
                    continue;
                }
                me->map = calloc(mc, sizeof(arsc_map_item));
                if (!me->map) { me->present = 0; me->map_count = 0; continue; }
                for (uint32_t j = 0; j < mc; j++) {
                    const uint8_t *m = e + 16 + j * 12;
                    me->map[j].name = arsc_f32(m);
                    me->map[j].size = arsc_f16(m + 4);
                    me->map[j].res0 = m[6];
                    me->map[j].data_type = m[7];
                    me->map[j].data = arsc_f32(m + 8);
                }
            }
        } else {
            me->data_type = e[11];
            me->data = arsc_f32(e + 12);
        }
    }
}

/* Reads one ResTable_package chunk into a new package of the model. */
static void load_package(arsc_model *m, const uint8_t *pkg, uint32_t size) {
    if (size < 288) return;
    uint16_t pkg_header = arsc_f16(pkg + 2);
    char name[128];
    arsc_package_name(pkg, name, sizeof(name));
    arsc_model_pkg *pk = arsc_model_add_pkg(m, pkg[8], name);
    if (!pk) return;

    uint32_t type_strings_off = arsc_f32(pkg + 268);
    pk->last_public_type = arsc_f32(pkg + 272);
    uint32_t key_strings_off = arsc_f32(pkg + 276);
    pk->last_public_key = arsc_f32(pkg + 280);
    if (pkg_header >= 288) pk->type_id_offset = arsc_f32(pkg + 284);

    if (type_strings_off + 28 <= size) {
        const uint8_t *pool = pkg + type_strings_off;
        pk->type_strings_utf8 = (arsc_f32(pool + 16) & (1u << 8)) != 0;
        uint32_t sc = arsc_f32(pool + 8);
        for (uint32_t i = 0; i < sc; i++) {
            char *s = arsc_pool_get(pool, i);
            arsc_pkg_add_type_string(pk, s ? s : "");
            free(s);
        }
    }
    if (key_strings_off + 28 <= size) {
        const uint8_t *pool = pkg + key_strings_off;
        pk->key_strings_utf8 = (arsc_f32(pool + 16) & (1u << 8)) != 0;
        uint32_t sc = arsc_f32(pool + 8);
        for (uint32_t i = 0; i < sc; i++) {
            char *s = arsc_pool_get(pool, i);
            arsc_pkg_add_key(pk, s ? s : "");
            free(s);
        }
    }

    uint32_t off = pkg_header;
    while (off + 8 <= size) {
        uint16_t ct = arsc_f16(pkg + off);
        uint16_t chs = arsc_f16(pkg + off + 2);
        uint32_t cs = arsc_f32(pkg + off + 4);
        if (cs < 8 || off + cs > size) break;
        if (ct == RES_TABLE_TYPE_SPEC_TYPE && chs >= 16 && off + cs <= size) {
            uint8_t tid = pkg[off + 8];
            uint32_t ec = arsc_f32(pkg + off + 12);
            arsc_model_type *ty = arsc_pkg_find_type(pk, tid);
            if (!ty) ty = arsc_pkg_add_type(pk, tid, ec);
            if (ty && ec > ty->entry_count) {
                uint32_t *nf = realloc(ty->spec_flags, ec * sizeof(uint32_t));
                if (nf) {
                    memset(nf + ty->entry_count, 0, (ec - ty->entry_count) * sizeof(uint32_t));
                    ty->spec_flags = nf;
                    ty->entry_count = ec;
                }
            }
            if (ty) {
                ty->res0 = pkg[off + 9];
                ty->res1 = arsc_f16(pkg + off + 10);
                for (uint32_t i = 0; i < ec && i < ty->entry_count; i++) {
                    ty->spec_flags[i] = arsc_f32(pkg + off + 16 + i * 4);
                }
            }
        } else if (ct == RES_TABLE_TYPE_TYPE && chs >= 20 && off + cs <= size) {
            uint8_t tid = pkg[off + 8];
            uint8_t tflags = pkg[off + 9];
            uint32_t ec = arsc_f32(pkg + off + 12);
            uint32_t es = arsc_f32(pkg + off + 16);
            uint8_t csz = (uint8_t)(chs - 20);
            if (csz > ARSC_CONFIG_BYTES) csz = ARSC_CONFIG_BYTES;
            arsc_model_type *ty = arsc_pkg_find_type(pk, tid);
            if (!ty) ty = arsc_pkg_add_type(pk, tid, ec);
            if (!ty) { off += cs; continue; }
            if (ec > ty->entry_count) {
                uint32_t *nf = realloc(ty->spec_flags, ec * sizeof(uint32_t));
                if (nf) {
                    memset(nf + ty->entry_count, 0, (ec - ty->entry_count) * sizeof(uint32_t));
                    ty->spec_flags = nf;
                    ty->entry_count = ec;
                }
            }
            arsc_model_variant *v = arsc_type_add_variant(ty, ty->entry_count, pkg + off + 20, csz, tflags);
            if (v) {
                v->reserved = arsc_f16(pkg + off + 10);
                load_entries(pkg + off, cs, chs, ec, es, v);
            }
        }
        off += cs;
    }
}

/* Reads a whole resources.arsc into a fresh model. */
arsc_model *arsc_model_load(const uint8_t *data, size_t size) {
    if (!data || size < 12) return NULL;
    if (arsc_f16(data) != RES_TABLE_TYPE) return NULL;
    uint16_t root_header = arsc_f16(data + 2);
    uint32_t root_size = arsc_f32(data + 4);
    if (root_header < 12 || root_size > size) root_size = (uint32_t)size;

    arsc_model *m = arsc_model_new();
    if (!m) return NULL;

    int have_pool = 0;
    uint32_t off = root_header;
    while (off + 8 <= root_size) {
        uint16_t ct = arsc_f16(data + off);
        uint16_t chs = arsc_f16(data + off + 2);
        uint32_t cs = arsc_f32(data + off + 4);
        if (cs < 8 || off + cs > root_size) break;
        if (ct == RES_STRING_POOL_TYPE && !have_pool) {
            have_pool = 1;
            m->utf8 = (arsc_f32(data + off + 16) & (1u << 8)) != 0;
            uint32_t sc = arsc_f32(data + off + 8);
            for (uint32_t i = 0; i < sc; i++) {
                char *s = arsc_pool_get(data + off, i);
                arsc_model_add_value(m, s ? s : "");
                free(s);
            }
        } else if (ct == RES_TABLE_PACKAGE_TYPE) {
            load_package(m, data + off, cs);
        }
        (void)chs;
        off += cs;
    }
    if (!m->pkg_count) {
        arsc_model_free(m);
        return NULL;
    }
    return m;
}
