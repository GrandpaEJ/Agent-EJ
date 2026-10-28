#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT(_op, _fmt, _kind, _name) {_op, _fmt, _kind, _name}

typedef enum {
    K_NONE, K_STRING, K_TYPE, K_FIELD, K_METHOD, K_PROTO, K_METHOD_HANDLE, K_METHOD_TYPE, K_CALL_SITE
} ref_kind;

typedef enum {
    F_10x, F_12x, F_11n, F_11x, F_10t, F_20t, F_30t,
    F_21c, F_22c, F_21t, F_22t, F_21s, F_22b, F_23x,
    F_35c, F_3rc, F_31i, F_51l, F_31t, F_21h, F_22s,
    F_22cs, F_35ms, F_3rms,
    F_22x, F_32x, F_12x_2, F_22s_multi,
} insn_fmt;

typedef struct { uint8_t op, fmt, kind; const char *name; } op_entry;

static const op_entry optab[] = {
    FMT(0x00, F_10x, K_NONE, "nop"),
    FMT(0x01, F_12x, K_NONE, "move"),
    FMT(0x02, F_22x, K_NONE, "move/from16"),
    FMT(0x03, F_32x, K_NONE, "move/16"),
    FMT(0x04, F_12x, K_NONE, "move-wide"),
    FMT(0x05, F_22x, K_NONE, "move-wide/from16"),
    FMT(0x06, F_32x, K_NONE, "move-wide/16"),
    FMT(0x07, F_12x, K_NONE, "move-object"),
    FMT(0x08, F_22x, K_NONE, "move-object/from16"),
    FMT(0x09, F_32x, K_NONE, "move-object/16"),
    FMT(0x0A, F_11x, K_NONE, "move-result"),
    FMT(0x0B, F_11x, K_NONE, "move-result-wide"),
    FMT(0x0C, F_11x, K_NONE, "move-result-object"),
    FMT(0x0D, F_11x, K_NONE, "move-exception"),
    FMT(0x0E, F_10x, K_NONE, "return-void"),
    FMT(0x0F, F_11x, K_NONE, "return"),
    FMT(0x10, F_11x, K_NONE, "return-wide"),
    FMT(0x11, F_11x, K_NONE, "return-object"),
    FMT(0x12, F_11n, K_NONE, "const/4"),
    FMT(0x13, F_21s, K_NONE, "const/16"),
    FMT(0x14, F_31i, K_NONE, "const"),
    FMT(0x15, F_21h, K_NONE, "const/high16"),
    FMT(0x16, F_21s, K_NONE, "const-wide/16"),
    FMT(0x17, F_31i, K_NONE, "const-wide/32"),
    FMT(0x18, F_51l, K_NONE, "const-wide"),
    FMT(0x19, F_21h, K_NONE, "const-wide/high16"),
    FMT(0x1A, F_21c, K_STRING, "const-string"),
    FMT(0x1B, F_21c, K_STRING, "const-string-jumbo"),
    FMT(0x1C, F_21c, K_TYPE, "const-class"),
    FMT(0x1D, F_11x, K_NONE, "monitor-enter"),
    FMT(0x1E, F_11x, K_NONE, "monitor-exit"),
    FMT(0x1F, F_21c, K_TYPE, "check-cast"),
    FMT(0x20, F_22c, K_TYPE, "instance-of"),
    FMT(0x21, F_12x, K_NONE, "array-length"),
    FMT(0x22, F_21c, K_TYPE, "new-instance"),
    FMT(0x23, F_22c, K_TYPE, "new-array"),
    FMT(0x24, F_35c, K_TYPE, "filled-new-array"),
    FMT(0x25, F_3rc, K_TYPE, "filled-new-array/range"),
    FMT(0x26, F_31t, K_NONE, "fill-array-data"),
    FMT(0x27, F_11x, K_NONE, "throw"),
    FMT(0x28, F_10t, K_NONE, "goto"),
    FMT(0x29, F_20t, K_NONE, "goto/16"),
    FMT(0x2A, F_30t, K_NONE, "goto/32"),
    FMT(0x2B, F_31t, K_NONE, "packed-switch"),
    FMT(0x2C, F_31t, K_NONE, "sparse-switch"),
    FMT(0x2D, F_23x, K_NONE, "cmpl-float"),
    FMT(0x2E, F_23x, K_NONE, "cmpg-float"),
    FMT(0x2F, F_23x, K_NONE, "cmpl-double"),
    FMT(0x30, F_23x, K_NONE, "cmpg-double"),
    FMT(0x31, F_23x, K_NONE, "cmp-long"),
    FMT(0x32, F_22t, K_NONE, "if-eq"),
    FMT(0x33, F_22t, K_NONE, "if-ne"),
    FMT(0x34, F_22t, K_NONE, "if-lt"),
    FMT(0x35, F_22t, K_NONE, "if-ge"),
    FMT(0x36, F_22t, K_NONE, "if-gt"),
    FMT(0x37, F_22t, K_NONE, "if-le"),
    FMT(0x38, F_21t, K_NONE, "if-eqz"),
    FMT(0x39, F_21t, K_NONE, "if-nez"),
    FMT(0x3A, F_21t, K_NONE, "if-ltz"),
    FMT(0x3B, F_21t, K_NONE, "if-gez"),
    FMT(0x3C, F_21t, K_NONE, "if-gtz"),
    FMT(0x3D, F_21t, K_NONE, "if-lez"),
    FMT(0x44, F_23x, K_NONE, "aget"),
    FMT(0x45, F_23x, K_NONE, "aget-wide"),
    FMT(0x46, F_23x, K_NONE, "aget-object"),
    FMT(0x47, F_23x, K_NONE, "aget-boolean"),
    FMT(0x48, F_23x, K_NONE, "aget-byte"),
    FMT(0x49, F_23x, K_NONE, "aget-char"),
    FMT(0x4A, F_23x, K_NONE, "aget-short"),
    FMT(0x4B, F_23x, K_NONE, "aput"),
    FMT(0x4C, F_23x, K_NONE, "aput-wide"),
    FMT(0x4D, F_23x, K_NONE, "aput-object"),
    FMT(0x4E, F_23x, K_NONE, "aput-boolean"),
    FMT(0x4F, F_23x, K_NONE, "aput-byte"),
    FMT(0x50, F_23x, K_NONE, "aput-char"),
    FMT(0x51, F_23x, K_NONE, "aput-short"),
    FMT(0x52, F_22c, K_FIELD, "iget"),
    FMT(0x53, F_22c, K_FIELD, "iget-wide"),
    FMT(0x54, F_22c, K_FIELD, "iget-object"),
    FMT(0x55, F_22c, K_FIELD, "iget-boolean"),
    FMT(0x56, F_22c, K_FIELD, "iget-byte"),
    FMT(0x57, F_22c, K_FIELD, "iget-char"),
    FMT(0x58, F_22c, K_FIELD, "iget-short"),
    FMT(0x59, F_22c, K_FIELD, "iput"),
    FMT(0x5A, F_22c, K_FIELD, "iput-wide"),
    FMT(0x5B, F_22c, K_FIELD, "iput-object"),
    FMT(0x5C, F_22c, K_FIELD, "iput-boolean"),
    FMT(0x5D, F_22c, K_FIELD, "iput-byte"),
    FMT(0x5E, F_22c, K_FIELD, "iput-char"),
    FMT(0x5F, F_22c, K_FIELD, "iput-short"),
    FMT(0x60, F_21c, K_FIELD, "sget"),
    FMT(0x61, F_21c, K_FIELD, "sget-wide"),
    FMT(0x62, F_21c, K_FIELD, "sget-object"),
    FMT(0x63, F_21c, K_FIELD, "sget-boolean"),
    FMT(0x64, F_21c, K_FIELD, "sget-byte"),
    FMT(0x65, F_21c, K_FIELD, "sget-char"),
    FMT(0x66, F_21c, K_FIELD, "sget-short"),
    FMT(0x67, F_21c, K_FIELD, "sput"),
    FMT(0x68, F_21c, K_FIELD, "sput-wide"),
    FMT(0x69, F_21c, K_FIELD, "sput-object"),
    FMT(0x6A, F_21c, K_FIELD, "sput-boolean"),
    FMT(0x6B, F_21c, K_FIELD, "sput-byte"),
    FMT(0x6C, F_21c, K_FIELD, "sput-char"),
    FMT(0x6D, F_21c, K_FIELD, "sput-short"),
    FMT(0x6E, F_35c, K_METHOD, "invoke-virtual"),
    FMT(0x6F, F_35c, K_METHOD, "invoke-super"),
    FMT(0x70, F_35c, K_METHOD, "invoke-direct"),
    FMT(0x71, F_35c, K_METHOD, "invoke-static"),
    FMT(0x72, F_35c, K_METHOD, "invoke-interface"),
    FMT(0x74, F_3rc, K_METHOD, "invoke-virtual/range"),
    FMT(0x75, F_3rc, K_METHOD, "invoke-super/range"),
    FMT(0x76, F_3rc, K_METHOD, "invoke-direct/range"),
    FMT(0x77, F_3rc, K_METHOD, "invoke-static/range"),
    FMT(0x78, F_3rc, K_METHOD, "invoke-interface/range"),
    FMT(0x7B, F_12x, K_NONE, "neg-int"),
    FMT(0x7C, F_12x, K_NONE, "not-int"),
    FMT(0x7D, F_12x, K_NONE, "neg-long"),
    FMT(0x7E, F_12x, K_NONE, "not-long"),
    FMT(0x7F, F_12x, K_NONE, "neg-float"),
    FMT(0x80, F_12x, K_NONE, "neg-double"),
    FMT(0x81, F_12x, K_NONE, "int-to-long"),
    FMT(0x82, F_12x, K_NONE, "int-to-float"),
    FMT(0x83, F_12x, K_NONE, "int-to-double"),
    FMT(0x84, F_12x, K_NONE, "long-to-int"),
    FMT(0x85, F_12x, K_NONE, "long-to-float"),
    FMT(0x86, F_12x, K_NONE, "long-to-double"),
    FMT(0x87, F_12x, K_NONE, "float-to-int"),
    FMT(0x88, F_12x, K_NONE, "float-to-long"),
    FMT(0x89, F_12x, K_NONE, "float-to-double"),
    FMT(0x8A, F_12x, K_NONE, "double-to-int"),
    FMT(0x8B, F_12x, K_NONE, "double-to-long"),
    FMT(0x8C, F_12x, K_NONE, "double-to-float"),
    FMT(0x8D, F_12x, K_NONE, "int-to-byte"),
    FMT(0x8E, F_12x, K_NONE, "int-to-char"),
    FMT(0x8F, F_12x, K_NONE, "int-to-short"),
    FMT(0x90, F_23x, K_NONE, "add-int"),
    FMT(0x91, F_23x, K_NONE, "sub-int"),
    FMT(0x92, F_23x, K_NONE, "mul-int"),
    FMT(0x93, F_23x, K_NONE, "div-int"),
    FMT(0x94, F_23x, K_NONE, "rem-int"),
    FMT(0x95, F_23x, K_NONE, "and-int"),
    FMT(0x96, F_23x, K_NONE, "or-int"),
    FMT(0x97, F_23x, K_NONE, "xor-int"),
    FMT(0x98, F_23x, K_NONE, "shl-int"),
    FMT(0x99, F_23x, K_NONE, "shr-int"),
    FMT(0x9A, F_23x, K_NONE, "ushr-int"),
    FMT(0x9B, F_23x, K_NONE, "add-long"),
    FMT(0x9C, F_23x, K_NONE, "sub-long"),
    FMT(0x9D, F_23x, K_NONE, "mul-long"),
    FMT(0x9E, F_23x, K_NONE, "div-long"),
    FMT(0x9F, F_23x, K_NONE, "rem-long"),
    FMT(0xA0, F_23x, K_NONE, "and-long"),
    FMT(0xA1, F_23x, K_NONE, "or-long"),
    FMT(0xA2, F_23x, K_NONE, "xor-long"),
    FMT(0xA3, F_23x, K_NONE, "shl-long"),
    FMT(0xA4, F_23x, K_NONE, "shr-long"),
    FMT(0xA5, F_23x, K_NONE, "ushr-long"),
    FMT(0xA6, F_23x, K_NONE, "add-float"),
    FMT(0xA7, F_23x, K_NONE, "sub-float"),
    FMT(0xA8, F_23x, K_NONE, "mul-float"),
    FMT(0xA9, F_23x, K_NONE, "div-float"),
    FMT(0xAA, F_23x, K_NONE, "rem-float"),
    FMT(0xAB, F_23x, K_NONE, "add-double"),
    FMT(0xAC, F_23x, K_NONE, "sub-double"),
    FMT(0xAD, F_23x, K_NONE, "mul-double"),
    FMT(0xAE, F_23x, K_NONE, "div-double"),
    FMT(0xAF, F_23x, K_NONE, "rem-double"),
    FMT(0xB0, F_12x, K_NONE, "add-int/2addr"),
    FMT(0xB1, F_12x, K_NONE, "sub-int/2addr"),
    FMT(0xB2, F_12x, K_NONE, "mul-int/2addr"),
    FMT(0xB3, F_12x, K_NONE, "div-int/2addr"),
    FMT(0xB4, F_12x, K_NONE, "rem-int/2addr"),
    FMT(0xB5, F_12x, K_NONE, "and-int/2addr"),
    FMT(0xB6, F_12x, K_NONE, "or-int/2addr"),
    FMT(0xB7, F_12x, K_NONE, "xor-int/2addr"),
    FMT(0xB8, F_12x, K_NONE, "shl-int/2addr"),
    FMT(0xB9, F_12x, K_NONE, "shr-int/2addr"),
    FMT(0xBA, F_12x, K_NONE, "ushr-int/2addr"),
    FMT(0xBB, F_12x, K_NONE, "add-long/2addr"),
    FMT(0xBC, F_12x, K_NONE, "sub-long/2addr"),
    FMT(0xBD, F_12x, K_NONE, "mul-long/2addr"),
    FMT(0xBE, F_12x, K_NONE, "div-long/2addr"),
    FMT(0xBF, F_12x, K_NONE, "rem-long/2addr"),
    FMT(0xC0, F_12x, K_NONE, "and-long/2addr"),
    FMT(0xC1, F_12x, K_NONE, "or-long/2addr"),
    FMT(0xC2, F_12x, K_NONE, "xor-long/2addr"),
    FMT(0xC3, F_12x, K_NONE, "shl-long/2addr"),
    FMT(0xC4, F_12x, K_NONE, "shr-long/2addr"),
    FMT(0xC5, F_12x, K_NONE, "ushr-long/2addr"),
    FMT(0xC6, F_12x, K_NONE, "add-float/2addr"),
    FMT(0xC7, F_12x, K_NONE, "sub-float/2addr"),
    FMT(0xC8, F_12x, K_NONE, "mul-float/2addr"),
    FMT(0xC9, F_12x, K_NONE, "div-float/2addr"),
    FMT(0xCA, F_12x, K_NONE, "rem-float/2addr"),
    FMT(0xCB, F_12x, K_NONE, "add-double/2addr"),
    FMT(0xCC, F_12x, K_NONE, "sub-double/2addr"),
    FMT(0xCD, F_12x, K_NONE, "mul-double/2addr"),
    FMT(0xCE, F_12x, K_NONE, "div-double/2addr"),
    FMT(0xCF, F_12x, K_NONE, "rem-double/2addr"),
    FMT(0xD0, F_22s, K_NONE, "add-int/lit16"),
    FMT(0xD1, F_22s, K_NONE, "rsub-int"), /* opcode 0xD1 drops the /lit16 suffix in real smali output; see smali_optab.h */
    FMT(0xD2, F_22s, K_NONE, "mul-int/lit16"),
    FMT(0xD3, F_22s, K_NONE, "div-int/lit16"),
    FMT(0xD4, F_22s, K_NONE, "rem-int/lit16"),
    FMT(0xD5, F_22s, K_NONE, "and-int/lit16"),
    FMT(0xD6, F_22s, K_NONE, "or-int/lit16"),
    FMT(0xD7, F_22s, K_NONE, "xor-int/lit16"),
    FMT(0xD8, F_22b, K_NONE, "add-int/lit8"),
    FMT(0xD9, F_22b, K_NONE, "rsub-int/lit8"),
    FMT(0xDA, F_22b, K_NONE, "mul-int/lit8"),
    FMT(0xDB, F_22b, K_NONE, "div-int/lit8"),
    FMT(0xDC, F_22b, K_NONE, "rem-int/lit8"),
    FMT(0xDD, F_22b, K_NONE, "and-int/lit8"),
    FMT(0xDE, F_22b, K_NONE, "or-int/lit8"),
    FMT(0xDF, F_22b, K_NONE, "xor-int/lit8"),
    FMT(0xE0, F_22b, K_NONE, "shl-int/lit8"),
    FMT(0xE1, F_22b, K_NONE, "shr-int/lit8"),
    FMT(0xE2, F_22b, K_NONE, "ushr-int/lit8"),
    /* DEX 037+ extended opcodes */
    FMT(0xFA, F_35c, K_CALL_SITE, "invoke-polymorphic"),
    FMT(0xFB, F_35c, K_CALL_SITE, "invoke-polymorphic/range"),
    FMT(0xFC, F_35c, K_CALL_SITE, "invoke-custom"),
    FMT(0xFD, F_3rc, K_CALL_SITE, "invoke-custom/range"),
    FMT(0xFE, F_21c, K_METHOD_HANDLE, "const-method-handle"),
    FMT(0xFF, F_21c, K_METHOD_TYPE, "const-method-type"),
};

