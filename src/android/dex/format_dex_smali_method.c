#include "format_dex_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int dex_decode_insn(const uint16_t *insns, int offset,
    uint32_t *out_op, uint32_t *out_vA, uint32_t *out_vB, uint32_t *out_vC,
    uint32_t *out_ref, int32_t *out_lit);
extern const char *dex_insn_name(uint8_t op);
extern int dex_insn_kind(uint8_t op);
extern int dex_insn_fmt(uint8_t op);

static void emit_access_comment(smali_sb *s, dex_ctx *ctx, uint32_t method_idx) {
    if (method_idx >= ctx->method_count) return;
    dex_method_id *mi = &ctx->methods[method_idx];
    const char *mname = (mi->name_idx < (uint32_t)ctx->sp.count)
                        ? ctx->sp.strings[mi->name_idx] : NULL;
    if (!mname) return;
    if (strncmp(mname, "access$", 7) != 0) return;
    int is_getter = (mname[7] == 'L');
    int is_invokes = (mname[7] != 'L' && mname[7] != 'S');
    for (uint32_t ci = 0; ci < ctx->class_count; ci++) {
        dex_class *cls = &ctx->classes[ci];
        for (int di = 0; di < cls->direct_count; di++) {
            if (cls->direct[di].method_idx == method_idx) {
                uint32_t code_off = cls->direct[di].code_off;
                if (code_off == 0) return;
                if (code_off + 16 > ctx->size) return;
                const uint8_t *raw = ctx->data + code_off;
                if (code_off + 16 + (raw[12] | (raw[13]<<8) | (raw[14]<<16) | (raw[15]<<24)) * 2 > ctx->size) return;
                uint32_t isz = (uint32_t)raw[12] | ((uint32_t)raw[13]<<8) | ((uint32_t)raw[14]<<16) | ((uint32_t)raw[15]<<24);
                const uint16_t *insns2 = (const uint16_t *)(raw + 16);
                for (int ii = 0; (uint32_t)ii < isz; ) {
                    uint32_t op2, vA2, vB2, vC2, rf2; int32_t lt2;
                    int w2 = dex_decode_insn(insns2, ii, &op2, &vA2, &vB2, &vC2, &rf2, &lt2);
                    if (w2 <= 0) { ii++; continue; }
                    int kind2 = dex_insn_kind((uint8_t)op2);
                    if (kind2 == 3 || kind2 == 4) {
                        const char *target = smali_res(ctx, kind2, rf2);
                        if (target) {
                            if (is_getter) smali_sf(s, "    # getter for: %s\n", target);
                            else if (is_invokes) smali_sf(s, "    # invokes: %s\n", target);
                        }
                        return;
                    }
                    ii += w2;
                }
                return;
            }
        }
    }
}

#define MAX_CATCH 8
#define DBG_LINE_BASE (-4)
#define DBG_LINE_RANGE 15

/* Signed hex literal as baksmali prints it; wide adds the L suffix. */
static void smali_lit(smali_sb *s, int64_t v, int wide) {
    if (v < 0) smali_sf(s, "-0x%llx%s", (unsigned long long)(0 - (uint64_t)v), wide ? "L" : "");
    else smali_sf(s, "0x%llx%s", (unsigned long long)v, wide ? "L" : "");
}

typedef struct {
    uint32_t start_addr;
    uint32_t insn_count;
    uint32_t catch_all_addr;
    uint32_t catch_type_count;
    uint32_t catch_types[MAX_CATCH];
    uint32_t catch_addrs[MAX_CATCH];
} try_block_t;

typedef struct {
    uint32_t addr;
    int line;
} line_entry_t;

static int emit_labels_at(smali_sb *s, uint32_t i, const uint32_t *lo,
                            const uint8_t *lkinds, int lc, try_block_t *blocks,
                            int block_count, dex_ctx *ctx) {
    /* emit branch target labels */
    for (int li = 0; li < lc; li++) {
        if (lo[li] == i) {
            if (lkinds[li] & 1)
                smali_sf(s, "    :cond_%x\n", i);
            if (lkinds[li] & 2)
                smali_sf(s, "    :goto_%x\n", i);
            if ((lkinds[li] & 128) || ((lkinds[li] & 4) && !(lkinds[li] & 64))) {
                /* a handler can serve typed catches and a catch-all at once */
                smali_sf(s, "    :catch_%x\n", i);
            }
            if (lkinds[li] & 64)
                smali_sf(s, "    :catchall_%x\n", i);
            if (lkinds[li] & 8)
                smali_sf(s, "    :pswitch_%x\n", i);
            if (lkinds[li] & 16)
                smali_sf(s, "    :sswitch_%x\n", i);
            if (lkinds[li] & 32)
                smali_sf(s, "    :try_start_%x\n", i);
        }
    }
    return 0;
}

