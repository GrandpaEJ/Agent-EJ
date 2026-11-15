#include "smali_error.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SMALI_MSG_MAX 512
#define SMALI_SRC_MAX 1024
#define SMALI_LOG_MAX (SMALI_DIAG_CAP * 256)

/* s_log holds every recorded diagnostic, one per line, up to SMALI_DIAG_CAP;
 * later ones are only counted so one bad file can't flood the report. */
static char s_log[SMALI_LOG_MAX];
static size_t s_log_len;
static int s_count;
static char s_src[SMALI_SRC_MAX] = "<smali>";

static void record(const char *msg, int echo) {
    s_count++;
    if (s_count > SMALI_DIAG_CAP) return;
    if (echo) fprintf(stderr, "Error: %s\n", msg);
    size_t n = strlen(msg);
    size_t room = sizeof(s_log) - s_log_len - 1;
    if (s_log_len && room) { s_log[s_log_len++] = '\n'; room--; }
    if (n > room) n = room;
    memcpy(s_log + s_log_len, msg, n);
    s_log_len += n;
    s_log[s_log_len] = '\0';
}

int smali_fail(const char *fmt, ...) {
    char msg[SMALI_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    record(msg, 1);
    return -1;
}

int smali_fail_at(uint32_t line, const char *fmt, ...) {
    char detail[SMALI_MSG_MAX], msg[SMALI_MSG_MAX + SMALI_SRC_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    snprintf(msg, sizeof(msg), "%s:%u: %s", s_src, (unsigned)line, detail);
    record(msg, 1);
    return -1;
}

void smali_set_error(const char *fmt, ...) {
    char msg[SMALI_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    record(msg, 0);
}

const char *smali_last_error(void) {
    static char out[SMALI_LOG_MAX + 64];
    if (s_count <= SMALI_DIAG_CAP) return s_log;
    snprintf(out, sizeof(out), "%s\n... %d more error(s) not shown", s_log, s_count - SMALI_DIAG_CAP);
    return out;
}

int smali_error_count(void) {
    return s_count;
}

void smali_clear_error(void) {
    s_log[0] = '\0';
    s_log_len = 0;
    s_count = 0;
}

void smali_set_source_name(const char *name) {
    snprintf(s_src, sizeof(s_src), "%s", name ? name : "<smali>");
}

const char *smali_source_name(void) {
    return s_src;
}
