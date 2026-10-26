#include "smali_annot.h"
#include "smali_lexer.h"
#include "smali_error.h"
#include <stdlib.h>
#include <string.h>

int smali_field_ref_valid(const char *ref) {
    if (!ref || ref[0] != 'L') return 0;
    const char *arrow = strstr(ref, "->");
    if (!arrow || arrow == ref || arrow[-1] != ';') return 0;
    const char *colon = strchr(arrow, ':');
    if (!colon || colon <= arrow + 2) return 0;  /* the name must not be empty */
    return colon[1] != '\0';                    /* the type must not be empty */
}

int smali_parse_annot_value(char **p, smali_annotation_elem_t *el, uint32_t line) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '"') {
        el->value_type = VALUE_TYPE_STRING;
        el->value_str = smali_parse_string_literal(p);
        if (!el->value_str) el->value_str = strdup("");
        return 0;
    }
    if (**p == '{') {
        (*p)++;
        el->value_type = VALUE_TYPE_ARRAY;
        el->array_count = 0;
        el->arr_head = NULL;
        el->arr_tail = NULL;
        while (**p) {
            while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n' || **p == ',') (*p)++;
            if (**p == '}' || **p == 0) { if (**p == '}') (*p)++; break; }
            if (**p == '#') { char *nl = smali_eol(*p); *p = nl ? nl + 1 : *p + strlen(*p); continue; }
            if (el->array_count >= 256) {
                while (**p && **p != '}') (*p)++;
                if (**p == '}') (*p)++;
                break;
            }
            smali_annot_val_t *node = calloc(1, sizeof(smali_annot_val_t));
            if (!node) return -1;
            if (smali_parse_annot_value(p, &node->elem, line) != 0) {
                smali_annot_elem_free(&node->elem);
                free(node);
                return -1;
            }
            node->next = NULL;
            if (el->arr_tail) el->arr_tail->next = node;
            else el->arr_head = node;
            el->arr_tail = node;
            el->array_count++;
        }
        return 0;
    }
    if (**p == '\'') {
        char *q = *p + 1;
        unsigned cv = (unsigned char)*q;
        if (*q == '\\') {
            q++;
            if (*q == 'u') cv = (unsigned)strtoul(q + 1, &q, 16);
            else { cv = *q == 'n' ? '\n' : *q == 't' ? '\t' : *q == 'r' ? '\r' : *q == 'b' ? '\b' : *q == 'f' ? '\f' : (unsigned char)*q; q++; }
        } else q++;
        if (*q == '\'') q++;
        *p = q;
        el->value_type = VALUE_TYPE_CHAR; el->value_int = cv;
        return 0;
    }
    char *tok = smali_next_token(p);
    if (!tok) { el->value_type = VALUE_TYPE_NULL; return 0; }
    if (strcmp(tok, "null") == 0) { el->value_type = VALUE_TYPE_NULL; free(tok); return 0; }
    if (strcmp(tok, "true") == 0) { el->value_type = VALUE_TYPE_BOOL; el->value_int = 1; free(tok); return 0; }
    if (strcmp(tok, "false") == 0) { el->value_type = VALUE_TYPE_BOOL; el->value_int = 0; free(tok); return 0; }
    if (strcmp(tok, ".enum") == 0) {
        free(tok);
        el->value_type = VALUE_TYPE_ENUM;
        el->value_str = smali_next_token(p);
        if (!el->value_str) {
            smali_fail_at(line, "malformed .enum value (expected Lclass;->name:type)");
            return -1;
        }
        if (!smali_field_ref_valid(el->value_str)) {
            smali_fail_at(line, "malformed .enum value '%s' (expected Lclass;->name:type)", el->value_str);
            return -1;
        }
        return 0;
    }
    if (tok[0] == 'L' || tok[0] == '[') {
        char *arrow = strstr(tok, "->");
        if (arrow && strchr(arrow, '(')) {
            el->value_type = VALUE_TYPE_METHOD;
            el->value_str = tok;
        } else if (arrow && strchr(arrow, ':')) {
            el->value_type = VALUE_TYPE_FIELD;
            el->value_str = tok;
            if (!smali_field_ref_valid(tok)) {
                smali_fail_at(line, "malformed field reference '%s' (expected Lclass;->name:type)", tok);
                return -1;
            }
        } else if (arrow) {
            /* "Lclass;->name" without a type is neither a type nor a field. */
            smali_fail_at(line, "malformed field reference '%s' (expected Lclass;->name:type)", tok);
            return -1;
        } else {
            el->value_type = VALUE_TYPE_TYPE;
            el->value_str = tok;
        }
        return 0;
    }
    if (strcmp(tok, ".subannotation") == 0) {
        free(tok);
        el->value_type = VALUE_TYPE_ANNOT;
        el->annot_type = smali_next_token(p);
        int sub_cap = 16;
        el->sub_elems = calloc(sub_cap, sizeof(smali_annotation_elem_t));
        el->annot_elem_count = 0;
        while (**p) {
            while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
            if (**p == '#') { char *nl = smali_eol(*p); *p = nl ? nl + 1 : *p + strlen(*p); continue; }
            char *et = smali_next_token(p);
            if (!et) break;
            if (strcmp(et, ".end") == 0) { char *sub = smali_next_token(p); if (sub) free(sub); free(et); break; }
            if (strcmp(et, ".subannotation") == 0) { free(et); continue; }
            char *eq = smali_next_token(p);
            if (eq && strcmp(eq, "=") == 0) {
                free(eq);
                if (el->annot_elem_count >= sub_cap) {
                    sub_cap *= 2;
                    el->sub_elems = realloc(el->sub_elems, sub_cap * sizeof(smali_annotation_elem_t));
                }
                smali_annotation_elem_t *se = &el->sub_elems[el->annot_elem_count];
                memset(se, 0, sizeof(*se));
                se->name = et;
                if (smali_parse_annot_value(p, se, line) != 0) {
                    smali_annot_elem_free(se);
                    return -1;
                }
                el->annot_elem_count++;
            } else { if (eq) free(eq); free(et); }
        }
        return 0;
    }
    size_t tl = strlen(tok);
    if (strncmp(tok + (tok[0] == '-'), "0x", 2) != 0 && tl > 1 && (tok[tl - 1] == 'f' || tok[tl - 1] == 'd')) {
        el->value_type = tok[tl - 1] == 'd' ? VALUE_TYPE_DOUBLE : VALUE_TYPE_FLOAT;
        el->value_double = strtod(tok, NULL);
    } else {
        el->value_int = (int64_t)smali_parse_lit(tok);
        el->value_type = tok[tl - 1] == 'L' ? VALUE_TYPE_LONG
                       : tok[tl - 1] == 's' ? VALUE_TYPE_SHORT
                       : tok[tl - 1] == 't' ? VALUE_TYPE_BYTE : VALUE_TYPE_INT;
    }
    free(tok);
    return 0;
}

