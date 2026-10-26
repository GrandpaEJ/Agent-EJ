#include "smali_parser.h"
#include "smali_lexer.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parse_annot_value(char **p, smali_annotation_elem_t *el) {
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '"') {
        el->value_type = VALUE_TYPE_STRING;
        el->value_str = smali_parse_string_literal(p);
        if (!el->value_str) el->value_str = strdup("");
        return;
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
            parse_annot_value(p, &node->elem);
            node->next = NULL;
            if (el->arr_tail) el->arr_tail->next = node;
            else el->arr_head = node;
            el->arr_tail = node;
            el->array_count++;
        }
        return;
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
        return;
    }
    char *tok = smali_next_token(p);
    if (!tok) { el->value_type = VALUE_TYPE_NULL; return; }
    if (strcmp(tok, "null") == 0) { el->value_type = VALUE_TYPE_NULL; free(tok); return; }
    if (strcmp(tok, "true") == 0) { el->value_type = VALUE_TYPE_BOOL; el->value_int = 1; free(tok); return; }
    if (strcmp(tok, "false") == 0) { el->value_type = VALUE_TYPE_BOOL; el->value_int = 0; free(tok); return; }
    if (strcmp(tok, ".enum") == 0) {
        free(tok);
        el->value_type = VALUE_TYPE_ENUM;
        el->value_str = smali_next_token(p);
        return;
    }
    if (tok[0] == 'L' || tok[0] == '[') {
        char *arrow = strstr(tok, "->");
        if (arrow && strchr(arrow, '(')) {
            el->value_type = VALUE_TYPE_METHOD;
            el->value_str = tok;
        } else if (arrow && strchr(arrow, ':')) {
            el->value_type = VALUE_TYPE_FIELD;
            el->value_str = tok;
        } else {
            el->value_type = VALUE_TYPE_TYPE;
            el->value_str = tok;
        }
        return;
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
                parse_annot_value(p, se);
                el->annot_elem_count++;
            } else { if (eq) free(eq); free(et); }
        }
        return;
    }
    size_t tl = strlen(tok);
    if (strncmp(tok + (tok[0] == '-'), "0x", 2) != 0 && tl > 1 && (tok[tl - 1] == 'f' || tok[tl - 1] == 'd')) {
        el->value_type = tok[tl - 1] == 'd' ? VALUE_TYPE_DOUBLE : VALUE_TYPE_FLOAT;
        el->value_double = strtod(tok, NULL);
    } else {
        char *end = NULL;
        el->value_int = (int64_t)smali_parse_lit(tok);
        (void)end;
        el->value_type = tok[tl - 1] == 'L' ? VALUE_TYPE_LONG
                       : tok[tl - 1] == 's' ? VALUE_TYPE_SHORT
                       : tok[tl - 1] == 't' ? VALUE_TYPE_BYTE : VALUE_TYPE_INT;
    }
    free(tok);
}

static uint32_t parse_access_flags(const char *tok) {
    if (strcmp(tok, "public") == 0) return 0x0001;
    if (strcmp(tok, "private") == 0) return 0x0002;
    if (strcmp(tok, "protected") == 0) return 0x0004;
    if (strcmp(tok, "static") == 0) return 0x0008;
    if (strcmp(tok, "final") == 0) return 0x0010;
    if (strcmp(tok, "synchronized") == 0) return 0x0020;
    if (strcmp(tok, "bridge") == 0) return 0x0040;
    if (strcmp(tok, "varargs") == 0) return 0x0080;
    if (strcmp(tok, "native") == 0) return 0x0100;
    if (strcmp(tok, "interface") == 0) return 0x0200;
    if (strcmp(tok, "abstract") == 0) return 0x0400;
    if (strcmp(tok, "synthetic") == 0) return 0x1000;
    if (strcmp(tok, "annotation") == 0) return 0x2000;
    if (strcmp(tok, "enum") == 0) return 0x4000;
    if (strcmp(tok, "constructor") == 0) return 0x10000;
    if (strcmp(tok, "declared-synchronized") == 0) return 0x20000;
    if (strcmp(tok, "volatile") == 0) return 0x0040;
    if (strcmp(tok, "transient") == 0) return 0x0080;
    return 0;
}

