/* Pool invariants checked right before a DEX is serialized (issue #38).
 *
 * The pool code satisfies these by construction (hash dedup at add time,
 * UTF-16 sort at build time); this pass turns a future regression there
 * into a named assembler error instead of a DEX that ART refuses with
 * "Out-of-order string_ids" or INSTALL_FAILED_INVALID_APK. O(n), no
 * allocation. */
#include "smali_pool.h"
#include "smali_error.h"

#include <stdint.h>

#define SHOWN 48  /* longest string echoed back in a diagnostic */

static int check_strings(const smali_pool_strings_t *s) {
    for (uint32_t i = 1; i < s->count; i++) {
        const char *prev = s->strings[i - 1], *cur = s->strings[i];
        int c = smali_utf16_cmp(prev, cur);
        if (c < 0) continue;
        return smali_fail("string pool %s at string_ids[%u]/[%u]: \"%.*s\" %s \"%.*s\"",
                          c == 0 ? "has a duplicate" : "is out of UTF-16 order",
                          i - 1, i, SHOWN, prev, c == 0 ? "==" : ">", SHOWN, cur);
    }
    return 0;
}

static int check_types(smali_ctx_def_t *ctx) {
    uint32_t prev = 0;
    for (uint32_t i = 0; i < ctx->types.count; i++) {
        const char *t = ctx->types.strings[i];
        uint32_t idx = smali_pool_find(&ctx->strings, t);
        if (idx == 0xFFFFFFFF)
            return smali_fail("type_ids[%u] \"%.*s\" has no entry in the string pool", i, SHOWN, t);
        if (i > 0 && idx <= prev)
            return smali_fail("type_ids %s at [%u]/[%u]: \"%.*s\" (string %u) after \"%.*s\" (string %u)",
                              idx == prev ? "has a duplicate" : "are not sorted by string index",
                              i - 1, i, SHOWN, t, idx, SHOWN, ctx->types.strings[i - 1], prev);
        prev = idx;
    }
    return 0;
}

int smali_validate_pool(smali_ctx_def_t *ctx) {
    if (check_strings(&ctx->strings) != 0) return -1;
    return check_types(ctx);
}
