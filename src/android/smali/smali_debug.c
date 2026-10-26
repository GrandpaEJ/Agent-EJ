#include "smali_debug.h"
#include "smali_pool.h"
#include "smali_encoder.h"
#include <string.h>
#include <stdlib.h>

#define DBG_LINE_BASE (-4)
#define DBG_LINE_RANGE 15

/* The string and type indices of the debug information are uleb128p1: the
 * encoded value is the index plus one, so that NO_INDEX (0xffffffff) is
 * encoded as zero. */
static void write_idx_p1(smali_buf_t *b, uint32_t idx) {
    if (idx == 0xFFFFFFFF) {
        buf_write_uleb128(b, 0);
        return;
    }
    buf_write_uleb128(b, idx + 1);
}

uint32_t write_debug_info(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    (void)ctx;
    int has_lines = 0, has_locals = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number >= 0) has_lines = 1;
    }
    if (m->local_count > 0) has_locals = 1;
    if (m->param_name_count == 0 && !has_lines && !m->prologue_set && !m->epilogue_set && !has_locals)
        return 0;

    align_4(b);
    uint32_t offset = b->len;

    int start_line = 1;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number > 0) {
            start_line = m->insns[i].line_number;
            break;
        }
    }

    buf_write_uleb128(b, (uint32_t)(start_line > 0 ? start_line : 1));
    buf_write_uleb128(b, m->param_name_count);
    for (uint32_t pi = 0; pi < m->param_name_count; pi++) {
        uint32_t idx = smali_pool_find(&ctx->strings, m->param_names[pi]);
        write_idx_p1(b, idx);
    }

    int cur_line = start_line;
    uint32_t cur_addr = 0;
    uint32_t *insn_offsets = malloc((m->insns_count + 1) * sizeof(uint32_t));
    if (!insn_offsets) return 0;
    smali_compute_insn_offsets(m, insn_offsets);

    if (m->prologue_set && m->insns_count > 0) {
        buf_write_u8(b, 0x07);
    }

    for (uint32_t li = 0; li < m->local_count; li++) {
        uint32_t reg = m->local_regs[li] & ~0x80000000;
        uint32_t name_idx = m->local_names[li] ? smali_pool_find(&ctx->strings, m->local_names[li]) : 0xFFFFFFFF;
        uint32_t type_idx = m->local_types[li] ? smali_pool_find(&ctx->types, m->local_types[li]) : 0xFFFFFFFF;

        uint32_t sig_idx = 0xFFFFFFFF;
        if (m->local_sigs[li]) sig_idx = smali_pool_find(&ctx->strings, m->local_sigs[li]);

        if (sig_idx != 0xFFFFFFFF) {
            buf_write_u8(b, 0x04);
            buf_write_uleb128(b, reg);
            write_idx_p1(b, name_idx);
            write_idx_p1(b, type_idx);
            write_idx_p1(b, sig_idx);
        } else {
            buf_write_u8(b, 0x03);
            buf_write_uleb128(b, reg);
            write_idx_p1(b, name_idx);
            write_idx_p1(b, type_idx);
        }
    }

    cur_line = start_line;
    cur_addr = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        int ln = m->insns[i].line_number;
        uint32_t addr_diff = insn_offsets[i] - cur_addr;
        if (ln <= 0) {
            /* No line number for this instruction: only the address moves. */
            if (addr_diff > 0) {
                buf_write_u8(b, 0x01);
                buf_write_uleb128(b, addr_diff);
                cur_addr = insn_offsets[i];
            }
            continue;
        }
        int line_diff = ln - cur_line;
        if (line_diff < DBG_LINE_BASE || line_diff >= DBG_LINE_BASE + DBG_LINE_RANGE) {
            buf_write_u8(b, 0x02);
            buf_write_sleb128(b, line_diff);
            cur_line = ln;
            line_diff = 0;
        }
        if (addr_diff > 15) {
            buf_write_u8(b, 0x01);
            buf_write_uleb128(b, addr_diff);
            cur_addr = insn_offsets[i];
            addr_diff = 0;
        }
        buf_write_u8(b, (uint8_t)(0x0A + addr_diff * DBG_LINE_RANGE + (line_diff - DBG_LINE_BASE)));
        cur_line = ln;
        cur_addr = insn_offsets[i];
    }
    free(insn_offsets);

    /* The epilogue marker closes the region of the epilogue. */
    if (m->epilogue_set) {
        buf_write_u8(b, 0x08);
    }

    buf_write_u8(b, 0x00);
    (void)has_locals;
    return offset;
}