static uint32_t calculate_ins_count(const char *sig, uint32_t access_flags) {
    uint32_t count = 0;
    if (!(access_flags & 0x0008)) {
        count++; // 'this' pointer
    }
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        if (*p == 'J' || *p == 'D') {
            count += 2;
            p++;
        } else if (*p == 'L') {
            count += 1;
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            count += 1;
            while (*p == '[') p++;
            if (*p == 'L') {
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else {
                p++;
            }
        } else {
            count += 1;
            p++;
        }
    }
    return count;
}

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text) {
    char *text_copy = strdup(text);
    char *p = text_copy;
    smali_class_def_t *curr = NULL;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        char *line_start = p;
        char *line_end = smali_eol(p);
        if (!line_end) line_end = p + strlen(p);
        
        if (*line_start == '#') {
            p = line_end;
            continue;
        }

        char *tok = smali_next_token(&p);
        if (!tok) {
            p = line_end;
            continue;
        }
        /* "tok" owns the dispatch token; branches that consume it set the
         * pointer to NULL so that it is released exactly once. */

        if (strcmp(tok, ".class") == 0) {
            if (ctx->class_count >= ctx->class_cap) {
                ctx->class_cap = ctx->class_cap ? ctx->class_cap * 2 : 16;
                ctx->classes = realloc(ctx->classes, ctx->class_cap * sizeof(smali_class_def_t));
            }
            curr = &ctx->classes[ctx->class_count++];
            memset(curr, 0, sizeof(smali_class_def_t));
            
            char *next = smali_next_token(&p);
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    curr->access_flags |= flags;
                    free(next);
                } else {
                    curr->descriptor = next;
                    break;
                }
                next = smali_next_token(&p);
            }
        } else if (strcmp(tok, ".super") == 0 && curr) {
            curr->super_class = smali_next_token(&p);
        } else if (strcmp(tok, ".source") == 0 && curr) {
            curr->source_file = smali_next_token(&p);
        } else if (strcmp(tok, ".implements") == 0 && curr) {
            char *iface = smali_next_token(&p);
            if (iface) {
                curr->interfaces = realloc(curr->interfaces, (curr->interface_count + 1) * sizeof(char *));
                curr->interfaces[curr->interface_count++] = iface;
            }
        } else if (strcmp(tok, ".field") == 0 && curr) {
            uint32_t access_flags = 0;
            char *next = smali_next_token(&p);
            char *name_type = NULL;
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    access_flags |= flags;
                    free(next);
                } else {
                    name_type = next;
                    break;
                }
                next = smali_next_token(&p);
            }
            if (name_type) {
                char *colon = strchr(name_type, ':');
                if (colon) {
                    *colon = '\0';
                    char *name = strdup(name_type);
                    char *type = strdup(colon + 1);
                    smali_field_def_t *field;
                    if (access_flags & 0x0008) {
                        if (curr->static_field_count >= curr->static_field_cap) {
                            curr->static_field_cap = curr->static_field_cap ? curr->static_field_cap * 2 : 8;
                            curr->static_fields = realloc(curr->static_fields, curr->static_field_cap * sizeof(smali_field_def_t));
                        }
                        field = &curr->static_fields[curr->static_field_count++];
                    } else {
                        if (curr->instance_field_count >= curr->instance_field_cap) {
                            curr->instance_field_cap = curr->instance_field_cap ? curr->instance_field_cap * 2 : 8;
                            curr->instance_fields = realloc(curr->instance_fields, curr->instance_field_cap * sizeof(smali_field_def_t));
                        }
                        field = &curr->instance_fields[curr->instance_field_count++];
                    }
                    memset(field, 0, sizeof(smali_field_def_t));
                    int has_init = 0;
                    int val_type = VALUE_TYPE_INT;
                    int64_t val_int = 0;
                    double val_double = 0.0;
                    char *val_str = NULL;
                    char *eq_tok = smali_next_token(&p);
                    if (eq_tok && strcmp(eq_tok, "=") == 0) {
                        free(eq_tok);
                        /* peek at raw source to detect quoted strings */
                        while (*p == ' ' || *p == '\t') p++;
                        int is_quoted = (*p == '"');
                        char *val_tok = NULL;
                        if (*p == '\'') {
                            /* char literal: 'a', '\\n', '\\u00e9' */
                            char *q = p + 1;
                            unsigned cv = (unsigned char)*q;
                            if (*q == '\\') {
                                q++;
                                if (*q == 'u') { cv = (unsigned)strtoul(q + 1, &q, 16); }
                                else { cv = *q == 'n' ? '\n' : *q == 't' ? '\t' : *q == 'r' ? '\r' : *q == 'b' ? '\b' : *q == 'f' ? '\f' : (unsigned char)*q; q++; }
                            } else q++;
                            if (*q == '\'') q++;
                            p = q;
                            val_type = VALUE_TYPE_CHAR; val_int = cv; has_init = 1;
                        } else val_tok = smali_next_token(&p);
                        if (val_tok) {
                            if (strcmp(val_tok, "null") == 0) {
                                val_type = VALUE_TYPE_NULL; free(val_tok);
                                // null initializers on reference types are redundant (default value)
                                // only treat as init value for non-reference types
                                if (type && (type[0] != 'L' && type[0] != '[')) {
                                    has_init = 1;
                                }
                            } else if (strcmp(val_tok, "true") == 0) {
                                val_type = VALUE_TYPE_BOOL; val_int = 1; has_init = 1; free(val_tok);
                            } else if (strcmp(val_tok, "false") == 0) {
                                val_type = VALUE_TYPE_BOOL; val_int = 0; has_init = 1; free(val_tok);
                            } else if (is_quoted) {
                                val_type = VALUE_TYPE_STRING; val_str = val_tok; has_init = 1;
                            } else if (strcmp(val_tok, ".enum") == 0) {
                                val_type = VALUE_TYPE_ENUM; free(val_tok);
                                val_str = smali_next_token(&p);
                                if (val_str) has_init = 1;
                            } else if (strncmp(val_tok + (val_tok[0] == '-'), "0x", 2) != 0 &&
                                       (val_tok[strlen(val_tok) - 1] == 'f' || val_tok[strlen(val_tok) - 1] == 'd')) {
                                if (val_tok[strlen(val_tok) - 1] == 'd') {
                                    val_type = VALUE_TYPE_DOUBLE;
                                } else {
                                    val_type = VALUE_TYPE_FLOAT;
                                }
                                val_double = strtod(val_tok, NULL);
                                has_init = 1; free(val_tok);
                            } else if (val_tok[0] == 'L' || val_tok[0] == '[') {
                                val_type = VALUE_TYPE_TYPE; val_str = val_tok; has_init = 1;
                            } else {
                                char *end = NULL;
                                val_int = strtoll(val_tok, &end, 0);
                                if (end && *end == 'L') val_type = VALUE_TYPE_LONG;
                                else if (end && *end == 's') val_type = VALUE_TYPE_SHORT;
                                else if (end && *end == 't') val_type = VALUE_TYPE_BYTE;
                                has_init = 1; free(val_tok);
                            }
                        }
                    } else if (eq_tok) {
                        free(eq_tok);
                    }
                    field->access_flags = access_flags;
                    field->name = name;
                    field->type = type;
                    field->has_init_value = has_init;
                    field->value_type = val_type;
                    field->value_int = val_int;
                    field->value_double = val_double;
                    field->value_str = val_str;
                    field->array_vals = NULL;
                    field->array_count = 0;
                    field->annot_count = 0;
                    
                    /* Parse optional field body for annotations */
                    while (*p) {
                        char *save_p = p;
                        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                        if (!*p) break;
                        if (*p == '#') { char *nl = smali_eol(p); p = nl ? nl + 1 : p + strlen(p); continue; }
                        char *inner_tok = smali_next_token(&p);
                        if (!inner_tok) break;
                        if (strcmp(inner_tok, ".end") == 0) {
                            char *sub = smali_next_token(&p);
                            if (sub && strcmp(sub, "field") == 0) {
                                free(sub); free(inner_tok); break;
                            }
                            if (sub) free(sub);
                            p = save_p; free(inner_tok); break;
                        }
                        if (strcmp(inner_tok, ".annotation") == 0) {
                            if (field->annot_count >= field->annot_cap) {
                                field->annot_cap = field->annot_cap ? field->annot_cap * 2 : 4;
                                field->annots = realloc(field->annots, field->annot_cap * sizeof(smali_annotation_t));
                            }
                            smali_annotation_t *ann = &field->annots[field->annot_count++];
                            memset(ann, 0, sizeof(*ann));
                            ann->visibility = 1;
                                char *vis_tok = smali_next_token(&p);
                                if (vis_tok) {
                                    if (strcmp(vis_tok, "runtime") == 0) ann->visibility = 1;
                                    else if (strcmp(vis_tok, "build") == 0) ann->visibility = 0;
                                    else if (strcmp(vis_tok, "system") == 0) ann->visibility = 2;
                                    else { ann->type = vis_tok; vis_tok = NULL; }
                                    if (vis_tok) free(vis_tok);
                                }
                                if (!ann->type) ann->type = smali_next_token(&p);
                                while (*p) {
                                    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                                    if (*p == '#') { char *nl = smali_eol(p); p = nl ? nl + 1 : p + strlen(p); continue; }
                                    char *elem_tok = smali_next_token(&p);
                                    if (!elem_tok) break;
                                    if (strcmp(elem_tok, ".end") == 0) { char *at = smali_next_token(&p); if (at) free(at); free(elem_tok); break; }
                                    char *eq_tok = smali_next_token(&p);
                                    if (eq_tok && strcmp(eq_tok, "=") == 0) {
                                        free(eq_tok);
                                        if (ann->elem_count < MAX_ANNOT_ELEMS) {
                                            smali_annotation_elem_t *el = &ann->elems[ann->elem_count];
                                            memset(el, 0, sizeof(*el));
                                            el->name = elem_tok;
                                            parse_annot_value(&p, el);
                                            ann->elem_count++;
                                        } else { free(elem_tok); }
                                    } else {
                                        if (eq_tok) free(eq_tok);
                                        free(elem_tok);
                                    }
                                }
                                free(inner_tok);
                                free(tok);
                                continue;
                        }
                        p = save_p;
                        free(inner_tok);
                        break;
                    }
                }
                free(name_type);
            }
        } else if (strcmp(tok, ".method") == 0 && curr) {
            uint32_t access_flags = 0;
            char *next = smali_next_token(&p);
            char *name_sig = NULL;
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    access_flags |= flags;
                    free(next);
                } else {
                    name_sig = next;
                    break;
                }
                next = smali_next_token(&p);
            }
            if (name_sig) {
                char *paren = strchr(name_sig, '(');
                if (paren) {
                    char *name = strndup(name_sig, paren - name_sig);
                    // Trim carriage returns or spaces from name
                    size_t name_l = strlen(name);
                    while (name_l > 0 && (name[name_l - 1] == '\r' || name[name_l - 1] == ' ' || name[name_l - 1] == '\t')) {
                        name[name_l - 1] = '\0';
                        name_l--;
                    }
                    char *sig = strdup(paren);
                    // Trim carriage returns or spaces from sig
                    size_t sig_l = strlen(sig);
                    while (sig_l > 0 && (sig[sig_l - 1] == '\r' || sig[sig_l - 1] == ' ' || sig[sig_l - 1] == '\t')) {
                        sig[sig_l - 1] = '\0';
                        sig_l--;
                    }
                    smali_method_def_t *method;
                    int is_direct = ((access_flags & 0x0008) || (access_flags & 0x0002) || strcmp(name, "<init>") == 0 || strcmp(name, "<clinit>") == 0 || (access_flags & 0x10000));
                    if (is_direct) {
                        if (curr->direct_method_count >= curr->direct_method_cap) {
                            curr->direct_method_cap = curr->direct_method_cap ? curr->direct_method_cap * 2 : 8;
                            curr->direct_methods = realloc(curr->direct_methods, curr->direct_method_cap * sizeof(smali_method_def_t));
                        }
                        method = &curr->direct_methods[curr->direct_method_count++];
                    } else {
                        if (curr->virtual_method_count >= curr->virtual_method_cap) {
                            curr->virtual_method_cap = curr->virtual_method_cap ? curr->virtual_method_cap * 2 : 8;
                            curr->virtual_methods = realloc(curr->virtual_methods, curr->virtual_method_cap * sizeof(smali_method_def_t));
                        }
                        method = &curr->virtual_methods[curr->virtual_method_count++];
                    }
                    memset(method, 0, sizeof(smali_method_def_t));
                    method->access_flags = access_flags;
                    method->name = name;
                    method->signature = sig;
                    method->ins_count = calculate_ins_count(sig, method->access_flags);
                    
                    smali_parse_method_body(ctx, method, &p);
                }
                free(name_sig);
            }
        } else if (strcmp(tok, ".annotation") == 0 && curr) {
            if (curr->annot_count >= curr->annot_cap) {
                curr->annot_cap = curr->annot_cap ? curr->annot_cap * 2 : 4;
                curr->annots = realloc(curr->annots, curr->annot_cap * sizeof(smali_annotation_t));
            }
            smali_annotation_t *ann = &curr->annots[curr->annot_count++];
            memset(ann, 0, sizeof(*ann));
            ann->visibility = 1;
                char *vis_tok = smali_next_token(&p);
                if (vis_tok) {
                    if (strcmp(vis_tok, "runtime") == 0) ann->visibility = 1;
                    else if (strcmp(vis_tok, "build") == 0) ann->visibility = 0;
                    else if (strcmp(vis_tok, "system") == 0) ann->visibility = 2;
                    else { ann->type = vis_tok; vis_tok = NULL; }
                    if (vis_tok) free(vis_tok);
                }
                if (!ann->type) ann->type = smali_next_token(&p);
                while (*p) {
                    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                    if (*p == '#') { char *nl = smali_eol(p); p = nl ? nl + 1 : p + strlen(p); continue; }
                    char *elem_tok = smali_next_token(&p);
                    if (!elem_tok) break;
                    if (strcmp(elem_tok, ".end") == 0) { char *at = smali_next_token(&p); if (at) free(at); free(elem_tok); break; }
                    char *eq_tok = smali_next_token(&p);
                    if (eq_tok && strcmp(eq_tok, "=") == 0) {
                        free(eq_tok);
                        if (ann->elem_count < MAX_ANNOT_ELEMS) {
                            smali_annotation_elem_t *el = &ann->elems[ann->elem_count];
                            memset(el, 0, sizeof(*el));
                            el->name = elem_tok;
                            parse_annot_value(&p, el);
                            ann->elem_count++;
                        } else { free(elem_tok); }
                    } else {
                        if (eq_tok) free(eq_tok);
                        free(elem_tok);
                    }
                }
            free(tok);
            continue;
        }
        /* .method/.field bodies advance p themselves; rewinding would re-parse their annotations */
        int is_method = (strcmp(tok, ".method") == 0 || strcmp(tok, ".field") == 0);
        free(tok);        if (!is_method) {
            p = line_end;
        }
    }
    free(text_copy);
    return 0;
}

int parse_smali_file_path(smali_ctx_def_t *ctx, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    size_t read_bytes = fread(buf, 1, sz, fp);
    buf[read_bytes] = '\0';
    fclose(fp);
    int ret = parse_smali_file_content(ctx, buf);
    free(buf);
    return ret;
}