static int emit_try_end_at(smali_sb *s, uint32_t i, try_block_t *blocks,
                            int block_count, dex_ctx *ctx) {
    int had_catch = 0;
    for (int bi = 0; bi < block_count; bi++) {
        uint32_t end_off = blocks[bi].start_addr + blocks[bi].insn_count;
        if (end_off == i) {
            smali_sf(s, "    :try_end_%x\n", i);
            had_catch = 1;
            for (uint32_t ci = 0; ci < blocks[bi].catch_type_count; ci++) {
                if (blocks[bi].catch_types[ci] < (uint32_t)ctx->type_count) {
                    const char *etype = ctx->sp.strings[ctx->type_ids[blocks[bi].catch_types[ci]]];
                    char *et = smali_tsm(etype);
                    smali_sf(s, "    .catch %s {:try_start_%x .. :try_end_%x} :catch_%x\n",
                        et ? et : etype, blocks[bi].start_addr, end_off, blocks[bi].catch_addrs[ci]);
                    free(et);
                }
            }
            if (blocks[bi].catch_all_addr != 0xFFFFFFFF) {
                smali_sf(s, "    .catchall {:try_start_%x .. :try_end_%x} :catchall_%x\n",
                    blocks[bi].start_addr, end_off, blocks[bi].catch_all_addr);
            }
        }
    }
    return had_catch;
}

