#include "smali_params.h"

/* Skips one type descriptor; returns its register width (0 at the end). */
static int next_param(const char **pp) {
    const char *p = *pp;
    if (!*p || *p == ')') return 0;
    int wide = (*p == 'J' || *p == 'D');
    while (*p == '[') p++;
    if (*p == 'L') { while (*p && *p != ';') p++; }
    if (*p) p++;
    *pp = p;
    return wide ? 2 : 1;
}

uint32_t smali_sig_param_count(const char *sig) {
    if (!sig) return 0;
    const char *p = sig + (*sig == '(');
    uint32_t n = 0;
    while (next_param(&p)) n++;
    return n;
}

int smali_param_index(const char *sig, int is_static, uint32_t reg_off) {
    if (!sig) return -1;
    if (!is_static) {
        if (reg_off == 0) return -1;
        reg_off--;
    }
    const char *p = sig + (*sig == '(');
    uint32_t reg = 0;
    for (int idx = 0;; idx++) {
        int w = next_param(&p);
        if (!w) return -1;
        if (reg_off == reg) return idx;
        if (reg_off < reg + (uint32_t)w) return -1;
        reg += (uint32_t)w;
    }
}
