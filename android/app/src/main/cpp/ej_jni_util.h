#ifndef EJ_JNI_UTIL_H
#define EJ_JNI_UTIL_H

#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include <android/asset_manager.h>

/* One bridge context for the whole .so: JNI entry points have nowhere else to
 * keep the asset manager and the app's private directory. */
typedef struct {
    AAssetManager *assets;
    char data_dir[512];
    char key_path[640];
    char cert_path[640];
    int keys_tried;
} ej_ctx_t;

ej_ctx_t *ej_ctx(void);
int ej_keys_ready(void);

/* UTF-8 -> java.lang.String (surrogate pairs handled, NULL -> ""). */
jstring ej_jstr(JNIEnv *env, const char *s);
jobjectArray ej_jstr_array(JNIEnv *env, const char **items, int n);
/* Pass/fail + message pair used by every "do the work" call. */
jstring ej_jresult(JNIEnv *env, int rc, const char *detail);

char *ej_dup(const char *s);
uint8_t *ej_read_file(const char *path, size_t *out_len, size_t max);
uint8_t *ej_read_zip_entry(const char *archive, const char *name, size_t *out_len);
int ej_write_file(const char *path, const void *data, size_t len);
const char *ej_basename(const char *path);
const char *ej_ext(const char *name);
int ej_ext_is(const char *name, const char *ext);

#endif
