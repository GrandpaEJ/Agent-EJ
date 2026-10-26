#include "smali_parser.h"
#include "smali_lexer.h"
#include "smali_pool.h"
#include "smali_error.h"
#include "smali_annot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Move an annotation into an annotation list. The struct is moved, not copied:
 * the pointers inside it belong to the list from now on. */
static void annot_push(smali_annotation_t **list, uint32_t *count, uint32_t *cap,
                       smali_annotation_t *src) {
    if (*count >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        *list = realloc(*list, *cap * sizeof(smali_annotation_t));
    }
    (*list)[(*count)++] = *src;
    memset(src, 0, sizeof(*src));
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

/* Normalises a method signature spec "name(params)return-type" into the DEX
 * form: the parameter types are concatenated without the separators the smali
 * source may use. The DEX signature has neither spaces nor commas, and a spec
 * that is truncated at the first space silently drops parameters ("a(I, I)V"
 * would become "a(I"). Returns a malloc'd string, or NULL when the spec is
 * not a signature at all. */
static char *normalize_method_spec(const char *spec) {
    const char *open = strchr(spec, '(');
    if (!open) return NULL;
    const char *close = strchr(open, ')');
    if (!close) return NULL;

    size_t name_len = (size_t)(open - spec);
    while (name_len > 0 && (spec[name_len - 1] == ' ' || spec[name_len - 1] == '\t'))
        name_len--;

    const char *rt = close + 1;
    while (*rt == ' ' || *rt == '\t') rt++;
    size_t rt_len = strlen(rt);
    while (rt_len > 0 && (rt[rt_len - 1] == ' ' || rt[rt_len - 1] == '\t' || rt[rt_len - 1] == '\r'))
        rt_len--;

    size_t plen = 0;
    for (const char *q = open + 1; q < close; q++)
        if (*q != ' ' && *q != '\t' && *q != ',') plen++;

    char *out = malloc(name_len + plen + rt_len + 3);
    if (!out) return NULL;
    memcpy(out, spec, name_len);
    out[name_len] = '(';
    size_t w = name_len + 1;
    for (const char *q = open + 1; q < close; q++) {
        if (*q == ' ' || *q == '\t' || *q == ',') continue;
        out[w++] = *q;
    }
    out[w++] = ')';
    memcpy(out + w, rt, rt_len);
    out[w + rt_len] = '\0';
    return out;
}

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text) {
    char *text_copy = strdup(text);
    char *p = text_copy;
    smali_class_def_t *curr = NULL;
    /* Source line of the construct being parsed (1-based). */
    smali_line_t lc;
    smali_line_init(&lc, text_copy, 1);

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        char *line_start = p;
        char *line_end = smali_eol(p);
        if (!line_end) line_end = p + strlen(p);

        /* The token starts here, so this is its source line. */
        smali_line_sync(&lc, p);
        uint32_t cur_line = lc.line;

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
            if (!curr->descriptor) {
                smali_fail_at(cur_line, "malformed .class directive (expected a type descriptor)");
                free(tok); free(text_copy);
                return -1;
            }
            free(tok); p = line_end; continue;
        } else if (strcmp(tok, ".super") == 0 && curr) {
            curr->super_class = smali_next_token(&p);
            free(tok); p = line_end; continue;
        } else if (strcmp(tok, ".source") == 0 && curr) {
            curr->source_file = smali_next_token(&p);
            free(tok); p = line_end; continue;
        } else if (strcmp(tok, ".implements") == 0 && curr) {
            char *iface = smali_next_token(&p);
            if (iface) {
                curr->interfaces = realloc(curr->interfaces, (curr->interface_count + 1) * sizeof(char *));
                curr->interfaces[curr->interface_count++] = iface;
            }
            free(tok); p = line_end; continue;
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
                if (!colon) {
                    smali_fail_at(cur_line, "malformed .field '%s' (expected name:type)", name_type);
                    free(name_type); free(tok); free(text_copy);
                    return -1;
                }
                {
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
                                            if (smali_parse_annot_value(&p, el, cur_line) != 0) {
                                                smali_annot_elem_free(el);
                                                free(inner_tok); free(tok); free(text_copy);
                                                return -1;
                                            }
                                            ann->elem_count++;
                                        } else { free(elem_tok); }
                                    } else {
                                        if (eq_tok) free(eq_tok);
                                        free(elem_tok);
                                    }
                                }
                                free(inner_tok);
                                /* "tok" belongs to the class level loop: it is
                                 * released there. Releasing it here as well
                                 * freed it twice and the second use read the
                                 * recycled memory. */
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
                /* The signature may contain spaces after the parameter commas
                 * ("a(I, I)V"), so the rest of the line completes the token
                 * that was read above. */
                size_t part_len = strlen(name_sig);
                size_t rest_len = (size_t)(line_end - p);
                char *raw = malloc(part_len + rest_len + 1);
                if (!raw) { free(name_sig); free(tok); free(text_copy); return -1; }
                memcpy(raw, name_sig, part_len);
                memcpy(raw + part_len, p, rest_len);
                raw[part_len + rest_len] = '\0';
                char *hash = strchr(raw, '#');
                if (hash) *hash = '\0';
                char *spec = normalize_method_spec(raw);
                free(raw);
                if (!spec) {
                    smali_fail_at(cur_line, "malformed .method '%s' (expected name(signature))", name_sig);
                    free(name_sig); free(tok); free(text_copy);
                    return -1;
                }
                free(name_sig);
                name_sig = spec;
                /* The method body starts on the following line. */
                p = line_end;
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
                    
                    if (smali_parse_method_body(ctx, method, &p, &lc) != 0) {
                        free(name_sig); free(tok); free(text_copy);
                        return -1;
                    }
                }
                free(name_sig);
            }
        } else if (strcmp(tok, ".annotation") == 0 && curr) {
            smali_annotation_t ann;
            memset(&ann, 0, sizeof(ann));
            ann.visibility = 1;
            if (smali_parse_annot_block(&p, &ann, cur_line) != 0) {
                smali_annot_free(&ann);
                free(tok); free(text_copy);
                return -1;
            }
            annot_push(&curr->annots, &curr->annot_count, &curr->annot_cap, &ann);
            free(tok);
            continue;
        }
        /* .method/.field bodies advance p themselves; rewinding would re-parse their annotations */
        int is_method = (strcmp(tok, ".method") == 0 || strcmp(tok, ".field") == 0);
        if (!is_method) {
            smali_fail_at(cur_line, "unknown directive '%s' at class level", tok);
            free(tok); free(text_copy);
            return -1;
        }
        free(tok);
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
    /* Diagnostics name the file they come from. */
    smali_set_source_name(filepath);
    int ret = parse_smali_file_content(ctx, buf);
    free(buf);
    return ret;
}
