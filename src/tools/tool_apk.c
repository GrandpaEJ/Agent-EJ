#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <unistd.h>
#endif

// Locate the default test key (matching the built-in certificate) so that
// zero-config signing works from any working directory.
static int resolve_default_key_path(char *out, size_t cap) {
    static const char *candidates[] = {
        "testkey.pem",       // legacy CWD-relative default
        "keys/testkey.pem",  // repo layout when run from the repo root
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(out, cap, "%s", candidates[i]);
            return 1;
        }
    }
#if defined(__linux__)
    // Fall back to the key shipped next to the executable.
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(out, cap, "%s/keys/testkey.pem", exe);
            struct stat st;
            if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) return 1;
        }
    }
#endif
    return 0;
}

// JSON error for signing failures, distinguishing cert/key mismatch (issue #13).
// The numeric "status" makes the CLI exit non-zero so shell callers can detect
// a failed signing attempt (see main.c tool-mode exit-code handling).
static char* sign_failure_json(int ret, const char *what) {
    char buf[512];
    if (ret == APK_SIGN_ERR_CERT_MISMATCH) {
        snprintf(buf, sizeof(buf),
                 "{\"status\": 1, \"error\": \"%s failed: certificate does not match the signing key; supply a matching cert= path\", \"code\": \"cert_key_mismatch\"}",
                 what);
    } else {
        snprintf(buf, sizeof(buf), "{\"status\": 1, \"error\": \"%s failed\"}", what);
    }
    return strdup(buf);
}

static int collect_files(const char *base, char ***paths, int *count, const char *root_dir) {
    DIR *d = opendir(base);
    struct dirent *ent;
    if (!d) return -1;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", base, ent->d_name);
        // Skip smali source directories at the root level
        if (strcmp(base, root_dir) == 0 && strncmp(ent->d_name, "smali", 5) == 0) {
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) continue;
        }
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            collect_files(p, paths, count, root_dir);
        } else {
            (*paths) = realloc(*paths, (*count + 1) * sizeof(char *));
            (*paths)[*count] = strdup(p);
            (*count)++;
        }
    }
    closedir(d);
    return 0;
}

char* execute_decode_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* out_dir_obj = cJSON_GetObjectItem(args, "out_dir");
    if (!path_obj || !cJSON_IsString(path_obj) || !out_dir_obj || !cJSON_IsString(out_dir_obj))
        return strdup("{\"error\": \"Missing path or out_dir\"}");
    
    int ret = apk_decode(path_obj->valuestring, out_dir_obj->valuestring);
    if (ret != 0) return strdup("{\"error\": \"Failed to decode APK\"}");
    
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"status\": \"success\", \"output_dir\": \"%s\"}", out_dir_obj->valuestring);
    return strdup(buf);
}

char* execute_build_apk(cJSON* args) {
    cJSON* src_dir_obj = cJSON_GetObjectItem(args, "src_dir");
    cJSON* out_apk_obj = cJSON_GetObjectItem(args, "out_apk");
    cJSON* key_path_obj = cJSON_GetObjectItem(args, "key_path");
    cJSON* cert_path_obj = cJSON_GetObjectItem(args, "cert_path");
    
    if (!src_dir_obj || !cJSON_IsString(src_dir_obj) || !out_apk_obj || !cJSON_IsString(out_apk_obj))
        return strdup("{\"error\": \"Missing src_dir or out_apk\"}");
        
    const char *key_path = (key_path_obj && cJSON_IsString(key_path_obj)) ? key_path_obj->valuestring : NULL;
    const char *cert_path = (cert_path_obj && cJSON_IsString(cert_path_obj)) ? cert_path_obj->valuestring : NULL;

    int ret = apk_build(src_dir_obj->valuestring, out_apk_obj->valuestring, key_path, cert_path);
    if (ret != 0) {
        if (ret == APK_SIGN_ERR_CERT_MISMATCH)
            return strdup("{\"error\": \"Failed to build APK: certificate does not match the signing key; supply a matching cert_path\", \"code\": \"cert_key_mismatch\"}");
        return strdup("{\"error\": \"Failed to build APK\"}");
    }
    
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"status\": \"success\", \"output_apk\": \"%s\"}", out_apk_obj->valuestring);
    return strdup(buf);
}

char* execute_analyze_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    char *dump = apk_analyze(path_obj->valuestring);
    if (!dump) return strdup("{\"error\": \"Failed to analyze APK\"}");
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "dump", dump);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    apk_free(dump);
    return res_str;
}

