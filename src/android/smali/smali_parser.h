#ifndef SMALI_PARSER_H
#define SMALI_PARSER_H

#include "smali_types.h"
#include "smali_lexer.h"

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text);
int parse_smali_file_path(smali_ctx_def_t *ctx, const char *filepath);
int smali_parse_method_body(smali_ctx_def_t *ctx, smali_method_def_t *m, char **p, smali_line_t *file_lc);

#endif
