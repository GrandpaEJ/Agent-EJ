/* Shared JNI helpers plus the one-time bridge context setup. */
#include "ej_jni_util.h"
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

static ej_ctx_t g_ctx;

ej_ctx_t *ej_ctx(void) { return &g_ctx; }

static int copy_asset(const char *asset, const char *dest) {
    if (!g_ctx.assets) return -1;
    AAsset *as = AAssetManager_open(g_ctx.assets, asset, AASSET_MODE_STREAMING);
    if (!as) return -1;
    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        AAsset_close(as);
        return -1;
    }
    char buf[4096];
    int n, ok = 1;
    while ((n = AAsset_read(as, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            ok = 0;
            break;
        }
    }
    fclose(f);
    AAsset_close(as);
    if (!ok) {
        remove(tmp);
        return -1;
    }
    return rename(tmp, dest);
}

int ej_keys_ready(void) {
    if (g_ctx.keys_tried) return g_ctx.key_path[0] != '\0';
    g_ctx.keys_tried = 1;
    if (!g_ctx.data_dir[0]) return 0;
    char kdir[560];
    snprintf(kdir, sizeof(kdir), "%s/keys", g_ctx.data_dir);
    mkdir(g_ctx.data_dir, 0755);
    mkdir(kdir, 0755);
    snprintf(g_ctx.key_path, sizeof(g_ctx.key_path), "%s/debug.pk8.pem", kdir);
    snprintf(g_ctx.cert_path, sizeof(g_ctx.cert_path), "%s/debug.cert.der", kdir);

    struct stat st;
    if (stat(g_ctx.key_path, &st) != 0 && copy_asset("keys/debug.pk8.pem", g_ctx.key_path) != 0) {
        g_ctx.key_path[0] = '\0';
        return 0;
    }
    if (stat(g_ctx.cert_path, &st) != 0 && copy_asset("keys/debug.cert.der", g_ctx.cert_path) != 0) {
        g_ctx.key_path[0] = '\0';
        return 0;
    }
    return 1;
}

jstring ej_jstr(JNIEnv *env, const char *s) {
    if (!s) return (*env)->NewStringUTF(env, "");
    size_t n = strlen(s);
    jchar *u = (jchar *)malloc((n + 1) * sizeof(jchar));
    if (!u) return (*env)->NewStringUTF(env, "");
    size_t i = 0, o = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (i < n) {
        unsigned c = p[i++];
        unsigned cp;
        if (c < 0x80) {
            u[o++] = (jchar)c;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i < n) {
            cp = ((c & 0x1F) << 6) | (p[i++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 1 < n) {
            cp = ((c & 0x0F) << 12) | ((p[i] & 0x3F) << 6) | (p[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF8) == 0xF0 && i + 2 < n) {
            cp = ((c & 0x07) << 18) | ((p[i] & 0x3F) << 12) |
                 ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            i += 3;
            if (cp > 0x10FFFF) cp = '?';
        } else {
            u[o++] = '?';
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            u[o++] = (jchar)(0xD800 + (cp >> 10));
            u[o++] = (jchar)(0xDC00 + (cp & 0x3FF));
        } else {
            u[o++] = (jchar)cp;
        }
    }
    jstring out = (*env)->NewString(env, u, (jsize)o);
    free(u);
    return out;
}

jobjectArray ej_jstr_array(JNIEnv *env, const char **items, int n) {
    jclass sc = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, n, sc, NULL);
    for (int i = 0; i < n; i++) {
        jstring s = ej_jstr(env, items[i]);
        (*env)->SetObjectArrayElement(env, arr, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    return arr;
}

jstring ej_jresult(JNIEnv *env, int rc, const char *detail) {
    char buf[512];
    if (detail && detail[0])
        snprintf(buf, sizeof(buf), "%d\t%s", rc, detail);
    else
        snprintf(buf, sizeof(buf), "%d", rc);
    return ej_jstr(env, buf);
}

char *ej_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

uint8_t *ej_read_file(const char *path, size_t *out_len, size_t max) {
    if (out_len) *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || (max && (size_t)n > max)) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *b = (uint8_t *)malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = '\0';
    if (out_len) *out_len = got;
    return b;
}

uint8_t *ej_read_zip_entry(const char *archive, const char *name, size_t *out_len) {
    if (out_len) *out_len = 0;
    zip_archive *za = zip_open(archive);
    if (!za) return NULL;
    int n = zip_get_num_entries(za);
    uint8_t *data = NULL;
    for (int i = 0; i < n; i++) {
        const char *en = zip_get_entry_name(za, i);
        if (en && strcmp(en, name) == 0) {
            data = (uint8_t *)zip_extract_entry(za, i, out_len);
            break;
        }
    }
    zip_close(za);
    return data;
}

int ej_write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t wrote = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return (wrote == len) ? 0 : -1;
}

const char *ej_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *ej_ext(const char *name) {
    const char *b = ej_basename(name);
    const char *dot = strrchr(b, '.');
    return (dot && dot[1]) ? dot + 1 : "";
}

int ej_ext_is(const char *name, const char *ext) {
    const char *e = ej_ext(name);
    if (!*e) return 0;
    return strcasecmp(e, ext) == 0;
}