static const op_entry *find_op(uint8_t op) {
    for (size_t i = 0; i < sizeof(optab)/sizeof(optab[0]); i++)
        if (optab[i].op == op) return &optab[i];
    return NULL;
}

// Decode an instruction at insns[offset]. Returns number of 16-bit units consumed.
int dex_decode_insn(const uint16_t *insns, int offset, uint32_t *out_op,
                    uint32_t *out_vA, uint32_t *out_vB, uint32_t *out_vC,
                    uint32_t *out_ref, int32_t *out_lit) {
    uint16_t word = insns[offset];
    uint8_t op = word & 0xFF;
    uint8_t arg = (word >> 8) & 0xFF;
    uint8_t nib0 = word & 0x0F;
    (void)nib0;
    const op_entry *e = find_op(op);
    if (!e) return -1;

    *out_op = op;
    uint32_t vA = 0, vB = 0, vC = 0, ref = 0;
    int32_t lit = 0;
    int words = 1;

    switch (e->fmt) {
    case F_10x:
        break;
    case F_12x: {
        vA = arg & 0x0F;
        vB = (arg >> 4) & 0x0F;
        break;
    }
    case F_11n: {
        vA = arg & 0x0F;
        lit = (int8_t)arg >> 4;
        break;
    }
    case F_11x:
        vA = arg;
        break;
    case F_10t: {
        int8_t off = (int8_t)arg;
        lit = off;
        break;
    }
    case F_20t: {
        int16_t off = (int16_t)insns[offset + 1];
        lit = off; words = 2;
        break;
    }
    case F_30t: {
        uint32_t off = (uint32_t)insns[offset + 1] | ((uint32_t)insns[offset + 2] << 16);
        lit = (int32_t)off; words = 3;
        break;
    }
    case F_21c:
    case F_21t:
    case F_21s:
    case F_21h: {
        vA = arg;
        if (op == 0x1B) {
            /* const-string/jumbo: 32-bit string index */
            ref = (uint32_t)insns[offset + 1] | ((uint32_t)insns[offset + 2] << 16);
            words = 3;
        } else {
            ref = insns[offset + 1];
            words = 2;
        }
        break;
    }
    case F_22c:
    case F_22t:
    case F_22s: {
        vA = arg & 0x0F;
        vB = (arg >> 4) & 0x0F;
        ref = insns[offset + 1];
        words = 2;
        break;
    }
    case F_22b: {
        vA = arg;
        vB = insns[offset + 1] & 0xFF;
        lit = (int8_t)((insns[offset + 1] >> 8) & 0xFF);
        words = 2;
        break;
    }
    case F_23x: {
        vA = arg;
        vB = insns[offset + 1] & 0xFF;
        vC = (insns[offset + 1] >> 8) & 0xFF;
        words = 2;
        break;
    }
    case F_35c: {
        uint16_t w1 = insns[offset + 1];
        uint16_t w2 = insns[offset + 2];
        vA = (arg >> 4) & 0x0F; // A = argument count (bits 12-15)
        vC = arg & 0x0F;        // C = first argument register (bits 8-11)
        ref = w1;               // reference index
        vB = w2 & 0xFF;         // D (low nibble), E (high nibble)
        lit = (int16_t)(w2 >> 8); // F (low nibble), G (high nibble)
        words = 3;
        break;
    }
    case F_3rc: {
        vA = arg;        // number of arguments
        ref = insns[offset + 1];
        vC = insns[offset + 2];
        words = 3;
        break;
    }
    case F_31i: {
        vA = arg;
        uint32_t lo = insns[offset + 1];
        uint32_t hi = insns[offset + 2];
        lit = (int32_t)(lo | (hi << 16));
        words = 3;
        break;
    }
    case F_51l: {
        vA = arg;
        uint64_t val = 0;
        for (int i = 0; i < 4; i++)
            val |= (uint64_t)insns[offset + 1 + i] << (i * 16);
        lit = (int32_t)(val & 0xFFFFFFFF);
        words = 5;
        break;
    }
    case F_31t:
        vA = arg;
        ref = (uint32_t)insns[offset + 1] | ((uint32_t)insns[offset + 2] << 16);
        words = 3;
        break;
    case F_22x: {
        vA = arg;
        vB = insns[offset + 1];
        words = 2;
        break;
    }
    case F_32x: {
        vA = insns[offset + 1];
        vB = insns[offset + 2];
        words = 3;
        break;
    }
    case F_22cs:
    case F_35ms:
    case F_3rms:
        vA = arg;
        words = 2;
        break;
    }

    *out_vA = vA; *out_vB = vB; *out_vC = vC;
    *out_ref = ref; *out_lit = lit;
    return words;
}

const char *dex_insn_name(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->name : NULL;
}

int dex_insn_kind(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->kind : 0;
}

int dex_insn_fmt(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->fmt : -1;
}
