#ifndef SMALI_LEXER_H
#define SMALI_LEXER_H

#include <stdint.h>
#include <stddef.h>

char *smali_next_token(char **p);
char *smali_parse_string_literal(char **p);
uint32_t smali_parse_register(const char *tok);
/* Parses a literal: optional "#", "-", decimal/hex/octal, optional L/s/t suffix; wraps to 64 bits. */
int64_t smali_parse_lit(const char *tok);

/* Finds the end of the current line without scanning the whole remaining buffer
 * (some libc strchr implementations measure the full string first, which is quadratic here). */
static inline char *smali_eol(char *s) {
    while (*s && *s != '\n') s++;
    return *s ? s : NULL;
}

#endif