int smali_dm(smali_sb *s, dex_ctx *ctx, dex_method_enc *me) {
    if (me->method_idx >= ctx->method_count)
        return smali_sf(s, ".method unknown\n.end method\n");

    dex_method_id *mi = &ctx->methods[me->method_idx];
    uint32_t fl = me->access_flags;
    const char *mn = mi->name_idx < (uint32_t)ctx->sp.count
                     ? ctx->sp.strings[mi->name_idx] : "??";
    static char protobuf[65536 + 8];
    smali_mproto(ctx, me->method_idx, protobuf, sizeof(protobuf));

    if (me->code_off == 0 || me->code_off + 16 > ctx->size) {
        smali_sf(s, ".method %s %s%s\n.end method\n",
                 smali_aflags(fl), mn, protobuf);
        return 0;
    }

    const uint8_t *p = ctx->data + me->code_off;
    uint16_t regs, ins, tries; uint32_t isz, dbg_off;
    memcpy(&regs, p, 2); memcpy(&ins, p+2, 2);
    memcpy(&tries, p+6, 2); memcpy(&dbg_off, p+8, 4);
    memcpy(&isz, p+12, 4);
    const uint16_t *insns = (const uint16_t *)(p + 16);

    /* method signature */
    const char *aflg = smali_aflags(fl);
    if (aflg[0])
        smali_sf(s, ".method %s %s%s\n", aflg, mn, protobuf);
    else
        smali_sf(s, ".method %s%s\n", mn, protobuf);
    smali_sf(s, "    .registers %u\n", (unsigned)regs);

    /* find class for annotation lookup */
    uint32_t class_idx = 0xFFFFFFFF;
    for (uint32_t ci2 = 0; ci2 < ctx->class_count; ci2++) {
        for (int di = 0; di < ctx->classes[ci2].direct_count; di++) {
            if (&ctx->classes[ci2].direct[di] == me) {
                class_idx = ci2;
                break;
            }
        }
        if (class_idx != 0xFFFFFFFF) break;
        for (int vi = 0; vi < ctx->classes[ci2].virtual_count; vi++) {
            if (&ctx->classes[ci2].virtual[vi] == me) {
                class_idx = ci2;
                break;
            }
        }
        if (class_idx != 0xFFFFFFFF) break;
    }

    /* write method-level annotations */
    if (class_idx != 0xFFFFFFFF) {
        smali_write_method_annot(s, ctx, &ctx->classes[class_idx], me->method_idx);
    }

    /* parse debug info for .line entries and .param names */
    /* label/line tables are sized from the code length so big obfuscated methods never overflow */
    const uint32_t nl = isz + 8;
    line_entry_t *lines = malloc(nl * sizeof(line_entry_t)); int line_count = 0;
    uint32_t *lo = malloc(nl * sizeof(uint32_t)); int lc = 0;
    uint8_t *lkinds = calloc(nl, 1);
    try_block_t *blocks = malloc((tries ? tries : 1) * sizeof(try_block_t));
    if (!lines || !lo || !lkinds || !blocks) { free(lines); free(lo); free(lkinds); free(blocks); return -1; }
    int has_prologue = 0;
    char *param_names[256]; int param_name_count = 0;
    memset(param_names, 0, sizeof(param_names));

    if (dbg_off != 0 && dbg_off < ctx->size - 1) {
        const uint8_t *dp = ctx->data + dbg_off;
        const uint8_t *dend = ctx->data + ctx->size;

        /* initial line is a uleb128 */
        /* actually: line_start is uleb128, parameters_size is uleb128 */
        uint32_t start_line = 0, params_sz = 0;
        smali_uleb(dp, &start_line, &dp);
        smali_uleb(dp, &params_sz, &dp);
        (void)start_line;
        /* param names: uleb128 string_id indices */
        for (uint32_t pi = 0; pi < params_sz && pi < 256; pi++) {
            uint32_t name_idx;
            smali_uleb(dp, &name_idx, &dp);
            if (name_idx < (uint32_t)ctx->sp.count && ctx->sp.strings[name_idx]) {
                param_names[pi] = strdup(ctx->sp.strings[name_idx]);
            }
            param_name_count++;
        }

        /* Parse bytecode until DBG_END_SEQUENCE */
        int cur_line = (int)start_line;
        uint32_t cur_addr = 0;

        while (dp < dend - 1) {
            uint8_t opcode = *dp++;
            if (opcode == 0x00) break; // DBG_END_SEQUENCE

            if (opcode >= 0x0A) {
                /* Special opcode */
                uint8_t adj = opcode - 0x0A;
                cur_addr += (uint32_t)(adj / DBG_LINE_RANGE);
                cur_line += DBG_LINE_BASE + (adj % DBG_LINE_RANGE);
                /* Record line entry for this address */
                if (line_count < nl && cur_addr < isz) {
                    lines[line_count].addr = cur_addr;
                    lines[line_count].line = cur_line;
                    line_count++;
                }
            } else {
                switch (opcode) {
                case 0x01: { uint32_t diff; smali_uleb(dp, &diff, &dp); cur_addr += diff; break; }
                case 0x02: { int32_t diff; smali_sleb(dp, &diff, &dp); cur_line += diff; break; }
                case 0x03: { uint32_t r, n, t; smali_uleb(dp, &r, &dp); smali_uleb(dp, &n, &dp); smali_uleb(dp, &t, &dp); break; }
                case 0x04: { uint32_t r, n, t, si; smali_uleb(dp, &r, &dp); smali_uleb(dp, &n, &dp); smali_uleb(dp, &t, &dp); smali_uleb(dp, &si, &dp); break; }
                case 0x05: { uint32_t r; smali_uleb(dp, &r, &dp); break; }
                case 0x06: { uint32_t r; smali_uleb(dp, &r, &dp); break; }
                case 0x07: has_prologue = 1; break;
                case 0x08: break; /* epilogue - just skip */
                case 0x09: { uint32_t n; smali_uleb(dp, &n, &dp); break; }
                default: break;
                }
            }
        }
    }

    /* emit .param directives */
    if (param_name_count > 0) {
        smali_sa(s, "\n");
        for (int pi = 0; pi < param_name_count; pi++) {
            if (param_names[pi]) {
                uint32_t locals = (uint32_t)regs - (uint32_t)ins;
                /* The debug-info name is arbitrary MUTF-8 and may itself
                 * contain '"' or '\\' (seen in the wild from obfuscators
                 * salting debug names to break naive disassemblers) —
                 * escape it the same way every other string is escaped. */
                smali_sf(s, "    .param p%d, \"%s\"\n", pi, smali_escape_str(param_names[pi]));
                (void)locals;
                free(param_names[pi]);
            }
        }
    }

    /* sorted line entries by address for interpolation */
    for (int i = 0; i < line_count; i++) {
        for (int j = i + 1; j < line_count; j++) {
            if (lines[j].addr < lines[i].addr) {
                line_entry_t t = lines[i]; lines[i] = lines[j]; lines[j] = t;
            }
        }
    }

    /* try/catch blocks */
    int block_count = 0;
    if (tries > 0) {
        uint32_t insns_end = 16 + isz * 2;
        uint32_t pad = (insns_end % 4) ? (4 - insns_end % 4) : 0;
        const uint8_t *try_start = p + 16 + isz * 2 + pad;
        const uint8_t *handler_start = try_start + (uint32_t)tries * 8;
        const uint8_t *end = ctx->data + ctx->size;

        for (uint32_t ti = 0; ti < tries && try_start + 8 <= end; ti++) {
            uint32_t start_addr; memcpy(&start_addr, try_start + ti*8, 4);
            uint16_t insn_count; memcpy(&insn_count, try_start + ti*8 + 4, 2);
            uint16_t handler_off; memcpy(&handler_off, try_start + ti*8 + 6, 2);
            const uint8_t *hp = handler_start + handler_off;
            if (hp + 1 > end) continue;

            try_block_t *tb = &blocks[block_count++];
            memset(tb, 0, sizeof(try_block_t));
            tb->start_addr = start_addr;
            tb->insn_count = insn_count;
            tb->catch_all_addr = 0xFFFFFFFF;

            int32_t size; smali_sleb(hp, &size, &hp);
            uint32_t count = (uint32_t)(size < 0 ? -size : size);
            tb->catch_type_count = count > MAX_CATCH ? MAX_CATCH : count;

            for (uint32_t ci = 0; ci < count; ci++) {
                uint32_t type_idx; smali_uleb(hp, &type_idx, &hp);
                uint32_t addr; smali_uleb(hp, &addr, &hp);
                if (ci < MAX_CATCH) { tb->catch_types[ci] = type_idx; tb->catch_addrs[ci] = addr; }
            }
            if (size <= 0) {
                smali_uleb(hp, &tb->catch_all_addr, &hp);
            }
        }
    }

    /* pass 1: collect branch targets and payload offsets */
    /* also collect payload targets from packed/sparse-switch/fill-array-data */
    for (int i2 = 0; (uint32_t)i2 < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt_val;
        int w = dex_decode_insn(insns, i2, &op, &vA, &vB, &vC, &rf, &lt_val);
        if (w <= 0) { i2 += 1; continue; }
        int fm = dex_insn_fmt((uint8_t)op);
        int bo = (fm==4||fm==5||fm==6) ? lt_val :
                 ((fm==9||fm==10) ? (int32_t)(int16_t)rf :
                 (fm==18 ? (int32_t)rf : 0));
        if (bo) {
            int t = i2 + bo;
            if (t >= 0 && (uint32_t)t < isz && lc < nl) {
                int dup = 0;
                for (int di = 0; di < lc; di++)
                    if (lo[di] == (uint32_t)t) { dup = 1; break; }
                if (!dup) lo[lc++] = (uint32_t)t;
            }
        }
        /* collect switch payload targets */
        if (fm == 18 && rf > 0) {
            uint32_t toff = (uint32_t)i2 + (uint32_t)rf;
            if (toff + 2 < isz) {
                const uint16_t *payload = insns + toff;
                uint16_t ident = payload[0];
                if (ident == 0x0100) {
                    uint16_t psize = payload[1];
                    for (int si = 0; si < (int)psize; si++) {
                        int32_t ptarget = (int32_t)(payload[4 + si*2] | (payload[5 + si*2] << 16));
                        uint32_t abs_t = (uint32_t)((int32_t)i2 + ptarget);
                        if (lc < nl) {
                            int dup3 = 0;
                            for (int di = 0; di < lc; di++)
                                if (lo[di] == abs_t) { lkinds[di] |= 8; dup3 = 1; break; }
                            if (!dup3) { lo[lc] = abs_t; lkinds[lc] = 8; lc++; }
                         }
                     }
} else if (ident == 0x0200) {
                     uint16_t psize = payload[1];
                     for (int si = 0; si < (int)psize; si++) {
                         int32_t ptarget = (int32_t)(payload[2 + (int)psize*2 + si*2] | (payload[3 + (int)psize*2 + si*2] << 16));
                         uint32_t abs_t = (uint32_t)((int32_t)i2 + ptarget);
                         if (lc < nl) {
                             int dup3 = 0;
                             for (int di = 0; di < lc; di++)
                                 if (lo[di] == abs_t) { lkinds[di] |= 16; dup3 = 1; break; }
                             if (!dup3) { lo[lc] = abs_t; lkinds[lc] = 16; lc++; }
                         }
                     }
                 }
            }
        }
        /* also add try handler targets */
        for (int bi2 = 0; bi2 < block_count; bi2++) {
            uint32_t end_off = blocks[bi2].start_addr + blocks[bi2].insn_count;
            for (uint32_t ci = 0; ci < blocks[bi2].catch_type_count; ci++) {
                uint32_t h = blocks[bi2].catch_addrs[ci];
                int dup2 = 0;
                for (int di = 0; di < lc; di++)
                    if (lo[di] == h) { lkinds[di] |= 4 | 128; dup2 = 1; break; }
                if (!dup2 && lc < nl) { lo[lc] = h; lkinds[lc] = 4 | 128; lc++; }
            }
            if (blocks[bi2].catch_all_addr != 0xFFFFFFFF && blocks[bi2].catch_all_addr < isz) {
                int dup2 = 0;
                for (int di = 0; di < lc; di++)
                    if (lo[di] == blocks[bi2].catch_all_addr) { lkinds[di] |= 64; dup2 = 1; break; }
                if (!dup2 && lc < nl) { lo[lc] = blocks[bi2].catch_all_addr; lkinds[lc] = 64; lc++; }
            }
            /* end_off for try_end label */
            if (end_off < isz) {
                int dup2 = 0;
                for (int di = 0; di < lc; di++)
                    if (lo[di] == end_off) { dup2 = 1; break; }
                if (!dup2 && lc < nl) lo[lc++] = end_off;
            }
        }
        i2 += w;
    }

    /* pass 2: determine label kinds */
    for (int li = 0; li < lc; li++) lkinds[li] &= ~3;
    for (int bi = 0; (uint32_t)bi < isz; ) {
        uint32_t bop, bvA, bvB, bvC, brf; int32_t blt;
        int bw = dex_decode_insn(insns, bi, &bop, &bvA, &bvB, &bvC, &brf, &blt);
        if (bw <= 0) { bi++; continue; }
        int bfm = dex_insn_fmt((uint8_t)bop);
        int bbo = (bfm==4||bfm==5||bfm==6) ? blt :
                  ((bfm==9||bfm==10) ? (int32_t)(int16_t)brf :
                  (bfm==18 ? (int32_t)brf : 0));
        if (bbo) {
            int t = bi + bbo;
            for (int li = 0; li < lc; li++) {
                if ((int)lo[li] == t) {
                    const char *bnm = dex_insn_name((uint8_t)bop);
                    if (bnm && strncmp(bnm, "goto", 4) == 0)
                        lkinds[li] |= 2;
                    else
                        lkinds[li] |= 1;
                }
            }
        }
        bi += bw;
    }

    /* mark handler labels as catch targets */
    for (int bi2 = 0; bi2 < block_count; bi2++) {
        for (uint32_t ci = 0; ci < blocks[bi2].catch_type_count; ci++) {
            for (int li = 0; li < lc; li++) {
                if (lo[li] == blocks[bi2].catch_addrs[ci])
                    lkinds[li] |= 4 | 128;
            }
        }
        if (blocks[bi2].catch_all_addr != 0xFFFFFFFF) {
            for (int li = 0; li < lc; li++) {
                if (lo[li] == blocks[bi2].catch_all_addr)
                    lkinds[li] |= 4;
            }
        }
        /* also add try_start labels */
        if (blocks[bi2].start_addr < isz) {
            int dup2 = 0;
            for (int li = 0; li < lc; li++) {
                if (lo[li] == blocks[bi2].start_addr) { lkinds[li] |= 32; dup2 = 1; break; }
            }
            if (!dup2 && lc < nl) { lo[lc] = blocks[bi2].start_addr; lkinds[lc] = 32; lc++; }
        }
    }

    smali_sa(s, "\n");

    if (has_prologue)
        smali_sa(s, "    .prologue\n");

    /* Collect payload offsets for packed-switch, sparse-switch, fill-array-data */
    uint32_t max_payloads = isz / 3 + 1;
    uint32_t *payload_offs = malloc(max_payloads * 2 * sizeof(uint32_t));
    if (!payload_offs) { free(lines); free(lo); free(lkinds); free(blocks); return -1; }
    uint32_t *payload_srcs = payload_offs + max_payloads;
    int payload_count = 0;
    for (int i2 = 0; (uint32_t)i2 < isz; ) {
        uint32_t op2, vA2, vB2, vC2, rf2; int32_t lt2;
        int w2 = dex_decode_insn(insns, i2, &op2, &vA2, &vB2, &vC2, &rf2, &lt2);
        if (w2 <= 0) { i2 += 1; continue; }
        int fm2 = dex_insn_fmt((uint8_t)op2);
        if (fm2 == 18) {
            uint32_t toff = (uint32_t)i2 + (uint32_t)rf2;
            if (toff + 4 <= isz && rf2 > 0 && (uint32_t)payload_count < max_payloads) {
                payload_offs[payload_count] = toff;
                payload_srcs[payload_count] = i2;
                payload_count++;
            }
        }
        i2 += w2;
    }

    /* pass 3: emit instructions with labels, payloads, and line numbers */
    for (int i = 0; (uint32_t)i < isz; ) {
        /* check if current offset is a payload */
        int is_payload = 0;
        for (int pi = 0; pi < payload_count; pi++) {
            if (payload_offs[pi] == (uint32_t)i) {
                is_payload = 1;
                const uint16_t *payload = insns + i;
                uint16_t ident = payload[0];
                if (ident == 0x0100) {
                    uint16_t size = payload[1];
                    int32_t first_key = (int32_t)(payload[2] | (payload[3] << 16));
                    smali_sf(s, "    :pswitch_data_%x\n", i);
                    smali_sf(s, "    .packed-switch 0x%x\n", (uint32_t)first_key);
                    int switch_off = (int)payload_srcs[pi];
                    for (int si = 0; si < (int)size; si++) {
                        int32_t ptarget = (int32_t)(payload[4 + si*2] | (payload[5 + si*2] << 16));
                        int abs_target = switch_off + ptarget;
                        smali_sf(s, "        :pswitch_%x\n", (uint32_t)abs_target);
                    }
                    smali_sa(s, "    .end packed-switch\n");
                    uint32_t pw = 4 + (uint32_t)size * 2;
                    for (uint32_t ppi = 0; ppi < pw; ppi++) {
                        i += 1;
                    }
                    goto next_insn;
                } else if (ident == 0x0200) {
                    uint16_t size = payload[1];
                    smali_sf(s, "    :sswitch_data_%x\n", i);
                    smali_sa(s, "    .sparse-switch\n");
                    int switch_off = (int)payload_srcs[pi];
                    for (int si = 0; si < (int)size; si++) {
                        int32_t key = (int32_t)(payload[2 + si*2] | (payload[3 + si*2] << 16));
                        int32_t ptarget = (int32_t)(payload[2 + (int)size*2 + si*2] | (payload[3 + (int)size*2 + si*2] << 16));
                        int abs_target = switch_off + ptarget;
                        smali_sf(s, "        0x%x -> :sswitch_%x\n", (uint32_t)key, (uint32_t)abs_target);
                    }
                    smali_sa(s, "    .end sparse-switch\n");
                    uint32_t pw = 2 + (uint32_t)size * 4;
                    for (uint32_t ppi = 0; ppi < pw; ppi++) {
                        i += 1;
                    }
                    goto next_insn;
                } else if (ident == 0x0300) {
                    uint16_t elem_width = payload[1];
                    uint32_t count = payload[2] | (payload[3] << 16);
                    smali_sf(s, "    :array_%x\n", i);
                    smali_sf(s, "    .array-data %u\n", (unsigned)elem_width);
                    const uint8_t *array_data = (const uint8_t *)(payload + 4);
                    for (uint32_t ai = 0; ai < count; ai++) {
                        int32_t val = 0;
                        if (elem_width == 1) {
                            val = (int32_t)(int8_t)array_data[ai];
                        } else if (elem_width == 2) {
                            val = (int32_t)(int16_t)(array_data[ai*2] | (array_data[ai*2+1] << 8));
                        } else if (elem_width == 4) {
                            val = (int32_t)(array_data[ai*4] | (array_data[ai*4+1] << 8) | (array_data[ai*4+2] << 16) | (array_data[ai*4+3] << 24));
                        } else if (elem_width == 8) {
                            int64_t v64 = 0;
                            for (int ebi = 0; ebi < 8; ebi++)
                                v64 |= (int64_t)array_data[ai*8+ebi] << (ebi*8);
                            smali_sf(s, "        0x%llxL\n", (unsigned long long)v64);
                            continue;
                        }
                        smali_sf(s, "        0x%x\n", val);
                    }
                    smali_sa(s, "    .end array-data\n");
                    /* advance past the payload data (pad to 4-byte alignment, then 2 code units for header) */
                    uint32_t data_bytes = count * elem_width;
                    uint32_t pw = 4 + (data_bytes + 1) / 2;
                    for (uint32_t ppi = 0; ppi < pw; ppi++) {
                        i += 1;
                    }
                    goto next_insn;
                }
            }
        }

uint32_t op, vA, vB, vC, rf; int32_t lt_val;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt_val);
        if (w <= 0) { i += 1; continue; }
        const char *nm = dex_insn_name((uint8_t)op);
        if (!nm) { i += w; continue; }
        int k = dex_insn_kind((uint8_t)op), fm = dex_insn_fmt((uint8_t)op);

        /* emit .line before instruction if we have debug info */
        for (int li2 = 0; li2 < line_count; li2++) {
            if (lines[li2].addr == (uint32_t)i) {
                smali_sf(s, "    .line %d\n", lines[li2].line);
            }
        }

        /* emit labels at this offset (branch targets and try_start only) */
        emit_labels_at(s, (uint32_t)i, lo, lkinds, lc, blocks, block_count, ctx);

        /* emit instruction text (payloads are handled above at their natural offsets) */
        /* emit access bridge comment before invoke-static access$ calls */
        if (op == 0x71 || op == 0x77) {
            if (k == 4) emit_access_comment(s, ctx, rf);
        }
        smali_sa(s, "    "); smali_sa(s, nm);

        #define RN(r) smali_reg_name(r, (uint32_t)regs, (uint32_t)ins)
        switch (fm) {
        case 0: break;
        case 1: case 21: smali_sf(s, " %s, %s", RN(vA), RN(vB)); break;
        case 2: if (lt_val < 0) smali_sf(s, " %s, -0x%x", RN(vA), -lt_val);
            else smali_sf(s, " %s, 0x%x", RN(vA), lt_val); break;
        case 3: smali_sf(s, " %s", RN(vA)); break;
        case 4: case 5: case 6: smali_sf(s, " :goto_%x", i+lt_val); break;
        case 7:
            if (k==1) smali_sf(s," %s, \"%s\"", RN(vA), smali_res(ctx,k,rf));
            else if (k>=2) { const char *r=smali_res(ctx,k,rf);
                if (k==2){char*tt=smali_tsm(r);smali_sf(s," %s, %s",RN(vA),tt);free(tt);}
                else smali_sf(s," %s, %s",RN(vA),r); }
            else smali_sf(s, " %s, 0x%x", RN(vA), rf);
            break;
case 19:
            if (op == 0x19) {
                smali_sf(s, " %s, ", RN(vA));
                smali_lit(s, (int64_t)((uint64_t)rf << 48), 1);
            } else {
                uint32_t v32 = (uint32_t)rf << 16;
                smali_sf(s, " %s, 0x%x", RN(vA), v32);
                if (op == 0x15 && v32 != 0 && v32 != 0x7fc00000u) {
                    float fv; uint32_t fv32 = v32; memcpy(&fv, &fv32, 4);
                    int fexp = (fv32 >> 23) & 0xFF;
                    if (isfinite(fv) && !isnan(fv) && fexp > 0 && fexp < 254) {
                        char fbuf[64];
                        snprintf(fbuf, sizeof(fbuf), "%.8g", fv);
                        if (!strchr(fbuf, '.') && !strchr(fbuf, 'e') && !strchr(fbuf, 'E'))
                            smali_sf(s, "    # %s.0f", fbuf);
                        else
                            smali_sf(s, "    # %sf", fbuf);
                    }
                }
            }
            break;
        case 8:
            if (k>=2) { const char *r=smali_res(ctx,k,rf);
                if(k==2){char*tt=smali_tsm(r);
                    smali_sf(s," %s, %s, %s",RN(vA),RN(vB),tt);free(tt);}
                else smali_sf(s," %s, %s, %s",RN(vA),RN(vB),r); }
            else smali_sf(s, " %s, %s, 0x%x", RN(vA), RN(vB), rf);
            break;
        case 9: {
            int br=(int32_t)(int16_t)rf;
            smali_sf(s," %s, :cond_%x", RN(vA), i+br); break; }
        case 10: {
            int br=(int32_t)(int16_t)rf;
            smali_sf(s," %s, %s, :cond_%x", RN(vA), RN(vB), i+br); break; }
        case 11: smali_sf(s," %s, ", RN(vA)); smali_lit(s, (int16_t)rf, op == 0x16); break;
        case 20: { int16_t v22s = (int16_t)rf; if (v22s < 0) smali_sf(s," %s, %s, -0x%x", RN(vA), RN(vB), -(int32_t)v22s);
                else smali_sf(s," %s, %s, 0x%x", RN(vA), RN(vB), (uint16_t)v22s); break; }
        case 24: smali_sf(s," %s, %s", RN(vA), RN(vB)); break;
        case 25: smali_sf(s," %s, %s", RN(vA), RN(vB)); break;
        case 12: if (lt_val < 0) smali_sf(s," %s, %s, -0x%x", RN(vA), RN(vB), -lt_val);
                else smali_sf(s," %s, %s, 0x%x", RN(vA), RN(vB), (uint32_t)lt_val); break;
        case 13: smali_sf(s," %s, %s, %s", RN(vA), RN(vB), RN(vC)); break;
        case 14: {
            const char *r=smali_res(ctx,k,rf); smali_sa(s," {");
            uint32_t r5[5]={vB&0xF, (vB>>4)&0xF, (uint32_t)lt_val&0xF,
                            ((uint32_t)lt_val>>4)&0xF, vC};
            for(uint32_t ai=0; ai<vA && ai<5; ai++){
                if(ai)smali_sa(s,", ");
                smali_sf(s,"%s", RN(r5[ai]));
            }
            smali_sf(s, "}, %s", r); break;
        }
        case 15: smali_sf(s," {%s .. %s}, %s", RN(vC), RN(vC+vA-1),
                          smali_res(ctx,k,rf)); break;
        case 16: if (op == 0x17) { smali_sf(s," %s, ", RN(vA)); smali_lit(s, (int32_t)lt_val, 1); }
            else if ((int32_t)lt_val < 0) smali_sf(s," %s, -0x%x", RN(vA), (uint32_t)0 - (uint32_t)lt_val);
            else {
                smali_sf(s," %s, 0x%x", RN(vA), (uint32_t)lt_val);
                if (op == 0x14 && (uint32_t)lt_val != 0) {
                    float fv; uint32_t fv32 = (uint32_t)lt_val; memcpy(&fv, &fv32, 4);
                    int fexp = (fv32 >> 23) & 0xFF;
                    if (isfinite(fv) && !isnan(fv) && fexp > 0 && fexp < 254 && fv >= 1.0f && fv < 1e7f) {
                        char fbuf[64];
                        snprintf(fbuf, sizeof(fbuf), "%.8g", fv);
                        smali_sf(s, "    # %sf", fbuf);
                    }
                }
            }
            break;
        case 17: {
            uint64_t v=0; for(int wi=0;wi<4;wi++)
                v|=(uint64_t)insns[i+1+wi]<<(wi*16);
            smali_sf(s," %s, ",RN(vA)); smali_lit(s, (int64_t)v, 1); break;
        }
        case 18: {
            uint32_t toff = (uint32_t)i + rf;
            if (op == 0x2c)
                smali_sf(s, " %s, :sswitch_data_%x", RN(vA), toff);
            else if (op == 0x2b)
                smali_sf(s, " %s, :pswitch_data_%x", RN(vA), toff);
            else
                smali_sf(s, " %s, :array_%x", RN(vA), toff);
            break;
        }
        default: smali_sf(s," %s", RN(vA)); break;
        }

int is_last = ((uint32_t)(i + w) >= isz) ? 1 : 0;
        smali_sa(s, "\n");
        /* emit try_end/catch at offsets within this instruction's range */
        int had_catch = 0;
        for (int off = i + 1; off <= i + w && (uint32_t)off <= isz; off++) {
            int mc = emit_try_end_at(s, (uint32_t)off, blocks, block_count, ctx);
            if (mc) had_catch = 1;
        }
        if (had_catch) smali_sa(s, "\n");
        else if (!is_last) smali_sa(s, "\n");
        i += w;
        next_insn:;
    }
    free(payload_offs); free(lines); free(lo); free(lkinds); free(blocks);
    return smali_sa(s, ".end method\n");
}// Debug: temporarily add to trace