char* execute_repack_apk(cJSON* args) {
    cJSON* dir_obj = cJSON_GetObjectItem(args, "dir");
    cJSON* out_obj = cJSON_GetObjectItem(args, "output");
    if (!dir_obj || !cJSON_IsString(dir_obj) || !out_obj || !cJSON_IsString(out_obj))
        return strdup("{\"error\": \"Missing dir or output path\"}");
    zip_writer *zw = zip_writer_open(out_obj->valuestring);
    if (!zw) return strdup("{\"error\": \"Failed to open output APK\"}");
    char **files = NULL;
    int count = 0;
    collect_files(dir_obj->valuestring, &files, &count, dir_obj->valuestring);
    for (int i = 0; i < count; i++) {
        const char *path = files[i];
        const char *name = path + strlen(dir_obj->valuestring) + 1;
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *buf = malloc(sz);
        fread(buf, 1, sz, fp);
        fclose(fp);
        zip_writer_add(zw, name, buf, sz, 1, 0);
        free(buf);
        free(files[i]);
    }
    free(files);
    zip_writer_close(zw);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", out_obj->valuestring);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_resign_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"status\": 1, \"error\": \"Missing path\"}");
    
    char out_path[4096];
    snprintf(out_path, sizeof(out_path), "%s.signed", path_obj->valuestring);

    cJSON* cert_obj = cJSON_GetObjectItem(args, "cert");
    cJSON* key_obj = cJSON_GetObjectItem(args, "key");
    const char *cert_path = (cert_obj && cJSON_IsString(cert_obj)) ? cert_obj->valuestring : NULL;
    const char *key_path = (key_obj && cJSON_IsString(key_obj)) ? key_obj->valuestring : NULL;

    char default_key[4096];
    if (!key_path) {
        if (!resolve_default_key_path(default_key, sizeof(default_key)))
            return strdup("{\"status\": 1, \"error\": \"No key supplied and default keys/testkey.pem not found; pass key=\"}");
        key_path = default_key;
    }

    rsa_key key;
    if (rsa_load_key(key_path, &key) != 0) {
        char err[512];
        snprintf(err, sizeof(err), "{\"status\": 1, \"error\": \"Failed to load private key: %s\"}", key_path);
        return strdup(err);
    }
    
    uint8_t *custom_cert = NULL;
    size_t custom_cert_len = 0;
    
    if (cert_path) {
        FILE *f = fopen(cert_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            custom_cert_len = ftell(f);
            fseek(f, 0, SEEK_SET);
            custom_cert = malloc(custom_cert_len);
            if (custom_cert) {
                size_t read_bytes = fread(custom_cert, 1, custom_cert_len, f);
                (void)read_bytes;
            }
            fclose(f);
        } else {
            char err[512];
            snprintf(err, sizeof(err), "{\"status\": 1, \"error\": \"Failed to load custom certificate: %s\"}", cert_path);
            return strdup(err);
        }
    }

    cJSON* scheme_obj = cJSON_GetObjectItem(args, "scheme");
    const char *scheme = (scheme_obj && cJSON_IsString(scheme_obj)) ? scheme_obj->valuestring : "v1";
    int do_v1 = strstr(scheme, "v1") != NULL;
    int do_v2 = strstr(scheme, "v2") != NULL;
    int do_v3 = strstr(scheme, "v3") != NULL;
    if (!do_v1 && !do_v2 && !do_v3) do_v1 = 1; // default
    
    int needs_v2_v3 = do_v2 || do_v3;

    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.apk", path_obj->valuestring);
    
    if (do_v1) {
        int ret = apk_sign_v1(path_obj->valuestring, needs_v2_v3 ? tmp_path : out_path, &key, do_v2, do_v3, custom_cert, custom_cert_len);
        if (ret != 0) {
            remove(needs_v2_v3 ? tmp_path : out_path); // no half-signed output
            if (custom_cert) free(custom_cert);
            return sign_failure_json(ret, "Native APK v1 signing");
        }
    }
    
    if (needs_v2_v3) {
        const char *in_file = do_v1 ? tmp_path : path_obj->valuestring;
        int ret = apk_sign_v2_v3(in_file, out_path, &key, do_v2, do_v3, custom_cert, custom_cert_len);
        if (ret != 0) {
            remove(out_path);
            if (custom_cert) free(custom_cert);
            if (do_v1) remove(tmp_path);
            return sign_failure_json(ret, "Native APK v2/v3 signing");
        }
        if (do_v1) remove(tmp_path);
    }
    
    if (custom_cert) free(custom_cert);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddStringToObject(res, "output", out_path);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_zipalign(cJSON* args) {
    cJSON* in_obj = cJSON_GetObjectItem(args, "in");
    cJSON* out_obj = cJSON_GetObjectItem(args, "out");
    cJSON* align_obj = cJSON_GetObjectItem(args, "align");
    
    if (!in_obj || !cJSON_IsString(in_obj) || !out_obj || !cJSON_IsString(out_obj))
        return strdup("{\"error\": \"Missing 'in' or 'out' path\"}");
        
    int alignment = 4;
    if (align_obj && cJSON_IsNumber(align_obj)) {
        alignment = align_obj->valueint;
    }
    
    if (zipalign_file(in_obj->valuestring, out_obj->valuestring, alignment) == 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "status", "success");
        cJSON_AddStringToObject(res, "output", out_obj->valuestring);
        char *res_str = cJSON_PrintUnformatted(res);
        cJSON_Delete(res);
        return res_str;
    } else {
        return strdup("{\"error\": \"Failed to zipalign APK\"}");
    }
}

