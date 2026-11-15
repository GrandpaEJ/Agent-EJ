#include "smali_debug.h"
#include "smali_pool.h"
#include "smali_encoder.h"
#include "smali_params.h"
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

/* Line/address state machine, with the delta rules of dexlib2's
 * DebugWriter so the stream is byte-identical to smali.jar's. */
typedef struct {
    smali_buf_t *b;
    int32_t line;
    uint32_t addr;
} dbg_state_t;

static void advance_pc(dbg_state_t *s, uint32_t addr) {
    if (addr > s->addr) {
        buf_write_u8(s->b, 0x01);
        buf_write_uleb128(s->b, addr - s->addr);
        s->addr = addr;
    }
}

static void emit_line(dbg_state_t *s, uint32_t addr, int32_t line) {
    int32_t ld = line - s->line;
    uint32_t ad = addr - s->addr;
    if (ld < DBG_LINE_BASE || ld > DBG_LINE_BASE + DBG_LINE_RANGE - 1) {
        buf_write_u8(s->b, 0x02);
        buf_write_sleb128(s->b, ld);
        s->line = line;
        ld = 0;
    }
    /* The special opcode is one byte: 0x0a + ad*15 + (ld+4) <= 0xff. */
    if ((ld < 2 && ad > 16) || (ld > 1 && ad > 15)) {
        advance_pc(s, addr);
        ad = 0;
    }
    /* Emitted even when both deltas are 0: it is what creates the entry. */
    buf_write_u8(s->b, (uint8_t)(0x0A + ad * DBG_LINE_RANGE + (uint32_t)(ld - DBG_LINE_BASE)));
    s->line += ld;
    s->addr += ad;
}

/* Debug registers are absolute: pN lives after the method's locals. */
static uint32_t dbg_reg(const smali_method_def_t *m, uint32_t reg) {
    if (!(reg & 0x80000000)) return reg;
    return m->registers_count - m->ins_count + (reg & ~0x80000000u);
}

static void emit_local(smali_ctx_def_t *ctx, dbg_state_t *s, const smali_method_def_t *m,
                       const smali_dbg_event_t *e) {
    uint32_t li = e->local;
    uint32_t name = m->local_names[li] ? smali_pool_find(&ctx->strings, m->local_names[li]) : 0xFFFFFFFF;
    uint32_t type = m->local_types[li] ? smali_pool_find(&ctx->types, m->local_types[li]) : 0xFFFFFFFF;
    uint32_t sig = m->local_sigs[li] ? smali_pool_find(&ctx->strings, m->local_sigs[li]) : 0xFFFFFFFF;
    buf_write_u8(s->b, sig != 0xFFFFFFFF ? 0x04 : 0x03);
    buf_write_uleb128(s->b, dbg_reg(m, e->reg));
    write_idx_p1(s->b, name);
    write_idx_p1(s->b, type);
    if (sig != 0xFFFFFFFF) write_idx_p1(s->b, sig);
}

uint32_t write_debug_info(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    int named = 0;
    for (uint32_t i = 0; i < m->param_name_count && !named; i++) named = m->param_names[i] != NULL;
    if (!named && m->dbg_count == 0) return 0;

    /* debug_info_item has no alignment requirement in the DEX spec, and
     * items are packed back-to-back in their own section (see the caller in
     * smali_writer.c) — padding here would desync a sequential walk. */
    uint32_t offset = b->len;

    /* line_start is the first .line (0 is a real line D8 emits for synthetic
     * lambdas/bridges), or 0 when the method has none — as dexlib2 does. */
    int32_t start_line = 0;
    for (uint32_t i = 0; i < m->dbg_count; i++) {
        if (m->dbg[i].kind == SMALI_DBG_LINE) { start_line = m->dbg[i].line; break; }
    }

    buf_write_uleb128(b, (uint32_t)start_line);
    /* dexlib2 lists every parameter of the proto, unnamed ones as NO_INDEX. */
    uint32_t nparams = smali_sig_param_count(m->signature);
    if (nparams < m->param_name_count) nparams = m->param_name_count;
    buf_write_uleb128(b, nparams);
    for (uint32_t pi = 0; pi < nparams; pi++) {
        const char *nm = pi < m->param_name_count ? m->param_names[pi] : NULL;
        write_idx_p1(b, nm ? smali_pool_find(&ctx->strings, nm) : 0xFFFFFFFF);
    }

    uint32_t *off = malloc((m->insns_count + 1) * sizeof(uint32_t));
    if (!off) return 0;
    smali_compute_insn_offsets(m, off);

    dbg_state_t s = {b, start_line, 0};
    for (uint32_t i = 0; i < m->dbg_count; i++) {
        const smali_dbg_event_t *e = &m->dbg[i];
        uint32_t addr = off[e->insn <= m->insns_count ? e->insn : m->insns_count];
        if (addr < s.addr) addr = s.addr;  /* addresses never move backwards */
        switch (e->kind) {
        case SMALI_DBG_LINE:
            emit_line(&s, addr, e->line);
            break;
        case SMALI_DBG_LOCAL:
            advance_pc(&s, addr);
            emit_local(ctx, &s, m, e);
            break;
        case SMALI_DBG_END_LOCAL:
        case SMALI_DBG_RESTART_LOCAL:
            advance_pc(&s, addr);
            buf_write_u8(b, e->kind == SMALI_DBG_END_LOCAL ? 0x05 : 0x06);
            buf_write_uleb128(b, dbg_reg(m, e->reg));
            break;
        case SMALI_DBG_PROLOGUE:
        case SMALI_DBG_EPILOGUE:
            advance_pc(&s, addr);
            buf_write_u8(b, e->kind == SMALI_DBG_PROLOGUE ? 0x07 : 0x08);
            break;
        }
    }
    free(off);

    buf_write_u8(b, 0x00);
    return offset;
}
