#include "smali_error.h"
#include <stdarg.h>
#include <stdio.h>

#define SMALI_MSG_MAX 512
#define SMALI_SRC_MAX 1024

static char s_msg[SMALI_MSG_MAX];
static char s_src[SMALI_SRC_MAX] = "<smali>";

int smali_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_msg, sizeof(s_msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "Error: %s\n", s_msg);
    return -1;
}

void smali_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_msg, sizeof(s_msg), fmt, ap);
    va_end(ap);
}

const char *smali_last_error(void) {
    return s_msg;
}

void smali_clear_error(void) {
    s_msg[0] = '\0';
}

void smali_set_source_name(const char *name) {
    snprintf(s_src, sizeof(s_src), "%s", name ? name : "<smali>");
}

const char *smali_source_name(void) {
    return s_src;
}
