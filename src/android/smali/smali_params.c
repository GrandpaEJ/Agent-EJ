#include "smali_params.h"
#include "smali_annot.h"
#include "smali_lexer.h"
#include <stdlib.h>
#include <string.h>

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

static int push_annot(smali_annotation_t **arr, uint32_t *count, uint32_t *cap,
                      smali_annotation_t *a) {
    if (*count >= *cap) {
        uint32_t ncap = *cap ? *cap * 2 : 4;
        smali_annotation_t *n = realloc(*arr, ncap * sizeof(*n));
        if (!n) return -1;
        *arr = n;
        *cap = ncap;
    }
    (*arr)[(*count)++] = *a;
    return 0;
}

int smali_parse_param_body(char **p, smali_method_def_t *m, uint32_t param, uint32_t line) {
    smali_annotation_t pend[MAX_ANNOTS];
    uint32_t n = 0;
    int closed = 0;
    char *s = *p;
    while (*s) {
        char *save = s;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (!*s) break;
        if (*s == '#') { char *nl = smali_eol(s); s = nl ? nl + 1 : s + strlen(s); continue; }
        char *tok = smali_next_token(&s);
        if (!tok) break;
        if (strcmp(tok, ".end") == 0) {
            char *sub = smali_next_token(&s);
            closed = sub && strcmp(sub, "param") == 0;
            free(sub);
            free(tok);
            if (!closed) s = save;
            break;
        }
        if (strcmp(tok, ".annotation") != 0) { free(tok); s = save; break; }
        free(tok);
        smali_annotation_t ann;
        memset(&ann, 0, sizeof(ann));
        ann.visibility = 1;
        if (smali_parse_annot_block(&s, &ann, line) != 0) {
            smali_annot_free(&ann);
            for (uint32_t i = 0; i < n; i++) smali_annot_free(&pend[i]);
            *p = s;
            return -1;
        }
        if (n < MAX_ANNOTS) pend[n++] = ann;
        else smali_annot_free(&ann);
    }
    *p = s;
    for (uint32_t i = 0; i < n; i++) {
        if (closed && param < m->param_name_count) {
            if (!m->param_annots[param])
                m->param_annots[param] = calloc(MAX_ANNOTS, sizeof(smali_annotation_t));
            if (m->param_annots[param] && m->param_annot_counts[param] < MAX_ANNOTS) {
                m->param_annots[param][m->param_annot_counts[param]++] = pend[i];
                continue;
            }
        } else if (!closed && push_annot(&m->annots, &m->annot_count, &m->annot_cap, &pend[i]) == 0) {
            continue;
        }
        smali_annot_free(&pend[i]);
    }
    return 0;
}
