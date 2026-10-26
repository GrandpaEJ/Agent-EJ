#include "smali_encoder.h"
#include "smali_pool.h"
#include "smali_error.h"
#include "smali_validate.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static uint32_t get_insn_size_words(smali_insn_t *ins) {
    switch (ins->fmt) {
        case 0: return 1; // F_10x
        case 1: case 21: return 1; // F_12x
        case 2: return 1; // F_11n
        case 3: return 1; // F_11x
        case 4: return 1; // F_10t
        case 5: return 2; // F_20t
        case 6: return 3; // F_30t
        case 7: case 19: return 2; // F_21c
        case 8: return 2; // F_22c
        case 9: case 10: return 2; // F_21t / F_22t
        case 11: return 2; // F_21s
        case 12: return 2; // F_22b
        case 22: return 2; // F_22s
        case 13: return 2; // F_23x
        case 14: return 3; // F_35c
        case 15: return 3; // F_3rc
        case 16: return 3; // F_31i
        case 18: return 3; // F_31t
        case 23: return 2; // F_22x
        case 24: return 3; // F_32x
        case 25: return 3; // F_31c
        case 26: return 5; // F_51l
        case 101: return 4 + ins->payload_targets_count * 2; // packed-switch-payload
        case 102: return 2 + ins->payload_targets_count * 4; // sparse-switch-payload
        case 103: return 2 + (ins->payload_data_len + 1) / 2 + 2; // array-data payload (ident+width+size+data, padded to ushort)
        default: return 1;
    }
}

static uint32_t resolve_register(uint32_t reg, uint32_t registers_count, uint32_t ins_count) {
    if (reg & 0x80000000) {
        uint32_t p_idx = reg & ~0x80000000;
        return registers_count - ins_count + p_idx;
    }
    return reg;
}

void smali_compute_insn_offsets(smali_method_def_t *m, uint32_t *out) {
    uint32_t cur_offset = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].fmt >= 101 && m->insns[i].fmt <= 103 && (cur_offset & 1)) cur_offset++;
        out[i] = cur_offset;
        cur_offset += get_insn_size_words(&m->insns[i]);
    }
    out[m->insns_count] = cur_offset;
}

