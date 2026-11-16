/* Dex editor save (MT's hammer): rebuild one dex in memory with some classes
 * replaced by edited smali. The untouched classes go through the same
 * dex -> smali -> dex round trip the Build APK pipeline uses, one class at a
 * time, so only the parsed model (not every class's text) is held at once. */
#include "smali_parser.h"
#include "smali_pool.h"
#include "smali_writer.h"
#include "smali_order.h"
#include "smali_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct dex_ctx dex_ctx;
dex_ctx *dex_parse(const uint8_t *data, size_t size);
char *dex_to_smali_class(dex_ctx *ctx, uint32_t class_idx);
uint32_t dex_class_count(dex_ctx *ctx);
void dex_free(dex_ctx *ctx);
char *smali_rename(const char *text, const char *const *from, const char *const *to, int n);

/* Parse one class, renamed first when there are renames. */
static int parse_renamed(smali_ctx_def_t *ctx, const char *text, const char *const *from,
                         const char *const *to, int n) {
    if (n == 0) return parse_smali_file_content(ctx, text);
    char *r = smali_rename(text, from, to, n);
    int rc = r ? parse_smali_file_content(ctx, r) : -1;
    free(r);
    return rc;
}

static int skipped(const uint32_t *skip, int n, uint32_t idx) {
    for (int i = 0; i < n; i++)
        if (skip[i] == idx) return 1;
    return 0;
}

int smali_patch_dex(const uint8_t *dex, size_t len, const uint32_t *skip, int n_skip,
                    const char *const *texts, int n_texts, const char *const *ren_from,
                    const char *const *ren_to, int n_ren, const char *out_dex) {
    if (!dex || !out_dex) return -1;
    smali_clear_error();
    dex_ctx *dc = dex_parse(dex, len);
    if (!dc) {
        smali_fail("cannot parse the original dex");
        return -1;
    }

    smali_ctx_def_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    int ret = 0;
    uint32_t count = dex_class_count(dc);
    for (uint32_t i = 0; i < count && ret == 0; i++) {
        if (skipped(skip, n_skip, i)) continue;
        char *smali = dex_to_smali_class(dc, i);
        if (!smali) continue;
        if (parse_renamed(&ctx, smali, ren_from, ren_to, n_ren) != 0) {
            smali_fail("class #%u does not re-assemble", i);
            ret = -1;
        }
        free(smali);
    }
    dex_free(dc);

    for (int i = 0; i < n_texts && ret == 0; i++) {
        int errs = smali_error_count();
        if (parse_renamed(&ctx, texts[i], ren_from, ren_to, n_ren) != 0) {
            if (smali_error_count() == errs) smali_fail("edited class %d has a syntax error", i + 1);
            ret = -1;
        }
    }

    if (ret == 0 && ctx.class_count == 0) {
        smali_fail("no classes to assemble");
        ret = -1;
    }
    if (ret == 0) {
        smali_order_classes(&ctx);
        if (smali_next_dex_group(&ctx, 0) < ctx.class_count) {
            smali_fail("the edited dex no longer fits one dex (64K method/field limit)");
            ret = -2;
        } else {
            smali_pool_build_all(&ctx);
            ret = write_assembled_dex(&ctx, out_dex);
        }
    }
    smali_ctx_free(&ctx);
    return ret;
}
