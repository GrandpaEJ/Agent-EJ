/* Rebuild one smali invoke line into invoke-static, preserving the register
 * list; shared by the `call` and `sigobj` call-site rewrites. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>

char *sk_rewrite_invoke_static(const char *line, size_t len, const char *sig) {
    const char *inv = strstr(line, "invoke-virtual");
    if (!inv || (size_t)(inv - line) >= len) return NULL;
    const char *rest = inv + strlen("invoke-virtual");
    const char *suffix = "";
    if (strncmp(rest, "/range", 6) == 0) { suffix = "/range"; rest += 6; }

    const char *open = strchr(rest, '{');
    const char *close = open ? strchr(open, '}') : NULL;
    if (!open || !close) return NULL;
    const char *nl = memchr(close, '\n', len - (size_t)(close - line));
    size_t tail = nl ? (size_t)(nl - close + 1) : len - (size_t)(close - line);

    sk_buf b;
    sk_buf_init(&b);
    sk_buf_add(&b, line, (size_t)(inv - line));
    sk_buf_addf(&b, "invoke-static%s {", suffix);
    sk_buf_add(&b, open + 1, (size_t)(close - open - 1));
    sk_buf_addf(&b, "}, %s", sig);
    if (tail && (line[len - 1] == '\n' || close[tail - 1] == '\n'))
        sk_buf_add(&b, "\n", 1);
    return sk_buf_take(&b);
}
