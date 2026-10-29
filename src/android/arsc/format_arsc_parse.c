#include "format_arsc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint16_t arsc_r16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t arsc_r32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const char *arsc_sp_string(const uint8_t *pool, uint32_t index) {
    if (!pool) return NULL;
    if (index >= arsc_r32(pool + 8)) return NULL;
    uint32_t flags = arsc_r32(pool + 16);
    uint32_t strs_start = arsc_r32(pool + 20);
    uint32_t hdr_sz = arsc_r16(pool + 2);
    uint32_t off = arsc_r32(pool + hdr_sz + index * 4);
    const uint8_t *p = pool + strs_start + off;
    static char buf[512];
    if (flags & (1<<8)) {
        if (*p & 0x80) p += 2; else p += 1;
        if (*p & 0x80) p += 2; else p += 1;
        size_t slen = strlen((const char*)p);
        if (slen > 511) slen = 511;
        memcpy(buf, p, slen); buf[slen] = 0;
        return buf;
    }
    uint16_t len = arsc_r16(p); p += 2;
    char *out = buf;
    for (uint16_t i = 0; i < len && (size_t)(out - buf) < 510; i++) {
        uint16_t wc = arsc_r16(p + i*2);
        if (wc < 0x80) *out++ = (char)wc;
        else if (wc < 0x800) { *out++ = 0xC0|(wc>>6); *out++ = 0x80|(wc&0x3F); }
        else { *out++ = 0xE0|(wc>>12); *out++ = 0x80|((wc>>6)&0x3F); *out++ = 0x80|(wc&0x3F); }
    }
    *out = 0;
    return buf;
}

arsc_ctx *arsc_parse(const uint8_t *data, size_t size) {
    if (!data || size < 12) return NULL;
    uint16_t type = arsc_r16(data);
    uint16_t hdr_sz = arsc_r16(data + 2);
    uint32_t chunk_sz = arsc_r32(data + 4);
    if (type != RES_TABLE_TYPE || chunk_sz > size) return NULL;
    arsc_ctx *ctx = calloc(1, sizeof(arsc_ctx));
    if (!ctx) return NULL;
    ctx->data = data; ctx->size = chunk_sz;
    uint32_t off = hdr_sz;
    while (off + 8 <= chunk_sz) {
        uint16_t ct = arsc_r16(data + off), chs = arsc_r16(data + off + 2);
        uint32_t cs = arsc_r32(data + off + 4);
        if (cs < 8 || off + cs > chunk_sz) break;
        if (ct == RES_STRING_POOL_TYPE && !ctx->string_pool_data) {
            ctx->string_pool_offset = off;
            ctx->string_pool_data = data + off;
            ctx->string_pool_size = cs;
            ctx->string_count = arsc_r32(data + off + 8);
            ctx->is_utf8 = (arsc_r32(data + off + 16) & (1 << 8)) != 0;
            ctx->string_offsets = (const uint32_t *)(data + off + chs);
            ctx->patched = calloc(ctx->string_count, sizeof(int));
            ctx->patched_strings = calloc(ctx->string_count, sizeof(char *));
            ctx->string_cache = calloc(ctx->string_count, sizeof(char *));
        }
        if (ct == RES_TABLE_PACKAGE_TYPE && ctx->package_count < MAX_PACKAGES) {
            arsc_package *pkg = &ctx->packages[ctx->package_count++];
            memset(pkg, 0, sizeof(arsc_package));
            pkg->id = data[off + 8];
            pkg->pkg_off = off;
            for (int i = 0; i < 128; i++) {
                uint16_t c = arsc_r16(data + off + 12 + i*2);
                if (!c) break;
                pkg->name[i] = (char)c;
            }
            if (!pkg->name[0]) snprintf(pkg->name, sizeof(pkg->name), "pkg_%02x", pkg->id);
            pkg->type_pool_off = off + arsc_r32(data + off + 268);
            pkg->key_pool_off = off + arsc_r32(data + off + 276);
            pkg->type_pool = data + pkg->type_pool_off;
            pkg->key_pool = data + pkg->key_pool_off;
            uint32_t poff = off + chs;
            while (poff + 8 <= off + cs) {
                uint16_t pt = arsc_r16(data + poff);
                uint32_t pcs = arsc_r32(data + poff + 4);
                if (pcs < 8 || poff + pcs > off + cs) break;
                if (pt == RES_TABLE_TYPE_TYPE && pkg->type_count < MAX_TYPES) {
                    uint8_t tid = data[poff + 8];
                    int ti = pkg->type_count++;
                    pkg->types[ti].id = tid;
                    const char *tn = arsc_sp_string(pkg->type_pool, tid - 1);
                    pkg->types[ti].name = tn ? strdup(tn) : NULL;
                    pkg->types[ti].entry_count = arsc_r32(data + poff + 12);
                    pkg->types[ti].entry_offsets = (const uint32_t*)(data + poff + arsc_r16(data + poff + 2));
                    pkg->types[ti].entry_data = data + poff + arsc_r32(data + poff + 16);
                    pkg->types[ti].key_pool = pkg->key_pool;
                    
                    // Parse attr bag entries for type 0x01 (attr) — enum/flag definitions
                    if (tid == 1 && pkg->key_pool) {
                        const uint32_t *eoffs = (const uint32_t*)(data + poff + arsc_r16(data + poff + 2));
                        const uint8_t *eents = data + poff + arsc_r32(data + poff + 16);
                        uint32_t ecount = arsc_r32(data + poff + 12);
                        uint32_t entries_base = poff + arsc_r32(data + poff + 16);
                        for (uint32_t ei = 0; ei < ecount && pkg->attr_entry_count < MAX_ATTR_ENTRIES; ei++) {
                            if (eoffs[ei] == 0xFFFFFFFF) continue;
                            const uint8_t *entry = eents + eoffs[ei];
                            uint16_t eflags = arsc_r16(entry + 2);
                            if (!(eflags & 0x0001)) continue;
                            uint16_t esize = arsc_r16(entry);
                            uint32_t attr_res_id = ((uint32_t)pkg->id << 24) | ((uint32_t)tid << 16) | ei;
                            // ResTable_map_entry layout: ResTable_entry(8) + parent(4) + count(4) + map_entries
                            if (esize < 16) continue;
                            uint32_t map_count = arsc_r32(entry + 12);
                            const uint8_t *map_start = entry + esize;
                            uint32_t abs_map_off = entries_base + eoffs[ei] + esize;
                            uint32_t map_avail = (poff + pcs) - abs_map_off;
                            for (uint32_t mi = 0; mi < map_count && mi * 8 + 8 <= map_avail; mi++) {
                                if (pkg->attr_entry_count >= MAX_ATTR_ENTRIES) break;
                                uint32_t map_name = arsc_r32(map_start + mi * 8);
                                uint32_t map_value = arsc_r32(map_start + mi * 8 + 4);
                                // Look up the key name for this resource ID
                                const char *val_name = arsc_lookup_id(ctx, map_name);
                                if (val_name) {
                                    arsc_attr_map *am = &pkg->attr_entries[pkg->attr_entry_count++];
                                    am->attr_id = attr_res_id;
                                    am->value = (int32_t)map_value;
                                    am->name = strdup(val_name);
                                }
                            }
                        }
                    }
                }
                poff += pcs;
            }
        }
        off += cs;
    }
    return ctx;
}

