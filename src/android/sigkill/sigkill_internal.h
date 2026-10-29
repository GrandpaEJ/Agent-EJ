#ifndef SIGKILL_INTERNAL_H
#define SIGKILL_INTERNAL_H

#include "sigkill.h"
#include <stdio.h>

/* --- growable string buffer ------------------------------------------- */
typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} sk_buf;

void sk_buf_init(sk_buf *b);
int  sk_buf_add(sk_buf *b, const char *s, size_t n);
int  sk_buf_adds(sk_buf *b, const char *s);
int  sk_buf_addf(sk_buf *b, const char *fmt, ...);
char *sk_buf_take(sk_buf *b);

/* --- files / walking --------------------------------------------------- */
int   sk_is_dir(const char *path);
char *sk_read_file(const char *path, size_t *len);
int   sk_write_file(const char *path, const void *data, size_t len);
int   sk_walk(const char *root, int (*fn)(const char *path, void *ud), void *ud);
void  sk_detail(sigkill_report *r, const char *type, const char *file,
                const char *fmt, ...);
int   sk_mkdir_p(const char *path);

/* Replace every occurrence of oldv by newv inside a NUL-terminated buffer.
 * The buffer is reallocated as needed; *len is updated. Returns count. */
int sk_replace_all(char **text, size_t *len, const char *oldv, const char *newv);

int sk_has(const char *s, const char *needle);
int sk_icontains(const char *hay, const char *needle);

/* --- digests ----------------------------------------------------------- */
int  sk_md5(const void *data, size_t len, uint8_t out[16]);
void sk_hex(const uint8_t *d, size_t n, char *out, int upper);
int  sk_b64_decode(const char *in, size_t inlen, uint8_t **out, size_t *outlen);
int  sk_hashcode(const uint8_t *d, size_t n, char *out, size_t cap);

/* --- certificate discovery -------------------------------------------- */
int sk_find_cert_der(const uint8_t *buf, size_t len,
                     const uint8_t **cert, size_t *clen);
int cert_from_apk(sigkill_cert *c, const char *apk_path);
int cert_from_tree(sigkill_cert *c, const char *tree_dir);
int sk_cert_from_blob(sigkill_cert *c, const uint8_t *blob, size_t len,
                      const char *source);

/* --- smali processing -------------------------------------------------- */
/* Rewrites a whole .smali file: stub detected check methods, inject package
 * manager hooks, then rewrite certificate constants. *stubbed receives the
 * number of neutralised methods. Returns a malloc'd buffer or NULL when the
 * file was not changed (caller keeps the original). */
char *sk_process_smali(const char *text, size_t len, const sigkill_opts *o,
                       sigkill_cert *c, sigkill_report *r, const char *file,
                       int *stubbed);

/* Inject hook calls inside one method block. Returns malloc'd text. */
char *sk_pkg_inject_block(const char *block, size_t len, sigkill_report *r,
                          const char *file, int *sites);

/* Neutralise one detected method block. */
char *sk_stub_block(const char *block, size_t len, const char *file,
                    const char *name, const char *ret,
                    sigkill_cert *c, sigkill_report *r);

/* Generate the runtime hook class source. */
char *sk_helper_smali(const sigkill_cert *c, size_t *len);

/* --- native / asset digest patching ------------------------------------ */
int sk_native_patch(const char *tree, sigkill_cert *c,
                    int patch, sigkill_report *r);

#endif /* SIGKILL_INTERNAL_H */
