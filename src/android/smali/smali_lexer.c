#include "smali_lexer.h"
#include <string.h>
#include <stdlib.h>

/* Appends one UTF-16 code unit as Modified UTF-8 (NUL -> C0 80, surrogates as 3 bytes). */
static size_t put_mutf8(char *out, unsigned u) {
    if (u != 0 && u < 0x80) { out[0] = (char)u; return 1; }
    if (u < 0x800) { out[0] = (char)(0xC0 | (u >> 6)); out[1] = (char)(0x80 | (u & 0x3F)); return 2; }
    out[0] = (char)(0xE0 | (u >> 12)); out[1] = (char)(0x80 | ((u >> 6) & 0x3F)); out[2] = (char)(0x80 | (u & 0x3F));
    return 3;
}

char *smali_parse_string_literal(char **p) {
    char *end = *p;
    if (*end != '"') return NULL;
    end++;
    size_t cap = 256, len = 0;
    char *str = malloc(cap);
    while (*end && *end != '"') {
        if (len + 8 >= cap) { cap *= 2; str = realloc(str, cap); }
        unsigned char c = (unsigned char)*end;
        if (c == '\\' && end[1]) {
            end++;
            switch (*end) {
            case 'n': str[len++] = '\n'; break;
            case 't': str[len++] = '\t'; break;
            case 'r': str[len++] = '\r'; break;
            case 'b': str[len++] = '\b'; break;
            case 'f': str[len++] = '\f'; break;
            case 'u': {
                unsigned u = 0; int n = 0;
                while (n < 4 && end[1] && strchr("0123456789abcdefABCDEF", end[1])) {
                    char h = end[1];
                    u = (u << 4) | (unsigned)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                    end++; n++;
                }
                len += put_mutf8(str + len, u);
                break;
            }
            default: str[len++] = *end;
            }
        } else if (c >= 0xF0 && end[1] && end[2] && end[3]) {
            /* raw 4-byte UTF-8 -> surrogate pair */
            unsigned cp = ((c & 7u) << 18) | ((end[1] & 0x3Fu) << 12) | ((end[2] & 0x3Fu) << 6) | (end[3] & 0x3Fu);
            cp -= 0x10000;
            len += put_mutf8(str + len, 0xD800 + (cp >> 10));
            len += put_mutf8(str + len, 0xDC00 + (cp & 0x3FF));
            end += 3;
        } else {
            str[len++] = (char)c;
        }
        end++;
    }
    str[len] = '\0';
    if (*end == '"') end++;
    *p = end;
    return str;
}

char *smali_next_token(char **p) {
    char *start = *p;
    while (*start && (*start == ' ' || *start == '\t' || *start == ',' || *start == '\r')) {
        start++;
    }
    if (!*start || *start == '\n' || *start == '#') {
        *p = start;
        return NULL;
    }
    if (*start == '"') {
        char *tok = smali_parse_string_literal(&start);
        *p = start;
        return tok;
    }
    if (*start == '{') {
        char *end = start;
        int has_colon = 0;
        while (*end && *end != '}') {
            if (*end == ':') has_colon = 1;
            end++;
        }
        if (*end == '}') {
            end++;
            size_t len = end - start;
            char *tok = malloc(len + 1);
            memcpy(tok, start, len);
            tok[len] = '\0';
            *p = end;
            return tok;
        }
    }
    char *end = start;
    if (*end == ':') {
        end++;
    }
    while (*end && *end != ' ' && *end != '\t' && *end != ',' && *end != '\r' && *end != '\n' && *end != '#') {
        end++;
    }
    size_t len = end - start;
    char *tok = malloc(len + 1);
    memcpy(tok, start, len);
    tok[len] = '\0';
    *p = end;
    return tok;
}

uint32_t smali_parse_register(const char *tok) {
    if (!tok) return 0;
    while (*tok == ' ' || *tok == '\t') tok++;
    if (tok[0] == 'v') return (uint32_t)atoi(tok + 1);
    if (tok[0] == 'p') return (uint32_t)atoi(tok + 1) | 0x80000000;
    return 0;
}

int64_t smali_parse_lit(const char *tok) {
    if (!tok) return 0;
    if (*tok == '#') tok++;
    int neg = 0;
    if (*tok == '-') { neg = 1; tok++; }
    else if (*tok == '+') tok++;
    uint64_t v = strtoull(tok, NULL, 0);
    return (int64_t)(neg ? (uint64_t)0 - v : v);
}