const char *arsc_lookup_id(arsc_ctx *ctx, uint32_t res_id) {
    if (!ctx) return NULL;
    uint32_t pkg_id = (res_id >> 24) & 0xFF;
    uint8_t type_id = (res_id >> 16) & 0xFF;
    uint32_t entry_idx = res_id & 0xFFFF;
    for (int p = 0; p < ctx->package_count; p++) {
        if (ctx->packages[p].id != pkg_id) continue;
        for (int t = 0; t < ctx->packages[p].type_count; t++) {
            if (ctx->packages[p].types[t].id != type_id) continue;
            if (entry_idx >= ctx->packages[p].types[t].entry_count) break;
            uint32_t eoff = ctx->packages[p].types[t].entry_offsets[entry_idx];
            if (eoff == 0xFFFFFFFF) break;
            const uint8_t *entry = ctx->packages[p].types[t].entry_data + eoff;
            uint32_t key_idx = arsc_r32(entry + 4);
            const char *key = arsc_sp_string(ctx->packages[p].types[t].key_pool, key_idx);
            if (!key) break;
            static char buf[256];
            const char *tn = ctx->packages[p].types[t].name;
            if (!tn) tn = "unknown";
            snprintf(buf, sizeof(buf), "%s/%s", tn, key);
            return buf;
        }
    }
    return NULL;
}

