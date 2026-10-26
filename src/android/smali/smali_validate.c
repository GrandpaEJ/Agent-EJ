#include "smali_validate.h"
#include "smali_error.h"
#include <stdio.h>

/* Per-format register limits, indexed by smali_insn_t.fmt. Each row mirrors
 * one row of the DEX bytecode format table; -1 means the format has no
 * register operand in that slot:
 *
 *   fmt  format  op layout            limits (vA, vB, vC)
 *     0  10x     AA|op                no register operands
 *   1,21 12x     B|A|op               15, 15
 *     2  11n     B|A|op               A: 15 (B is a 4-bit literal)
 *     3  11x     AA|op                255
 *   4, 5, 6  10t/20t/30t              no register operands
 *  7,19 21c     AA|op BBBB            255
 *     8  22c     B|A|op CCCC          15, 15
 *     9  21t     AA|op BBBB           255
 *    10  22t     B|A|op CCCC          15, 15
 *    11  21s     AA|op BBBB           255
 *    12  22b     AA|op CC|BB          255, 255
 *    13  23x     AA|op CC|BB          255, 255, 255
 *    14  35c     A|op BBBB G|F|E|D|C  count 0..5, argument regs 15 (below)
 *    15  3rc     A|op BBBB CCCC       255 (count), -, 65535 (first reg)
 *    16  31i     AA|op BBBB0000       255
 *    18  31t     AA|op BBBB0000       255
 *    22  22s     B|A|op CCCC          15, 15
 *    23  22x     AA|op BBBB           255 (A), 65535 (B)
 *    24  32x     AAAA|op BBBB         65535, 65535
 *    25  31c     AA|op BBBB0000       255
 *    26  51l     AA|op BBBB0000...    255
 *
 * Rows 17 and 20 are unused format numbers. */
typedef struct {
    const char *name;
    int32_t va;
    int32_t vb;
    int32_t vc;
} smali_fmt_limits_t;

static const smali_fmt_limits_t k_fmt_limits[] = {
    /*  0 */ { "F_10x",  -1,   -1,   -1 },
    /*  1 */ { "F_12x",  15,   15,   -1 },
    /*  2 */ { "F_11n",  15,   -1,   -1 },
    /*  3 */ { "F_11x",  255,  -1,   -1 },
    /*  4 */ { "F_10t",  -1,   -1,   -1 },
    /*  5 */ { "F_20t",  -1,   -1,   -1 },
    /*  6 */ { "F_30t",  -1,   -1,   -1 },
    /*  7 */ { "F_21c",  255,  -1,   -1 },
    /*  8 */ { "F_22c",  15,   15,   -1 },
    /*  9 */ { "F_21t",  255,  -1,   -1 },
    /* 10 */ { "F_22t",  15,   15,   -1 },
    /* 11 */ { "F_21s",  255,  -1,   -1 },
    /* 12 */ { "F_22b",  255,  255,  -1 },
    /* 13 */ { "F_23x",  255,  255,  255 },
    /* 14 */ { "F_35c",  -1,   -1,   -1 },  /* count + arg regs, checked below */
    /* 15 */ { "F_3rc",  255,  -1,   65535 },
    /* 16 */ { "F_31i",  255,  -1,   -1 },
    /* 17 */ { "(unused)", -1, -1,   -1 },
    /* 18 */ { "F_31t",  255,  -1,   -1 },
    /* 19 */ { "F_21c",  255,  -1,   -1 },
    /* 20 */ { "(unused)", -1, -1,   -1 },
    /* 21 */ { "F_12x",  15,   15,   -1 },
    /* 22 */ { "F_22s",  15,   15,   -1 },
    /* 23 */ { "F_22x",  255,  65535, -1 },
    /* 24 */ { "F_32x",  65535, 65535, -1 },
    /* 25 */ { "F_31c",  255,  -1,   -1 },
    /* 26 */ { "F_51l",  255,  -1,   -1 },
};

#define FMT_LIMITS_COUNT (sizeof(k_fmt_limits) / sizeof(k_fmt_limits[0]))
#define FMT_35C 14

/* F_35c: A is a 4-bit argument count. 0 is legal (invoke-static {} on a
 * no-argument method), 5 is the maximum; anything above silently dropped
 * registers before this check existed. Argument registers are 4-bit. */
#define F35C_MAX_COUNT 5
#define F35C_MAX_REG   15

static int insn_line(const smali_insn_t *ins) {
    /* ins->line_number carries the nearest .line directive (-1 when the
     * method has none); physical source lines arrive with the parser's
     * syntax-error reporting. */
    return ins->line_number;
}

static int fail_op(const smali_method_def_t *m, const smali_insn_t *ins,
                   const char *fmt_name, const char *slot, uint32_t got, uint32_t max) {
    return smali_fail("register %s out of range for %s (v%u > v%u) in %s%s at line %d [op 0x%02x]",
                      slot, fmt_name, got, max,
                      m->name ? m->name : "?", m->signature ? m->signature : "",
                      insn_line(ins), ins->op);
}

int smali_validate_insn(const smali_method_def_t *m, const smali_insn_t *ins,
                        uint32_t vA, uint32_t vB, uint32_t vC,
                        const uint32_t *invoke_regs) {
    if (ins->fmt == FMT_35C) {
        if (ins->vA > F35C_MAX_COUNT)
            return smali_fail("invoke argument count %u out of range for F_35c (max %d) in %s%s at line %d [op 0x%02x]",
                              ins->vA, F35C_MAX_COUNT,
                              m->name ? m->name : "?", m->signature ? m->signature : "",
                              insn_line(ins), ins->op);
        if (!invoke_regs) return 0; /* caller did not resolve; encoder always does */
        for (uint32_t i = 0; i < ins->vA && i < 5; i++) {
            if (invoke_regs[i] > F35C_MAX_REG) {
                char slot[8];
                snprintf(slot, sizeof(slot), "arg%u", i);
                return fail_op(m, ins, "F_35c", slot, invoke_regs[i], F35C_MAX_REG);
            }
        }
        return 0;
    }

    if (ins->fmt >= FMT_LIMITS_COUNT) return 0; /* payloads 101..103 */
    const smali_fmt_limits_t *lim = &k_fmt_limits[ins->fmt];

    if (lim->va >= 0 && vA > (uint32_t)lim->va)
        return fail_op(m, ins, lim->name, "vA", vA, (uint32_t)lim->va);
    if (lim->vb >= 0 && vB > (uint32_t)lim->vb)
        return fail_op(m, ins, lim->name, "vB", vB, (uint32_t)lim->vb);
    if (lim->vc >= 0 && vC > (uint32_t)lim->vc)
        return fail_op(m, ins, lim->name, "vC", vC, (uint32_t)lim->vc);
    return 0;
}
