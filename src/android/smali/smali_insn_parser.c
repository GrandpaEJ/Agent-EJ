#include "smali_parser.h"
#include "smali_lexer.h"
#include "smali_optab.h"
#include "smali_error.h"
#include "smali_annot.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int smali_parse_method_body(smali_ctx_def_t *ctx, smali_method_def_t *m, char **p, smali_line_t *file_lc) {
    (void)ctx;
    char *start = *p;
    int current_line = -1;  /* track current .line for next instruction */
    /* Source line of the instruction being parsed. The body is counted from
     * its own start so that a rewind inside the body stays cheap; when the
     * body is done the file counter is moved to the end without rescanning. */
    smali_line_t lc;
    smali_line_init(&lc, start, file_lc->line);
    while (*start) {
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
        if (!*start) break;
        
        char *line_end = smali_eol(start);
        if (!line_end) line_end = start + strlen(start);

        /* The token starts here, so this is its source line. */
        smali_line_sync(&lc, start);
        
        if (*start == '#') {
            start = line_end;
            continue;
        }
        
        char *tok = smali_next_token(&start);
        if (!tok) {
            start = line_end;
            continue;
        }
        
        if (strcmp(tok, ".end") == 0) {
            char *next = smali_next_token(&start);
            if (next && strcmp(next, "method") == 0) {
                free(next); free(tok);
                *p = line_end;
                /* The body has been counted; move the file counter to the end
                 * of the method without scanning the body a second time. */
                smali_line_jump(file_lc, *p, lc.line);
                return 0;
            }
            if (next) free(next);
        } else if (strcmp(tok, ".line") == 0) {
            char *line_tok = smali_next_token(&start);
            if (line_tok) {
                current_line = atoi(line_tok);
                free(line_tok);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".param") == 0) {
            char *reg_tok = smali_next_token(&start);
            char *name_tok = smali_next_token(&start);
            if (reg_tok && name_tok) {
                /* ".param pN, ..." names the Nth register, not the Nth
                 * named parameter: a leading parameter with no debug name
                 * is simply skipped in the source. The debug_info table on
                 * disk is positional (one uleb128p1 per "in" register, with
                 * 0 = NO_INDEX for an unnamed one), so the slot this name
                 * belongs in is (register number - first "in" register),
                 * not the order .param directives happen to appear in. */
                uint32_t first_in = m->registers_count - m->ins_count;
                char *digits = reg_tok + 1; /* skip 'p' */
                uint32_t reg_num = (uint32_t)atoi(digits);
                uint32_t idx = reg_num >= first_in ? reg_num - first_in : 0;
                if (idx >= m->param_name_count) {
                    uint32_t old_count = m->param_name_count;
                    uint32_t new_count = idx + 1;
                    m->param_names = realloc(m->param_names, new_count * sizeof(char *));
                    m->param_annots = realloc(m->param_annots, new_count * sizeof(smali_annotation_t*));
                    m->param_annot_counts = realloc(m->param_annot_counts, new_count * sizeof(uint32_t));
                    for (uint32_t z = old_count; z < new_count; z++) {
                        m->param_names[z] = NULL;
                        m->param_annots[z] = NULL;
                        m->param_annot_counts[z] = 0;
                    }
                    m->param_name_count = new_count;
                }
                free(m->param_names[idx]);
                m->param_names[idx] = strdup(name_tok);
                free(name_tok);
                free(reg_tok);
            } else {
                if (reg_tok) free(reg_tok);
                if (name_tok) free(name_tok);
            }
            free(tok);
            
            /* Parse optional param body for annotations */
            while (*start) {
                char *save_p = start;
                while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
                if (!*start) break;
                if (*start == '#') { char *nl = smali_eol(start); start = nl ? nl + 1 : start + strlen(start); continue; }
                char *inner_tok = smali_next_token(&start);
                if (!inner_tok) break;
                if (strcmp(inner_tok, ".end") == 0) {
                    char *sub = smali_next_token(&start);
                    if (sub && strcmp(sub, "param") == 0) {
                        free(sub); free(inner_tok); break;
                    }
                    if (sub) free(sub);
                    start = save_p; free(inner_tok); break;
                }
                if (strcmp(inner_tok, ".annotation") == 0 && m->param_name_count > 0) {
                    uint32_t pidx = m->param_name_count - 1;
                    if (m->param_annot_counts[pidx] < MAX_ANNOTS) {
                        if (!m->param_annots[pidx]) {
                            m->param_annots[pidx] = calloc(MAX_ANNOTS, sizeof(smali_annotation_t));
                        }
                        smali_annotation_t ann;
                        memset(&ann, 0, sizeof(ann));
                        ann.visibility = 1;
                        if (smali_parse_annot_block(&start, &ann, lc.line) != 0) {
                            smali_annot_free(&ann);
                            free(inner_tok); free(tok);
                            return -1;
                        }
                        m->param_annots[pidx][m->param_annot_counts[pidx]++] = ann;
                    }
                    free(inner_tok);
                    continue;
                }
                start = save_p;
                free(inner_tok);
                break;
            }
            continue;
        } else if (strcmp(tok, ".prologue") == 0) {
            m->prologue_set = 1;
            free(tok);
            continue;
        } else if (strcmp(tok, ".epilogue") == 0) {
            m->epilogue_set = 1;
            free(tok);
            continue;
        } else if (strcmp(tok, ".local") == 0) {
            char *reg_tok = smali_next_token(&start);
            char *name_tok = smali_next_token(&start);
            char *type_tok = smali_next_token(&start);
            /* ".local v0, "name": <type>" - the colon separates the name from
             * the type. The lexer keeps a leading colon with its token, so
             * the type is either glued to it or in the next token. */
            if (type_tok && type_tok[0] == ':') {
                if (type_tok[1] != '\0') {
                    memmove(type_tok, type_tok + 1, strlen(type_tok));
                } else {
                    free(type_tok);
                    type_tok = smali_next_token(&start);
                }
            }
            if (reg_tok && name_tok && type_tok) {
                m->local_regs = realloc(m->local_regs, (m->local_count + 1) * sizeof(uint32_t));
                m->local_names = realloc(m->local_names, (m->local_count + 1) * sizeof(char *));
                m->local_types = realloc(m->local_types, (m->local_count + 1) * sizeof(char *));
                m->local_sigs = realloc(m->local_sigs, (m->local_count + 1) * sizeof(char *));
                m->local_regs[m->local_count] = smali_parse_register(reg_tok);
                m->local_names[m->local_count] = strdup(name_tok);
                m->local_types[m->local_count] = strdup(type_tok);
                /* A signature is only present when it follows an '='. */
                char *sig_tok = smali_next_token(&start);
                if (sig_tok && strcmp(sig_tok, "=") == 0) {
                    free(sig_tok);
                    sig_tok = smali_next_token(&start);
                } else {
                    free(sig_tok);
                    sig_tok = NULL;
                }
                m->local_sigs[m->local_count] = sig_tok ? strdup(sig_tok) : NULL;
                if (sig_tok) free(sig_tok);
                m->local_count++;
                free(name_tok);
                free(type_tok);
                free(reg_tok);
            } else {
                if (reg_tok) free(reg_tok);
                if (name_tok) free(name_tok);
                if (type_tok) free(type_tok);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".array-data") == 0) {
            char *width_tok = smali_next_token(&start);
            uint16_t elem_width = width_tok ? (uint16_t)atoi(width_tok) : 4;
            if (width_tok) free(width_tok);
            if (m->insns_count >= m->insns_cap) {
                m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
            }
            smali_insn_t *ins = &m->insns[m->insns_count++];
            memset(ins, 0, sizeof(smali_insn_t));
            ins->fmt = 103; /* array-data payload */
            ins->payload_element_width = elem_width;
            ins->line_number = current_line;
            ins->src_line = (int)lc.line;
            current_line = -1;

            /* collect values until .end array-data */
            while (*start) {
                char *line_end_inner = smali_eol(start);
                if (!line_end_inner) line_end_inner = start + strlen(start);
                char *p_inner = start;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && strcmp(next_inner, "array-data") == 0) {
                            free(next_inner);
                            free(tok_inner);
                            start = line_end_inner;
                            if (*start == '\n') start++;
                            break;
                        }
                        if (next_inner) free(next_inner);
                    } else {
                        uint8_t *expanded = realloc(ins->payload_data, ins->payload_data_len + (uint32_t)elem_width);
                        if (expanded) {
                            ins->payload_data = expanded;
                            int64_t val = smali_parse_lit(tok_inner);
                            for (int bi2 = 0; bi2 < (int)elem_width; bi2++) {
                                ins->payload_data[ins->payload_data_len + bi2] = (val >> (bi2 * 8)) & 0xFF;
                            }
                            ins->payload_data_len += elem_width;
                        }
                    }
                    free(tok_inner);
                }
                start = line_end_inner;
                if (*start == '\n') start++;
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".registers") == 0) {
            char *count_tok = smali_next_token(&start);
            if (count_tok) { m->registers_count = atoi(count_tok); free(count_tok); }
        } else if (strcmp(tok, ".annotation") == 0) {
            smali_annotation_t ann;
            memset(&ann, 0, sizeof(ann));
            ann.visibility = 1;
            free(tok);
            if (smali_parse_annot_block(&start, &ann, lc.line) != 0) {
                smali_annot_free(&ann);
                return -1;
            }
            if (m->annot_count >= m->annot_cap) {
                m->annot_cap = m->annot_cap ? m->annot_cap * 2 : 4;
                m->annots = realloc(m->annots, m->annot_cap * sizeof(smali_annotation_t));
            }
            m->annots[m->annot_count++] = ann;
            continue;
        } else if (strcmp(tok, ".locals") == 0) {
            char *count_tok = smali_next_token(&start);
            if (count_tok) {
                m->locals_count = atoi(count_tok);
                m->registers_count = m->locals_count + m->ins_count;
                free(count_tok);
            }
        } else if (strcmp(tok, ".packed-switch") == 0 || strcmp(tok, ".sparse-switch") == 0) {
            int is_packed = (strcmp(tok, ".packed-switch") == 0);
            if (m->insns_count >= m->insns_cap) {
                m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
            }
            smali_insn_t *ins = &m->insns[m->insns_count++];
            memset(ins, 0, sizeof(smali_insn_t));
            ins->fmt = is_packed ? 101 : 102;
            ins->line_number = current_line;
            ins->src_line = (int)lc.line;
            current_line = -1;
            
            if (is_packed) {
                char *first_key_tok = smali_next_token(&start);
                if (first_key_tok) {
                    ins->lit = (int32_t)strtol(first_key_tok, NULL, 0);
                    free(first_key_tok);
                }
            }

            while (*start) {
                char *line_end_inner = smali_eol(start);
                if (!line_end_inner) line_end_inner = start + strlen(start);
                char *p_inner = start;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && (strcmp(next_inner, "packed-switch") == 0 || strcmp(next_inner, "sparse-switch") == 0)) {
                            free(next_inner);
                            free(tok_inner);
                            start = line_end_inner;
                            if (*start == '\n') start++;
                            break;
                        }
                        if (next_inner) free(next_inner);
                    } else if (tok_inner[0] == ':') {
                        ins->payload_targets = realloc(ins->payload_targets, (ins->payload_targets_count + 1) * sizeof(char *));
                        ins->payload_targets[ins->payload_targets_count++] = strdup(tok_inner);
                    } else {
                        char *arrow_tok = smali_next_token(&p_inner);
                        if (arrow_tok && strcmp(arrow_tok, "->") == 0) {
                            char *target_tok = smali_next_token(&p_inner);
                            if (target_tok && target_tok[0] == ':') {
                                int32_t key = (int32_t)strtol(tok_inner, NULL, 0);
                                ins->payload_keys = realloc(ins->payload_keys, (ins->payload_targets_count + 1) * sizeof(int32_t));
                                ins->payload_targets = realloc(ins->payload_targets, (ins->payload_targets_count + 1) * sizeof(char *));
                                ins->payload_keys[ins->payload_targets_count] = key;
                                ins->payload_targets[ins->payload_targets_count] = strdup(target_tok);
                                ins->payload_targets_count++;
                            }
                            if (target_tok) free(target_tok);
                        }
                        if (arrow_tok) free(arrow_tok);
                    }
                    free(tok_inner);
                }
                start = line_end_inner;
                if (*start == '\n') start++;
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".catch") == 0 || strcmp(tok, ".catchall") == 0) {
            if (m->catches_count >= m->catches_cap) {
                m->catches_cap = m->catches_cap ? m->catches_cap * 2 : 4;
                m->catches = realloc(m->catches, m->catches_cap * sizeof(smali_catch_t));
            }
            smali_catch_t *ctch = &m->catches[m->catches_count++];
            memset(ctch, 0, sizeof(smali_catch_t));
            int is_catchall = (strcmp(tok, ".catchall") == 0);
            if (!is_catchall) {
                ctch->type = smali_next_token(&start);
            }
            char *range_tok = smali_next_token(&start);
            if (range_tok) {
                char *dots = strstr(range_tok, "..");
                if (dots) {
                    *dots = '\0';
                    char *start_lbl = range_tok;
                    if (start_lbl[0] == '{') start_lbl++;
                    while (*start_lbl == ' ' || *start_lbl == '\t') start_lbl++;
                    char *end_lbl = dots + 2;
                    while (*end_lbl == ' ' || *end_lbl == '\t') end_lbl++;
                    char *brace = strchr(end_lbl, '}');
                    if (brace) *brace = '\0';
                    size_t sl = strlen(start_lbl);
                    while (sl > 0 && (start_lbl[sl-1] == ' ' || start_lbl[sl-1] == '\t')) { start_lbl[sl-1] = '\0'; sl--; }
                    size_t el = strlen(end_lbl);
                    while (el > 0 && (end_lbl[el-1] == ' ' || end_lbl[el-1] == '\t')) { end_lbl[el-1] = '\0'; el--; }

                    ctch->start_label = strdup(start_lbl);
                    ctch->end_label = strdup(end_lbl);
                }
                free(range_tok);
            }
            ctch->handler_label = smali_next_token(&start);
        } else if (tok[0] == ':') {
            if (m->labels_count >= m->labels_cap) {
                m->labels_cap = m->labels_cap ? m->labels_cap * 2 : 8;
                m->labels = realloc(m->labels, m->labels_cap * sizeof(smali_label_t));
            }
            smali_label_t *lbl = &m->labels[m->labels_count++];
            lbl->name = strdup(tok);
            lbl->offset = m->insns_count;
        } else {
            const smali_op_t *op = find_smali_op(tok);
            if (op) {
                if (m->insns_count >= m->insns_cap) {
                    m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                    m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
                }
                smali_insn_t *ins = &m->insns[m->insns_count++];
                memset(ins, 0, sizeof(smali_insn_t));
                ins->op = op->op;
                ins->fmt = op->fmt;
                ins->kind = op->kind;
                ins->line_number = current_line;
                ins->src_line = (int)lc.line;
                current_line = -1;
                
                if (ins->fmt == 1 || ins->fmt == 21 || ins->fmt == 23 || ins->fmt == 24) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 2 || ins->fmt == 11) {
                    char *vA_tok = smali_next_token(&start);
                    char *lit_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (lit_tok) {
                        ins->lit = smali_parse_lit(lit_tok);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 3) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 4 || ins->fmt == 5 || ins->fmt == 6) {
                    ins->label_target = smali_next_token(&start);
                } else if (ins->fmt == 7 || ins->fmt == 19 || ins->fmt == 25) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->ref_str = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 8) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    ins->ref_str = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 9 || ins->fmt == 10) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (vA_tok) free(vA_tok);
                    if (ins->fmt == 10) {
                        char *vB_tok = smali_next_token(&start);
                        ins->vB = smali_parse_register(vB_tok);
                        if (vB_tok) free(vB_tok);
                    }
                    ins->label_target = smali_next_token(&start);
                } else if (ins->fmt == 12 || ins->fmt == 22) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    char *lit_tok = smali_next_token(&start);
                    if (lit_tok) {
                        ins->lit = smali_parse_lit(lit_tok);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 13) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    char *vC_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    ins->vC = smali_parse_register(vC_tok);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                    if (vC_tok) free(vC_tok);
                } else if (ins->fmt == 14) {
                    char *reg_list = smali_next_token(&start);
                    if (reg_list) {
                        char *p_reg = reg_list;
                        if (p_reg[0] == '{') p_reg++;
                        uint32_t reg_idx = 0;
                        char rname[32]; int r_len = 0;
                        while (*p_reg && *p_reg != '}') {
                            if (*p_reg == ' ' || *p_reg == '\t' || *p_reg == ',') {
                                if (r_len > 0) {
                                    rname[r_len] = '\0';
                                    uint32_t reg_num = smali_parse_register(rname);
                                    if (reg_idx < 5) ins->regs[reg_idx] = reg_num;
                                    reg_idx++; r_len = 0;
                                }
                            } else { rname[r_len++] = *p_reg; }
                            p_reg++;
                        }
                        if (r_len > 0) {
                            rname[r_len] = '\0';
                            uint32_t reg_num = smali_parse_register(rname);
                            if (reg_idx < 5) ins->regs[reg_idx] = reg_num;
                            reg_idx++;
                        }
                        ins->vA = reg_idx; free(reg_list);
                    }
                    ins->ref_str = smali_next_token(&start);
                } else if (ins->fmt == 15) {
                    char *reg_range = smali_next_token(&start);
                    if (reg_range) {
                        char *dots = strstr(reg_range, "..");
                        if (dots) {
                            *dots = '\0';
                            char *start_reg = reg_range;
                            if (start_reg[0] == '{') start_reg++;
                            char *end_reg = dots + 2;
                            char *end_brace = strchr(end_reg, '}');
                            if (end_brace) *end_brace = '\0';
                            uint32_t first = smali_parse_register(start_reg);
                            uint32_t last = smali_parse_register(end_reg);
                            ins->vC = first;
                            ins->vA = last - first + 1;
                        } else {
                            char *start_reg = reg_range;
                            if (start_reg[0] == '{') start_reg++;
                            char *end_brace = strchr(start_reg, '}');
                            if (end_brace) *end_brace = '\0';
                            ins->vC = smali_parse_register(start_reg);
                            ins->vA = 1;
                        }
                        free(reg_range);
                    }
                    ins->ref_str = smali_next_token(&start);
                } else if (ins->fmt == 16 || ins->fmt == 26) {
                    char *vA_tok = smali_next_token(&start);
                    char *lit_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (lit_tok) {
                        ins->lit = smali_parse_lit(lit_tok);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 18) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->label_target = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                }
            } else if (tok[0] == '.') {
                /* A directive the parser does not know. ".restart local" is
                 * emitted by baksmali and carries no data, so it is skipped;
                 * anything else is a typo worth reporting. */
                if (strcmp(tok, ".restart") != 0) {
                    smali_fail_at(lc.line, "unknown directive '%s' in method %s%s",
                                  tok, m->name ? m->name : "?",
                                  m->signature ? m->signature : "");
                    free(tok);
                    return -1;
                }
            } else {
                smali_fail_at(lc.line, "unknown instruction '%s' in method %s%s",
                              tok, m->name ? m->name : "?",
                              m->signature ? m->signature : "");
                free(tok);
                return -1;
            }
        }
        free(tok);
        start = line_end;
    }
    *p = start;
    return 0;
}
