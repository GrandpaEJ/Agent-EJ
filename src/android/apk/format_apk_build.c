#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#if defined(_WIN32)
#define MKDIR(path) mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

static int util_make_dirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            MKDIR(tmp);
            *p = '/';
        }
    }
    return MKDIR(tmp);
}

int apk_decode(const char *apk_path, const char *out_dir) {
    zip_archive *za = zip_open(apk_path);
    if (!za) return -1;
    
    if (zip_extract_all(za, out_dir) != 0) {
        zip_close(za);
        return -1;
    }
    zip_close(za);

    DIR *d = opendir(out_dir);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "classes", 7) == 0 && strstr(ent->d_name, ".dex") != NULL) {
            char dex_path[1024];
            snprintf(dex_path, sizeof(dex_path), "%s/%s", out_dir, ent->d_name);
            
            char smali_dir[1024];
            char num[32] = {0};
            strncpy(num, ent->d_name + 7, strlen(ent->d_name) - 11);
            snprintf(smali_dir, sizeof(smali_dir), "%s/smali_classes%s", out_dir, num);
            
            FILE *fp = fopen(dex_path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                uint8_t *buf = malloc(sz);
                if (buf) {
                    if (fread(buf, 1, sz, fp) == (size_t)sz) {
                        dex_ctx *dx = dex_parse(buf, sz);
                        if (dx) {
                            uint32_t ci = 0;
                            while (1) {
                                char *smali = dex_to_smali_class(dx, ci);
                                if (!smali) break;
                                
                                char *class_line = strdup(smali);
                                char *nl = strchr(class_line, '\n');
                                if (nl) *nl = '\0';
                                char *semi = strchr(class_line, ';');
                                if (semi) {
                                    *semi = '\0';
                                    char *desc = strrchr(class_line, 'L');
                                    if (desc) {
                                        desc++;
                                        char filepath[1024];
                                        snprintf(filepath, sizeof(filepath), "%s/%s.smali", smali_dir, desc);
                                        char dirpath[1024];
                                        snprintf(dirpath, sizeof(dirpath), "%s/%s.smali", smali_dir, desc);
                                        char *slash = strrchr(dirpath, '/');
                                        if (slash) {
                                            *slash = '\0';
                                            util_make_dirs(dirpath);
                                        }
                                        FILE *out_fp = fopen(filepath, "w");
                                        if (out_fp) {
                                            fputs(smali, out_fp);
                                            fclose(out_fp);
                                        }
                                    }
                                }
                                free(class_line);
                                free(smali);
                                ci++;
                            }
                            dex_free(dx);
                        }
                    }
                    free(buf);
                }
                fclose(fp);
            }
            remove(dex_path);
        }
    }
    closedir(d);
    
    // Decode AXML files and generate resources using ARSC context
    arsc_decode_apk(out_dir);
    
    return 0;
}

static int util_collect_files(const char *base, char ***paths, int *count, const char *root_dir) {
    DIR *d = opendir(base);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", base, ent->d_name);
        
        if (strcmp(base, root_dir) == 0 && strncmp(ent->d_name, "smali", 5) == 0) {
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
                continue;
            }
        }
        
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            util_collect_files(p, paths, count, root_dir);
        } else {
            (*paths) = realloc(*paths, (*count + 1) * sizeof(char *));
            (*paths)[*count] = strdup(p);
            (*count)++;
        }
    }
    closedir(d);
    return 0;
}

/* "smali" sorts first, then smali_classesN by N */
static int smali_dir_rank(const char *name) {
    return strncmp(name, "smali_classes", 13) == 0 ? atoi(name + 13) : 1;
}

static int cmp_smali_dirs(const void *a, const void *b) {
    int ra = smali_dir_rank(*(char *const *)a), rb = smali_dir_rank(*(char *const *)b);
    return ra != rb ? (ra < rb ? -1 : 1) : strcmp(*(char *const *)a, *(char *const *)b);
}