uint32_t arsc_reverse_lookup(arsc_ctx *ctx, const char *type_name, const char *key_name) {
    if (!ctx || !type_name || !key_name) return 0xFFFFFFFF;
    for (int p = 0; p < ctx->package_count; p++) {
        for (int t = 0; t < ctx->packages[p].type_count; t++) {
            if (!ctx->packages[p].types[t].name) continue;
            if (strcmp(ctx->packages[p].types[t].name, type_name) != 0) continue;
            uint32_t poff = ctx->packages[p].pkg_off + arsc_r16(ctx->data + ctx->packages[p].pkg_off + 2);
            uint32_t pkg_end = ctx->packages[p].pkg_off + arsc_r32(ctx->data + ctx->packages[p].pkg_off + 4);
            uint8_t tid = ctx->packages[p].types[t].id;
            while (poff + 8 <= pkg_end) {
                uint16_t pt = arsc_r16(ctx->data + poff);
                uint32_t pcs = arsc_r32(ctx->data + poff + 4);
                if (pcs < 8 || poff + pcs > pkg_end) break;
                if (pt == RES_TABLE_TYPE_TYPE && ctx->data[poff + 8] == tid) {
                    const uint32_t *offsets = (const uint32_t*)(ctx->data + poff + arsc_r16(ctx->data + poff + 2));
                    const uint8_t *entries = ctx->data + poff + arsc_r32(ctx->data + poff + 16);
                    uint32_t ec = arsc_r32(ctx->data + poff + 12);
                    for (uint32_t i = 0; i < ec; i++) {
                        if (offsets[i] == 0xFFFFFFFF) continue;
                        const uint8_t *entry = entries + offsets[i];
                        uint32_t key_idx = arsc_r32(entry + 4);
                        const char *kn = arsc_sp_string(ctx->packages[p].key_pool, key_idx);
                        if (kn && strcmp(kn, key_name) == 0)
                            return ((uint32_t)ctx->packages[p].id << 24) | ((uint32_t)tid << 16) | i;
                    }
                    break; // only check first chunk
                }
                poff += pcs;
            }
        }
    }
    return 0xFFFFFFFF;
}

const char *arsc_attr_enum(arsc_ctx *ctx, uint32_t attr_id, int32_t value) {
    if (!ctx) return NULL;
    for (int p = 0; p < ctx->package_count; p++) {
        for (int i = 0; i < ctx->packages[p].attr_entry_count; i++) {
            if (ctx->packages[p].attr_entries[i].attr_id == attr_id &&
                ctx->packages[p].attr_entries[i].value == value)
                return ctx->packages[p].attr_entries[i].name;
        }
    }
    return NULL;
}

int arsc_attr_enum_value(arsc_ctx *ctx, uint32_t attr_id, const char *name, int32_t *out) {
    if (!ctx || !name) return 0;
    for (int p = 0; p < ctx->package_count; p++) {
        for (int i = 0; i < ctx->packages[p].attr_entry_count; i++) {
            if (ctx->packages[p].attr_entries[i].attr_id == attr_id &&
                ctx->packages[p].attr_entries[i].name &&
                strcmp(ctx->packages[p].attr_entries[i].name, name) == 0) {
                *out = ctx->packages[p].attr_entries[i].value;
                return 1;
            }
        }
    }
    return 0;
}

const char *arsc_get_type_name(arsc_ctx *ctx, uint32_t pkg_id, uint8_t type_id) {
    if (!ctx) return NULL;
    for (int p = 0; p < ctx->package_count; p++) {
        if (ctx->packages[p].id != pkg_id) continue;
        for (int t = 0; t < ctx->packages[p].type_count; t++) {
            if (ctx->packages[p].types[t].id == type_id)
                return ctx->packages[p].types[t].name;
        }
    }
    return NULL;
}

const char *arsc_get_string(arsc_ctx *ctx, uint32_t index) {
    if (!ctx || index >= ctx->string_count) return NULL;
    if (ctx->patched[index]) return ctx->patched_strings[index];
    if (ctx->string_cache[index]) return ctx->string_cache[index];
    uint32_t strs_start = arsc_r32(ctx->string_pool_data + 20);
    uint32_t off = arsc_r32((const uint8_t*)&ctx->string_offsets[index]);
    const uint8_t *p = ctx->string_pool_data + strs_start + off;
    if (ctx->is_utf8) {
        if (*p & 0x80) p += 2; else p += 1;
        if (*p & 0x80) p += 2; else p += 1;
        ctx->string_cache[index] = strdup((const char*)p);
    } else {
        uint16_t len = arsc_r16(p); p += 2;
        char *s = malloc((size_t)len * 3 + 1);
        char *out = s;
        for (uint16_t i = 0; i < len; i++) {
            uint16_t wc = arsc_r16(p + i*2);
            if (wc < 0x80) *out++ = (char)wc;
            else if (wc < 0x800) { *out++ = 0xC0|(wc>>6); *out++ = 0x80|(wc&0x3F); }
            else { *out++ = 0xE0|(wc>>12); *out++ = 0x80|((wc>>6)&0x3F); *out++ = 0x80|(wc&0x3F); }
        }
        *out = 0; ctx->string_cache[index] = s;
    }
    return ctx->string_cache[index];
}

void arsc_free(arsc_ctx *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->string_count; i++) {
        free(ctx->patched_strings[i]);
        free(ctx->string_cache[i]);
    }
    for (int p = 0; p < ctx->package_count; p++) {
        for (int t = 0; t < ctx->packages[p].type_count; t++) {
            free((void*)ctx->packages[p].types[t].name);
        }
        for (int i = 0; i < ctx->packages[p].attr_entry_count; i++) {
            free(ctx->packages[p].attr_entries[i].name);
        }
    }
    free(ctx->patched); free(ctx->patched_strings); free(ctx->string_cache);
    free(ctx);
}