int smali_encode_method_insns(smali_ctx_def_t *ctx, smali_method_def_t *m, uint16_t *out_buf, uint32_t *out_words) {
    uint32_t *insn_offsets = malloc((m->insns_count + 1) * sizeof(uint32_t));
    smali_compute_insn_offsets(m, insn_offsets);

    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        smali_insn_t *ins = &m->insns[i];
        if (ins->fmt == 101 || ins->fmt == 102) {
            const char *payload_label = NULL;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (m->labels[j].offset == i) {
                    payload_label = m->labels[j].name;
                    break;
                }
            }
            uint32_t switch_ins_off = 0;
            if (payload_label) {
                for (uint32_t j = 0; j < m->insns_count; j++) {
                    if (m->insns[j].label_target && strcmp(m->insns[j].label_target, payload_label) == 0) {
                        switch_ins_off = insn_offsets[j];
                        break;
                    }
                }
            }

            if (ins->fmt == 101) {
                if (write_idx % 2 != 0) {
                    out_buf[write_idx++] = 0x0000;
                }
                out_buf[write_idx++] = 0x0100;
                out_buf[write_idx++] = ins->payload_targets_count;
                out_buf[write_idx++] = ins->lit & 0xFFFF;
                out_buf[write_idx++] = (ins->lit >> 16) & 0xFFFF;
                for (uint32_t t = 0; t < ins->payload_targets_count; t++) {
                    uint32_t target_idx = 0;
                    for (uint32_t j = 0; j < m->labels_count; j++) {
                        if (strcmp(m->labels[j].name, ins->payload_targets[t]) == 0) {
                            target_idx = m->labels[j].offset;
                            break;
                        }
                    }
                    int32_t target_rel_off = (int32_t)insn_offsets[target_idx] - (int32_t)switch_ins_off;
                    out_buf[write_idx++] = target_rel_off & 0xFFFF;
                    out_buf[write_idx++] = (target_rel_off >> 16) & 0xFFFF;
                }
            } else {
                if (write_idx % 2 != 0) {
                    out_buf[write_idx++] = 0x0000;
                }
                out_buf[write_idx++] = 0x0200;
                out_buf[write_idx++] = ins->payload_targets_count;
                for (uint32_t t = 0; t < ins->payload_targets_count; t++) {
                    int32_t key = ins->payload_keys[t];
                    out_buf[write_idx++] = key & 0xFFFF;
                    out_buf[write_idx++] = (key >> 16) & 0xFFFF;
                }
                for (uint32_t t = 0; t < ins->payload_targets_count; t++) {
                    uint32_t target_idx = 0;
                    for (uint32_t j = 0; j < m->labels_count; j++) {
                        if (strcmp(m->labels[j].name, ins->payload_targets[t]) == 0) {
                            target_idx = m->labels[j].offset;
                            break;
                        }
                    }
                    int32_t target_rel_off = (int32_t)insn_offsets[target_idx] - (int32_t)switch_ins_off;
                    out_buf[write_idx++] = target_rel_off & 0xFFFF;
                    out_buf[write_idx++] = (target_rel_off >> 16) & 0xFFFF;
                }
            }
            continue;
        }
        if (ins->fmt == 103) {
            /* array-data payload */
            if (write_idx % 2 != 0) {
                out_buf[write_idx++] = 0x0000;
            }
            out_buf[write_idx++] = 0x0300; /* ident */
            out_buf[write_idx++] = ins->payload_element_width;
            uint32_t count = ins->payload_data_len / ins->payload_element_width;
            out_buf[write_idx++] = count & 0xFFFF;
            out_buf[write_idx++] = (count >> 16) & 0xFFFF;
            for (uint32_t bi2 = 0; bi2 < ins->payload_data_len; bi2++) {
                if (bi2 % 2 == 0) {
                    out_buf[write_idx] = ins->payload_data[bi2];
                } else {
                    out_buf[write_idx++] |= (uint16_t)ins->payload_data[bi2] << 8;
                }
            }
            if (ins->payload_data_len % 2 != 0) write_idx++;
            continue;
        }
        
        if (write_idx != insn_offsets[i]) {
            fprintf(stderr, "Error: encoder offset mismatch at insn %d (fmt %d): write_idx=%u, expected=%u\n", i, ins->fmt, write_idx, insn_offsets[i]);
            write_idx = insn_offsets[i];
        }
        
        uint32_t ins_off = insn_offsets[i];
        uint16_t w0 = ins->op, w1 = 0, w2 = 0, w3 = 0, w4 = 0;

        int32_t rel_off = 0;
        if (ins->label_target) {
            uint32_t target_idx = 0xFFFFFFFF;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (strcmp(m->labels[j].name, ins->label_target) == 0) {
                    target_idx = m->labels[j].offset;
                    break;
                }
            }
            if (target_idx != 0xFFFFFFFF) {
                rel_off = (int32_t)insn_offsets[target_idx] - (int32_t)ins_off;
            }
        }

        // Resolving reference indexes based on kind
        uint32_t ref_idx = 0;
        if (ins->ref_str) {
            if (ins->kind == 1) {
                ref_idx = smali_pool_find(&ctx->strings, ins->ref_str);
            } else if (ins->kind == 2) {
                ref_idx = smali_pool_find(&ctx->types, ins->ref_str);
            } else if (ins->kind == 3) {
                ref_idx = smali_pool_find(&ctx->fields, ins->ref_str);
            } else if (ins->kind == 4) {
                ref_idx = smali_pool_find(&ctx->methods, ins->ref_str);
            }
            if (ref_idx == 0xFFFFFFFF) {
                /* An unresolvable reference ("??") must not silently become
                 * index 0, that would point the instruction at an unrelated
                 * member. Report it and abort the encoding. */
                free(insn_offsets);
                return smali_fail("unresolved %s reference '%s' in %s%s (line %d)",
                        ins->kind == 1 ? "string" : ins->kind == 2 ? "type" :
                        ins->kind == 3 ? "field" : "method",
                        ins->ref_str, m->name ? m->name : "?",
                        m->signature ? m->signature : "", ins->line_number);
            }
        }

        uint32_t vA = resolve_register(ins->vA, m->registers_count, m->ins_count);
        uint32_t vB = resolve_register(ins->vB, m->registers_count, m->ins_count);
        uint32_t vC = resolve_register(ins->vC, m->registers_count, m->ins_count);

        /* F_35c keeps its argument registers in ins->regs[]; resolve them
         * once so the validator and the encoder agree on the values. */
        uint32_t invoke_regs[5];
        if (ins->fmt == 14) {
            for (int r = 0; r < 5; r++)
                invoke_regs[r] = resolve_register(ins->regs[r], m->registers_count, m->ins_count);
        }

        /* Register range check against the DEX bytecode format limits. */
        if (smali_validate_insn(m, ins, vA, vB, vC,
                                ins->fmt == 14 ? invoke_regs : NULL) != 0) {
            free(insn_offsets);
            return -1;
        }

        switch (ins->fmt) {
            case 0: // F_10x
                w0 = ins->op; break;
            case 1: case 21: // F_12x
                w0 = ins->op | (vA << 8) | (vB << 12); break;
            case 2: // F_11n
                w0 = ins->op | (vA << 8) | ((ins->lit & 0xF) << 12); break;
            case 3: // F_11x
                w0 = ins->op | (vA << 8); break;
            case 4: // F_10t
                w0 = ins->op | (((uint8_t)rel_off) << 8); break;
            case 5: // F_20t
                w0 = ins->op; w1 = (uint16_t)rel_off; break;
            case 6: // F_30t
                w0 = ins->op; w1 = (uint16_t)rel_off; w2 = (uint16_t)(rel_off >> 16); break;
            case 7: case 19: // F_21c
                w0 = ins->op | (vA << 8); w1 = (uint16_t)ref_idx; break;
            case 8: // F_22c
                w0 = ins->op | (vA << 8) | (vB << 12); w1 = (uint16_t)ref_idx; break;
            case 9: // F_21t
                w0 = ins->op | (vA << 8); w1 = (uint16_t)rel_off; break;
            case 10: // F_22t
                w0 = ins->op | (vA << 8) | (vB << 12); w1 = (uint16_t)rel_off; break;
            case 11: // F_21s
                w0 = ins->op | (vA << 8);
                if (ins->op == 0x15) w1 = (uint16_t)(ins->lit >> 16);
                else if (ins->op == 0x19) w1 = (uint16_t)(ins->lit >> 48);
                else w1 = (uint16_t)ins->lit;
                break;
            case 12: // F_22b
                w0 = ins->op | (vA << 8); w1 = (vB) | ((ins->lit & 0xFF) << 8); break;
            case 22: // F_22s
                w0 = ins->op | (vA << 8) | (vB << 12); w1 = (uint16_t)ins->lit; break;
            case 13: // F_23x
                w0 = ins->op | (vA << 8); w1 = (vB) | (vC << 8); break;
            case 14: { // F_35c
                uint32_t r0 = invoke_regs[0];
                uint32_t r1 = invoke_regs[1];
                uint32_t r2 = invoke_regs[2];
                uint32_t r3 = invoke_regs[3];
                uint32_t r4 = invoke_regs[4];
                w0 = ins->op | (ins->vA << 12) | ((r4 & 0xF) << 8);
                w1 = (uint16_t)ref_idx;
                w2 = (r0 & 0xF) | ((r1 & 0xF) << 4) | ((r2 & 0xF) << 8) | ((r3 & 0xF) << 12);
                break;
            }
            case 15: // F_3rc
                w0 = ins->op | (ins->vA << 8); w1 = (uint16_t)ref_idx; w2 = (uint16_t)vC; break;
            case 16: // F_31i
                w0 = ins->op | (vA << 8); w1 = (uint16_t)ins->lit; w2 = (uint16_t)(ins->lit >> 16); break;
            case 18: // F_31t
                w0 = ins->op | (vA << 8); w1 = (uint16_t)rel_off; w2 = (uint16_t)(rel_off >> 16); break;
            case 23: // F_22x
                w0 = ins->op | (vA << 8); w1 = (uint16_t)vB; break;
            case 24: // F_32x
                w0 = ins->op; w1 = (uint16_t)vA; w2 = (uint16_t)vB; break;
            case 25: // F_31c
                w0 = ins->op | (vA << 8); w1 = (uint16_t)(ref_idx & 0xFFFF); w2 = (uint16_t)(ref_idx >> 16); break;
            case 26: // F_51l
                w0 = ins->op | (vA << 8);
                w1 = (uint16_t)ins->lit;
                w2 = (uint16_t)(ins->lit >> 16);
                w3 = (uint16_t)(ins->lit >> 32);
                w4 = (uint16_t)(ins->lit >> 48);
                break;
        }
        uint32_t words = get_insn_size_words(ins);
        out_buf[write_idx++] = w0;
        if (words > 1) out_buf[write_idx++] = w1;
        if (words > 2) out_buf[write_idx++] = w2;
        if (words > 3) out_buf[write_idx++] = w3;
        if (words > 4) out_buf[write_idx++] = w4;
    }
    free(insn_offsets);
    *out_words = write_idx;
    return 0;
}
