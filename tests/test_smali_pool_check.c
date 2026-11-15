/* smali_validate_pool() must accept a built pool and reject each kind of
 * corruption with a named diagnostic (issue #38). Built and run by
 * tests/test_smali_pool_check.sh against the project's object files. */
#include "smali_pool.h"
#include "smali_error.h"

#include <stdio.h>
#include <string.h>

static int pass, fail;

static void expect(const char *name, int rc, int want_rc, const char *want_msg) {
    const char *msg = smali_last_error();
    int ok = rc == want_rc && (!want_msg || strstr(msg, want_msg));
    if (ok) { pass++; printf("  ok   - %s%s%s\n", name, want_msg ? ": " : "", want_msg ? msg : ""); }
    else { fail++; printf("  FAIL - %s (rc=%d, msg=\"%s\")\n", name, rc, msg); }
    smali_clear_error();
}

static void build(smali_ctx_def_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    smali_pool_init(&ctx->strings);
    smali_pool_init(&ctx->types);
    /* \xF0\x9F\x98\x80 (U+1F600) is a surrogate pair in UTF-16 and must sort
     * before U+E000 even though its UTF-8 bytes sort after. */
    const char *s[] = {"La;", "Lb;", "Lc;", "alpha", "\xEE\x80\x80", "\xF0\x9F\x98\x80"};
    for (unsigned i = 0; i < sizeof(s) / sizeof(*s); i++) smali_pool_add(&ctx->strings, s[i]);
    smali_pool_sort_strings(&ctx->strings);
    const char *t[] = {"La;", "Lb;", "Lc;"};
    for (unsigned i = 0; i < 3; i++) smali_pool_add(&ctx->types, t[i]);
    smali_pool_sort_strings(&ctx->types);
}

static void swap(char **v, int a, int b) { char *t = v[a]; v[a] = v[b]; v[b] = t; }

int main(void) {
    smali_ctx_def_t ctx;
    puts("== pool validator (issue #38) ==");

    build(&ctx);
    expect("sorted pool accepted", smali_validate_pool(&ctx), 0, NULL);
    expect("surrogate sorts before U+E000",
           smali_utf16_cmp("\xF0\x9F\x98\x80", "\xEE\x80\x80") < 0 ? 0 : -1, 0, NULL);

    swap(ctx.strings.strings, 3, 4);
    expect("strings out of order", smali_validate_pool(&ctx), -1, "out of UTF-16 order");
    swap(ctx.strings.strings, 3, 4);

    char *keep = ctx.strings.strings[4];
    ctx.strings.strings[4] = ctx.strings.strings[3];
    expect("duplicate string", smali_validate_pool(&ctx), -1, "has a duplicate");
    ctx.strings.strings[4] = keep;

    swap(ctx.types.strings, 0, 2);
    expect("types not sorted by string index", smali_validate_pool(&ctx), -1, "not sorted by string index");
    swap(ctx.types.strings, 0, 2);

    keep = ctx.types.strings[1];
    ctx.types.strings[1] = "Lmissing;";
    expect("type without string", smali_validate_pool(&ctx), -1, "has no entry in the string pool");
    ctx.types.strings[1] = keep;

    expect("restored pool accepted", smali_validate_pool(&ctx), 0, NULL);
    smali_pool_free(&ctx.strings);
    smali_pool_free(&ctx.types);
    printf("== pass=%d fail=%d ==\n", pass, fail);
    return fail != 0;
}
