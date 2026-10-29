/* Native and asset digest patching: many protected apps compare the APK
 * signature inside a .so (JNI) or a bundled asset, not in Java.  Every
 * occurrence of an old certificate representation is overwritten with the
 * new one (same encoding => same length, no size change). */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct native_ctx {
    sigkill_cert *cert;
    int patch;
    sigkill_report *r;
};

static uint8_t *find_bytes(uint8_t *buf, size_t len, const char *needle, size_t nl) {
    if (nl == 0 || len < nl) return NULL;
    for (size_t i = 0; i + nl <= len; i++)
        if (memcmp(buf + i, needle, nl) == 0) return buf + i;
    return NULL;
}

static int count_and_patch(uint8_t *buf, size_t len, const char *oldv,
                           const char *newv, int patch, int *out) {
    size_t ol = strlen(oldv), nl = strlen(newv);
    if (ol == 0 || ol != nl || ol > len) return 0;
    uint8_t *p = buf;
    size_t left = len;
    int n = 0;
    while (left >= ol) {
        uint8_t *hit = find_bytes(p, left, oldv, ol);
        if (!hit) break;
        n++;
        if (patch) memcpy(hit, newv, nl);
        size_t adv = (size_t)(hit - p) + ol;
        p += adv;
        left -= adv;
    }
    if (n) *out += n;
    return n;
}

static int native_file(const char *path, void *ud) {
    struct native_ctx *ctx = ud;
    if (!sk_has(path, "/lib/") && !sk_has(path, "/assets/")) return 0;
    ctx->r->native_files_scanned++;

    int fd = open(path, O_RDWR);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return 0; }
    size_t len = (size_t)st.st_size;
    uint8_t *buf = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) { close(fd); return 0; }

    int hits = 0;
    for (int i = 0; i < ctx->cert->rep_count; i++) {
        sigkill_rep *rep = &ctx->cert->reps[i];
        if (!rep->newv) continue;
        int n = 0;
        count_and_patch(buf, len, rep->oldv, rep->newv, ctx->patch, &n);
        if (n > 0) {
            hits += n;
            sk_detail(ctx->r, "native", path, "%s x%d%s", rep->kind, n,
                      ctx->patch ? " patched" : " found");
        }
    }
    if (hits > 0) {
        if (ctx->patch) {
            msync(buf, len, MS_SYNC);
            ctx->r->native_files_patched++;
        }
        ctx->r->native_hits += hits;
    }
    munmap(buf, len);
    close(fd);
    return 0;
}

int sk_native_patch(const char *tree, sigkill_cert *c, int patch,
                    sigkill_report *r) {
    struct native_ctx ctx = { c, patch, r };
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s/lib", tree);
    if (sk_is_dir(dir)) sk_walk(dir, native_file, &ctx);
    snprintf(dir, sizeof(dir), "%s/assets", tree);
    if (sk_is_dir(dir)) sk_walk(dir, native_file, &ctx);
    return 0;
}
