/* Chain every selected call-site transform over one method block. */
#include "sigkill_internal.h"
#include <stdlib.h>
#include <string.h>

/* Apply every selected call-site transform in sequence.  Stubbing above
 * replaces the whole body and wins; otherwise the transforms chain so that a
 * method with both a PackageManager lookup and a Signature.toCharsString()
 * call gets both hooks. */
char *sk_chain_transforms(const char *block, size_t len,
                              const sigkill_opts *o, sigkill_cert *c,
                              sigkill_report *r, const char *file) {
    char *work = NULL;
    const char *base = block;
    size_t blen = len;

#define SK_CHAIN(cond, call, counter, label)                               \
    do {                                                                   \
        if (cond) {                                                        \
            int sites = 0;                                                 \
            char *nb = (call);                                             \
            if (nb && sites > 0) {                                         \
                r->counter += sites;                                       \
                free(work);                                                \
                work = nb;                                                 \
                base = work;                                               \
                blen = strlen(work);                                       \
            } else {                                                       \
                free(nb);                                                  \
            }                                                              \
            (void)label;                                                   \
        }                                                                  \
    } while (0)

    SK_CHAIN(o->do_sigobj,
             sk_sigobj_inject_block(base, blen, c, r, file, &sites),
             sigobj_sites_patched, "sigobj");
    SK_CHAIN(o->do_checkapi,
             sk_checkapi_inject_block(base, blen, r, file, &sites),
             checkapi_sites_patched, "checkapi");
    SK_CHAIN(o->do_call,
             sk_call_rewrite_block(base, blen, r, file, &sites),
             call_sites_patched, "call");
    SK_CHAIN(o->do_pkg,
             sk_pkg_inject_block(base, blen, r, file, &sites),
             pkg_sites_patched, "pkg");
#undef SK_CHAIN

    return work;
}

