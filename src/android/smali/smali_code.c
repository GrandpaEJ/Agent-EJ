#include "smali_code.h"
#include "smali_encoder.h"
#include "smali_pool.h"
#include "smali_debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_HANDLERS 64

typedef struct {
    uint32_t type_idx;
    uint32_t handler_addr;
} write_handler_t;

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    write_handler_t handlers[MAX_HANDLERS];
    uint32_t handlers_count;
} write_try_range_t;

static int comp_ranges(const void *a, const void *b) {
    const write_try_range_t *ra = (const write_try_range_t *)a;
    const write_try_range_t *rb = (const write_try_range_t *)b;
    if (ra->start_addr != rb->start_addr) {
        return (ra->start_addr < rb->start_addr) ? -1 : 1;
    }
    return (ra->end_addr < rb->end_addr) ? -1 : 1;
}

uint32_t write_code_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    if (m->access_flags & 0x0500) return 0;
    if (m->insns_count == 0 || m->insns_count > 65536) return 0;
    if (!m->insns) { fprintf(stderr, "BUG: insns is NULL for method with insns_count=%u\n", m->insns_count); return 0; }
    align_4(b);
    uint32_t offset = b->len;

    uint32_t max_units = m->insns_count * 6;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        switch (m->insns[i].fmt) {
            case 101: max_units += 4 + m->insns[i].payload_targets_count * 2; break;
            case 102: max_units += 2 + m->insns[i].payload_targets_count * 4; break;
            case 103: max_units += 2 + (m->insns[i].payload_data_len + 1) / 2 + 2; break;
            default: break;
        }
    }
    uint16_t *code_buf = malloc(max_units * sizeof(uint16_t));
    uint32_t code_words = smali_encode_method_insns(ctx, m, code_buf);

    write_try_range_t *ranges = calloc(m->catches_count ? m->catches_count : 1, sizeof(write_try_range_t));
    uint32_t range_count = 0;

    if (m->catches_count > 0) {
        uint32_t *insn_offsets = malloc((m->insns_count + 1) * sizeof(uint32_t));
        smali_compute_insn_offsets(m, insn_offsets);
        uint32_t cur_offset = 0; (void)cur_offset;
        for (uint32_t c_idx = 0; c_idx < m->catches_count; c_idx++) {
            smali_catch_t *ctch = &m->catches[c_idx];
            int found_start = 0, found_end = 0, found_handler = 0;
            uint32_t start_addr = 0;
            uint32_t end_addr = 0;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (strcmp(m->labels[j].name, ctch->start_label) == 0) {
                    start_addr = insn_offsets[m->labels[j].offset];
                    found_start = 1;
                }
                if (strcmp(m->labels[j].name, ctch->end_label) == 0) {
                    end_addr = insn_offsets[m->labels[j].offset];
                    found_end = 1;
                }
            }
            uint32_t handler_addr = 0;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (strcmp(m->labels[j].name, ctch->handler_label) == 0) {
                    handler_addr = insn_offsets[m->labels[j].offset];
                    found_handler = 1;
                    break;
                }
            }
            if (!found_start || !found_end || !found_handler) continue;
            uint32_t type_idx = 0xFFFFFFFF;
            if (ctch->type) {
                type_idx = smali_pool_find(&ctx->types, ctch->type);
            }

            int found_range = -1;
            for (uint32_t r = 0; r < range_count; r++) {
                if (ranges[r].start_addr == start_addr && ranges[r].end_addr == end_addr) {
                    found_range = (int)r;
                    break;
                }
            }

            if (found_range == -1) {
                write_try_range_t *rng = &ranges[range_count++];
                rng->start_addr = start_addr;
                rng->end_addr = end_addr;
                rng->handlers_count = 1;
                rng->handlers[0].type_idx = type_idx;
                rng->handlers[0].handler_addr = handler_addr;
            } else {
                write_try_range_t *rng = &ranges[found_range];
                if (rng->handlers_count >= MAX_HANDLERS) continue;
                if (type_idx == 0xFFFFFFFF) {
                    rng->handlers[rng->handlers_count++] = (write_handler_t){type_idx, handler_addr};
                } else {
                    int catchall_idx = -1;
                    for (uint32_t h = 0; h < rng->handlers_count; h++) {
                        if (rng->handlers[h].type_idx == 0xFFFFFFFF) {
                            catchall_idx = (int)h;
                            break;
                        }
                    }
                    if (catchall_idx != -1) {
                        for (int h = (int)rng->handlers_count; h > catchall_idx; h--) {
                            rng->handlers[h] = rng->handlers[h - 1];
                        }
                        rng->handlers[catchall_idx] = (write_handler_t){type_idx, handler_addr};
                        rng->handlers_count++;
                    } else {
                        rng->handlers[rng->handlers_count++] = (write_handler_t){type_idx, handler_addr};
                    }
                }
            }
        }
        qsort(ranges, range_count, sizeof(write_try_range_t), comp_ranges);
        free(insn_offsets);
    }

    uint16_t outs_size = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].fmt == 14 || m->insns[i].fmt == 15) {
            if (m->insns[i].vA > outs_size) {
                outs_size = m->insns[i].vA;
            }
        }
    }

    {
        uint32_t valid_count = 0;
        for (uint32_t r = 0; r < range_count; r++) {
            if (ranges[r].end_addr <= ranges[r].start_addr) continue;
            if (ranges[r].end_addr > code_words) continue;
            int valid_handlers = 1;
            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF &&
                    ranges[r].handlers[h].type_idx >= ctx->types.count) {
                    valid_handlers = 0;
                    break;
                }
            }
            if (!valid_handlers) continue;
            if (valid_count != r)
                ranges[valid_count] = ranges[r];
            valid_count++;
        }
        range_count = valid_count;
    }

    buf_write_u16(b, m->registers_count);
    buf_write_u16(b, m->ins_count);
    buf_write_u16(b, outs_size);
    buf_write_u16(b, range_count);
    uint32_t dbg_off_slot = b->len;
    buf_write_u32(b, 0);
    buf_write_u32(b, code_words);
    buf_write(b, code_buf, code_words * 2);

    if (range_count > 0) {
        {
            uint32_t valid_count = 0;
            for (uint32_t r = 0; r < range_count; r++) {
                if (ranges[r].end_addr <= ranges[r].start_addr) continue;
                if (ranges[r].end_addr > code_words) continue;
                int valid_handlers = 1;
                for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                    if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF &&
                        ranges[r].handlers[h].type_idx >= (uint32_t)ctx->types.count) {
                        valid_handlers = 0;
                        break;
                    }
                }
                if (!valid_handlers) continue;
                if (valid_count != r)
                    ranges[valid_count] = ranges[r];
                valid_count++;
            }
            range_count = valid_count;
        }
    }

    if (range_count > 0) {
        if (code_words % 2 != 0) {
            buf_write_u16(b, 0);
        }

        smali_buf_t h_buf;
        buf_init(&h_buf);
        buf_write_uleb128(&h_buf, range_count);

        uint32_t *handler_list_offsets = malloc(range_count * sizeof(uint32_t));
        for (uint32_t r = 0; r < range_count; r++) {
            handler_list_offsets[r] = h_buf.len;
            int has_catchall = 0;
            uint32_t explicit_count = 0;
            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx == 0xFFFFFFFF) has_catchall = 1;
                else explicit_count++;
            }
            int32_t size = has_catchall ? -((int32_t)explicit_count) : (int32_t)explicit_count;
            buf_write_sleb128(&h_buf, size);

            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF) {
                    buf_write_uleb128(&h_buf, ranges[r].handlers[h].type_idx);
                    buf_write_uleb128(&h_buf, ranges[r].handlers[h].handler_addr);
                }
            }
            if (has_catchall) {
                for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                    if (ranges[r].handlers[h].type_idx == 0xFFFFFFFF) {
                        buf_write_uleb128(&h_buf, ranges[r].handlers[h].handler_addr);
                        break;
                    }
                }
            }
        }

        for (uint32_t r = 0; r < range_count; r++) {
            buf_write_u32(b, ranges[r].start_addr);
            buf_write_u16(b, (uint16_t)(ranges[r].end_addr - ranges[r].start_addr));
            buf_write_u16(b, (uint16_t)handler_list_offsets[r]);
        }

        buf_write(b, h_buf.buf, h_buf.len);
        buf_free(&h_buf);
        free(handler_list_offsets);
    }

    uint32_t debug_info_off = write_debug_info(ctx, b, m);
    if (debug_info_off != 0) {
        *(uint32_t *)(b->buf + dbg_off_slot) = debug_info_off;
    }

    free(code_buf);
    free(ranges);
    align_4(b);
    return offset;
}
