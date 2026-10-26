#include "smali_flow_internal.h"
#include "smali_optab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void fb_init(flow_buf_t *b) {
    b->cap = FLOW_OUT_CHUNK;
    b->len = 0;
    b->buf = (char *)malloc(b->cap);
    if (b->buf) b->buf[0] = 0;
}

void fb_free(flow_buf_t *b) {
    free(b->buf);
    b->buf = NULL;
    b->len = b->cap = 0;
}

static void fb_reserve(flow_buf_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    while (b->len + extra + 1 > b->cap) b->cap *= 2;
    b->buf = (char *)realloc(b->buf, b->cap);
}

void fb_append(flow_buf_t *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    fb_reserve(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

void fb_appendf(flow_buf_t *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    fb_reserve(b, (size_t)n);
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

char *fb_steal(flow_buf_t *b) {
    char *r = b->buf;
    if (!r) r = strdup("");
    b->buf = NULL;
    b->len = b->cap = 0;
    return r;
}

const char *flow_op_name(uint8_t op) {
    for (size_t i = 0; i < sizeof(smali_optab) / sizeof(smali_optab[0]); i++)
        if (smali_optab[i].op == op) return smali_optab[i].name;
    return NULL;
}

int flow_insn_is_term(const smali_insn_t *ins) {
    if (ins->fmt >= 4 && ins->fmt <= 6) return 1;
    if (ins->fmt == 9 || ins->fmt == 10) return 1;
    if (ins->op >= 0x0E && ins->op <= 0x11) return 1;
    if (ins->op == 0x27) return 1;
    if (ins->op == 0x2B || ins->op == 0x2C) return 1;
    return 0;
}

int flow_insn_is_code(const smali_insn_t *ins) {
    return ins->fmt <= 26;
}

int flow_insn_is_branch(const smali_insn_t *ins) {
    if (ins->fmt >= 4 && ins->fmt <= 6) return 1;
    if (ins->fmt == 9 || ins->fmt == 10) return 1;
    return 0;
}

int flow_insn_is_switch(const smali_insn_t *ins) {
    return ins->op == 0x2B || ins->op == 0x2C;
}

int flow_insn_is_return(const smali_insn_t *ins) {
    return ins->op >= 0x0E && ins->op <= 0x11;
}

int flow_insn_is_throw(const smali_insn_t *ins) {
    return ins->op == 0x27;
}

int flow_insn_is_invoke(const smali_insn_t *ins) {
    return ins->op >= 0x6E && ins->op <= 0x78;
}

void flow_reg_name(char *b, size_t sz, uint32_t r) {
    if (r & 0x80000000)
        snprintf(b, sz, "p%d", r & ~0x80000000);
    else
        snprintf(b, sz, "v%d", r);
}

void flow_fmt_insn(const smali_insn_t *ins, char *b, size_t sz, int with_line) {
    const char *nm = flow_op_name(ins->op);
    if (!nm) { snprintf(b, sz, "???"); return; }
    char r0[16], r1[16], r2[16];
    flow_reg_name(r0, sizeof(r0), ins->vA);
    flow_reg_name(r1, sizeof(r1), ins->vB);
    flow_reg_name(r2, sizeof(r2), ins->vC);

    char prefix[24] = "";
    if (with_line && ins->line_number > 0)
        snprintf(prefix, sizeof(prefix), "L%-4d ", ins->line_number);

    switch (ins->fmt) {
        case 0: snprintf(b, sz, "%s%s", prefix, nm); break;
        case 1: case 23: case 24:
            snprintf(b, sz, "%s%s %s, %s", prefix, nm, r0, r1); break;
        case 2: snprintf(b, sz, "%s%s %s, #%lld", prefix, nm, r0, (long long)ins->lit); break;
        case 3: snprintf(b, sz, "%s%s %s", prefix, nm, r0); break;
        case 4: case 5: case 6:
            snprintf(b, sz, "%s%s :%s", prefix, nm, ins->label_target ? ins->label_target : "?"); break;
        case 7: case 25:
            snprintf(b, sz, "%s%s %s, %s", prefix, nm, r0, ins->ref_str ? ins->ref_str : ""); break;
        case 8:
            snprintf(b, sz, "%s%s %s, %s, %s", prefix, nm, r0, r1, ins->ref_str ? ins->ref_str : ""); break;
        case 9:
            snprintf(b, sz, "%s%s %s, :%s", prefix, nm, r0, ins->label_target ? ins->label_target : "?"); break;
        case 10:
            snprintf(b, sz, "%s%s %s, %s, :%s", prefix, nm, r0, r1, ins->label_target ? ins->label_target : "?"); break;
        case 11: case 16: case 19:
            snprintf(b, sz, "%s%s %s, #%lld", prefix, nm, r0, (long long)ins->lit); break;
        case 12:
            snprintf(b, sz, "%s%s %s, %s, #%lld", prefix, nm, r0, r1, (long long)ins->lit); break;
        case 13:
            snprintf(b, sz, "%s%s %s, %s, %s", prefix, nm, r0, r1, r2); break;
        case 14: {
            char rl[64]; int pos = 0;
            pos += snprintf(rl + pos, sizeof(rl) - pos, "{");
            for (int i = 0; i < 5 && (i == 0 || ins->regs[i] != 0); i++) {
                if (i > 0) pos += snprintf(rl + pos, sizeof(rl) - pos, ", ");
                char rd[16]; flow_reg_name(rd, sizeof(rd), ins->regs[i]);
                pos += snprintf(rl + pos, sizeof(rl) - pos, "%s", rd);
            }
            snprintf(rl + pos, sizeof(rl) - pos, "}");
            snprintf(b, sz, "%s%s %s, %s", prefix, nm, rl, ins->ref_str ? ins->ref_str : "");
            break;
        }
        case 15:
            snprintf(b, sz, "%s%s {%s .. %s}, %s", prefix, nm, r2, r0, ins->ref_str ? ins->ref_str : "");
            break;
        case 18:
            snprintf(b, sz, "%s%s %s, :%s", prefix, nm, r0, ins->label_target ? ins->label_target : "?"); break;
        case 22:
            snprintf(b, sz, "%s%s %s, %s, #%lld", prefix, nm, r0, r1, (long long)ins->lit); break;
        case 26:
            snprintf(b, sz, "%s%s %s, #%lld", prefix, nm, r0, (long long)(int64_t)ins->lit); break;
        default: snprintf(b, sz, "%s%s", prefix, nm); break;
    }
}