int smali_parse_annot_block(char **p, smali_annotation_t *ann, uint32_t line) {
    char *vis_tok = smali_next_token(p);
    if (vis_tok) {
        if (strcmp(vis_tok, "runtime") == 0) ann->visibility = 1;
        else if (strcmp(vis_tok, "build") == 0) ann->visibility = 0;
        else if (strcmp(vis_tok, "system") == 0) ann->visibility = 2;
        else { ann->type = vis_tok; vis_tok = NULL; }
        if (vis_tok) free(vis_tok);
    }
    if (!ann->type) ann->type = smali_next_token(p);
    if (!ann->type) {
        smali_fail_at(line, "malformed .annotation directive (expected a type)");
        return -1;
    }
    while (**p) {
        while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
        if (**p == '#') { char *nl = smali_eol(*p); *p = nl ? nl + 1 : *p + strlen(*p); continue; }
        char *elem_tok = smali_next_token(p);
        if (!elem_tok) break;
        if (strcmp(elem_tok, ".end") == 0) {
            char *at = smali_next_token(p);
            if (at) free(at);
            free(elem_tok);
            break;
        }
        char *eq_tok = smali_next_token(p);
        if (eq_tok && strcmp(eq_tok, "=") == 0) {
            free(eq_tok);
            if (ann->elem_count < MAX_ANNOT_ELEMS) {
                smali_annotation_elem_t *el = &ann->elems[ann->elem_count];
                memset(el, 0, sizeof(*el));
                el->name = elem_tok;
                if (smali_parse_annot_value(p, el, line) != 0) {
                    smali_annot_elem_free(el);
                    return -1;
                }
                ann->elem_count++;
            } else { free(elem_tok); }
        } else {
            if (eq_tok) free(eq_tok);
            free(elem_tok);
        }
    }
    return 0;
}