int apk_build(const char *src_dir, const char *out_apk, const char *key_path, const char *cert_path) {
    DIR *d = opendir(src_dir);
    if (!d) return -1;
    char **dex_files = NULL;
    int dex_count = 0;
    struct dirent *ent;
    /* collect smali, smali_classes2, smali_classes3 ... in dex order */
    char **dirs = NULL;
    int dir_count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "smali", 5) != 0) continue;
        char smali_dir[1024];
        snprintf(smali_dir, sizeof(smali_dir), "%s/%s", src_dir, ent->d_name);
        struct stat st;
        if (stat(smali_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            dirs = realloc(dirs, (dir_count + 1) * sizeof(char *));
            dirs[dir_count++] = strdup(ent->d_name);
        }
    }
    closedir(d);
    qsort(dirs, dir_count, sizeof(char *), cmp_smali_dirs);

    /* classes.dex, classes2.dex ... numbered across all dirs, since one dir may spill over */
    int next_idx = 1;
    for (int i = 0; i < dir_count; i++) {
        char smali_dir[1024], dex_path[1024], dex_name[64];
        snprintf(smali_dir, sizeof(smali_dir), "%s/%s", src_dir, dirs[i]);
        if (next_idx == 1) snprintf(dex_name, sizeof(dex_name), "classes.dex");
        else snprintf(dex_name, sizeof(dex_name), "classes%d.dex", next_idx);
        snprintf(dex_path, sizeof(dex_path), "%s/%s", src_dir, dex_name);
        int written = 0;
        if (smali_assemble_n(smali_dir, dex_path, &written) == 0) {
            for (int k = 0; k < written; k++) {
                char part[1024];
                if (k == 0) snprintf(part, sizeof(part), "%s", dex_path);
                else snprintf(part, sizeof(part), "%s/classes%d.dex", src_dir, next_idx + k);
                dex_files = realloc(dex_files, (dex_count + 1) * sizeof(char *));
                dex_files[dex_count++] = strdup(part);
            }
            next_idx += written;
        }
        free(dirs[i]);
    }
    free(dirs);

    char tmp_unaligned[1024];
    snprintf(tmp_unaligned, sizeof(tmp_unaligned), "%s.unaligned.zip", out_apk);

    zip_writer *zw = zip_writer_open(tmp_unaligned);
    if (!zw) return -1;

    char **files = NULL;
    int count = 0;
    util_collect_files(src_dir, &files, &count, src_dir);

    for (int i = 0; i < count; i++) {
        const char *path = files[i];
        const char *name = path + strlen(src_dir) + 1;
        FILE *fp = fopen(path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            uint8_t *buf = malloc(sz);
            if (buf) {
                if (fread(buf, 1, sz, fp) == (size_t)sz) {
                    zip_writer_add(zw, name, buf, sz, 1, 0);
                }
                free(buf);
            }
            fclose(fp);
        }
        free(files[i]);
    }
    free(files);
    zip_writer_close(zw);

    // Clean up compiled DEX files from source directory
    for (int i = 0; i < dex_count; i++) {
        remove(dex_files[i]);
        free(dex_files[i]);
    }
    free(dex_files);

    if (key_path) {
        rsa_key key;
        if (rsa_load_key(key_path, &key) != 0) {
            printf("rsa_load_key failed\n");
            remove(tmp_unaligned);
            return -1;
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
                    size_t rb = fread(custom_cert, 1, custom_cert_len, f);
                    (void)rb;
                }
                fclose(f);
            }
        }

        // 1. Sign v1
        char tmp_signed1[1024];
        snprintf(tmp_signed1, sizeof(tmp_signed1), "%s.signed1.unaligned.zip", out_apk);
        if (apk_sign_v1(tmp_unaligned, tmp_signed1, &key, 1, 1, custom_cert, custom_cert_len) != 0) {
            printf("apk_sign_v1 failed\n");
            remove(tmp_unaligned);
            if (custom_cert) free(custom_cert);
            return -1;
        }
        remove(tmp_unaligned);

        // 2. Zipalign
        char tmp_aligned[1024];
        snprintf(tmp_aligned, sizeof(tmp_aligned), "%s.aligned.zip", out_apk);
        if (zipalign_file(tmp_signed1, tmp_aligned, 4) != 0) {
            printf("zipalign_file failed\n");
            remove(tmp_signed1);
            if (custom_cert) free(custom_cert);
            return -1;
        }
        remove(tmp_signed1);

        // 3. Sign v2/v3
        if (apk_sign_v2_v3(tmp_aligned, out_apk, &key, 1, 1, custom_cert, custom_cert_len) != 0) {
            printf("apk_sign_v2_v3 failed, fallback to v1\n");
            rename(tmp_aligned, out_apk); // Fallback to v1 only if v2 fails
        } else {
            remove(tmp_aligned);
        }
        if (custom_cert) free(custom_cert);
    } else {
        // No signing, just zipalign
        if (zipalign_file(tmp_unaligned, out_apk, 4) != 0) {
            printf("zipalign_file failed (no sig)\n");
            remove(tmp_unaligned);
            return -1;
        }
        remove(tmp_unaligned);
    }
    return 0;
}
