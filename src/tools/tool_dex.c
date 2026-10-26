#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include "smali_parser.h"
#include "smali_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

char* execute_disasm_dex(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* cls_obj = cJSON_GetObjectItem(args, "class");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    uint32_t cls_idx = 0;
    if (cls_obj && cJSON_IsNumber(cls_obj)) cls_idx = (uint32_t)cJSON_GetNumberValue(cls_obj);
    else if (cls_obj && cJSON_IsString(cls_obj)) cls_idx = (uint32_t)atol(cls_obj->valuestring);
    FILE *fp = fopen(path_obj->valuestring, "rb");
    if (!fp) return strdup("{\"error\": \"Failed to open file\"}");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz > 10 * 1024 * 1024) { fclose(fp); return strdup("{\"error\": \"File too large\"}"); }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return strdup("{\"error\": \"Memory allocation failed\"}"); }
    fread(buf, 1, sz, fp);
    fclose(fp);

    dex_ctx *ctx = dex_parse(buf, sz);
    if (!ctx) { free(buf); return strdup("{\"error\": \"Invalid DEX file\"}"); }

    char *smali = dex_to_smali_class(ctx, cls_idx);
    dex_free(ctx);
    free(buf);
    if (!smali) return strdup("{\"error\": \"Failed to generate smali\"}");

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "smali", smali);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(smali);
    return res_str;
}

char* execute_smali_assemble(cJSON* args) {
    cJSON* src_dir_obj = cJSON_GetObjectItem(args, "src_dir");
    cJSON* out_dex_obj = cJSON_GetObjectItem(args, "out_dex");
    if (!src_dir_obj || !cJSON_IsString(src_dir_obj) || !out_dex_obj || !cJSON_IsString(out_dex_obj))
        return strdup("{\"error\": \"Missing src_dir or out_dex path\"}");
    int dex_count = 0;
    int ret = smali_assemble_n(src_dir_obj->valuestring, out_dex_obj->valuestring, &dex_count);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", ret);
    if (ret == 0) {
        cJSON_AddStringToObject(res, "output", out_dex_obj->valuestring);
        cJSON_AddNumberToObject(res, "dex_count", dex_count);
    } else {
        cJSON_AddStringToObject(res, "error", "Assembly failed");
    }
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_smali_flow(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* method_obj = cJSON_GetObjectItem(args, "method");
    cJSON* mode_obj = cJSON_GetObjectItem(args, "mode");
    cJSON* scope_obj = cJSON_GetObjectItem(args, "scope");
    if (!path_obj || !cJSON_IsString(path_obj))
        return strdup("{\"error\": \"Missing path to .smali file\"}");

    smali_flow_mode_t mode = SMALI_FLOW_ADVANCE;
    if (mode_obj && cJSON_IsString(mode_obj)) {
        const char *m = mode_obj->valuestring;
        if      (strcmp(m, "basic")   == 0) mode = SMALI_FLOW_BASIC;
        else if (strcmp(m, "advance") == 0) mode = SMALI_FLOW_ADVANCE;
        else if (strcmp(m, "full")    == 0) mode = SMALI_FLOW_FULL;
    }
    const char *scope = "method";
    if (scope_obj && cJSON_IsString(scope_obj)) scope = scope_obj->valuestring;

    smali_ctx_def_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    // Check if path is a directory or file
    struct stat st;
    if (stat(path_obj->valuestring, &st) != 0)
        return strdup("{\"error\": \"Path not found\"}");

    if (S_ISDIR(st.st_mode)) {
        // Directory: collect .smali files
        char **files = NULL;
        int file_count = 0;
        DIR *d = opendir(path_obj->valuestring);
        if (!d) return strdup("{\"error\": \"Cannot open directory\"}");
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char p[4096];
            snprintf(p, sizeof(p), "%s/%s", path_obj->valuestring, ent->d_name);
            struct stat ps;
            if (stat(p, &ps) == 0 && S_ISDIR(ps.st_mode)) {
                // Recurse into subdirectories
                char **sub = NULL;
                DIR *sd = opendir(p);
                if (sd) {
                    struct dirent *se;
                    while ((se = readdir(sd)) != NULL) {
                        if (strcmp(se->d_name, ".") == 0 || strcmp(se->d_name, "..") == 0) continue;
                        char sp[4096];
                        snprintf(sp, sizeof(sp), "%s/%s", p, se->d_name);
                        if (strstr(se->d_name, ".smali")) {
                            files = realloc(files, (file_count + 1) * sizeof(char *));
                            files[file_count++] = strdup(sp);
                        }
                    }
                    closedir(sd);
                }
                if (sub) free(sub);
            } else if (strstr(ent->d_name, ".smali")) {
                files = realloc(files, (file_count + 1) * sizeof(char *));
                files[file_count++] = strdup(p);
            }
        }
        closedir(d);

        for (int i = 0; i < file_count; i++) {
            parse_smali_file_path(&ctx, files[i]);
            free(files[i]);
        }
        free(files);
    } else {
        // Single file
        parse_smali_file_path(&ctx, path_obj->valuestring);
    }

    if (ctx.class_count == 0) {
        return strdup("{\"error\": \"No classes found\"}");
    }

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "scope", scope);
    const char *mode_str = mode == SMALI_FLOW_BASIC ? "basic" :
                           mode == SMALI_FLOW_FULL  ? "full"  : "advance";
    cJSON_AddStringToObject(res, "mode", mode_str);

    if (strcmp(scope, "file") == 0) {
        char *f = smali_flow_file(&ctx, mode);
        if (f) cJSON_AddStringToObject(res, "flowchart", f);
        free(f);
        char *res_str = cJSON_PrintUnformatted(res);
        cJSON_Delete(res);
        smali_ctx_free(&ctx);
        return res_str;
    }

    cJSON *classes_arr = cJSON_CreateArray();

    for (uint32_t ci = 0; ci < ctx.class_count; ci++) {
        smali_class_def_t *cls = &ctx.classes[ci];
        cJSON *cls_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(cls_obj, "class", cls->descriptor ? cls->descriptor : "");
        cJSON *methods_arr = cJSON_CreateArray();

        if (strcmp(scope, "class") == 0) {
            char *f = smali_flow_class(cls, mode);
            if (f) cJSON_AddStringToObject(cls_obj, "flowchart", f);
            free(f);
            cJSON_AddItemToArray(classes_arr, cls_obj);
            continue;
        }

        smali_method_def_t *all_methods[] = {cls->direct_methods, cls->virtual_methods};
        uint32_t all_counts[] = {cls->direct_method_count, cls->virtual_method_count};
        const char *kind[] = {"direct", "virtual"};

        for (int mk = 0; mk < 2; mk++) {
            for (uint32_t mi = 0; mi < all_counts[mk]; mi++) {
                smali_method_def_t *method = &all_methods[mk][mi];
                char sig[512];
                if (method_obj && cJSON_IsString(method_obj)) {
                    char full[512];
                    snprintf(full, sizeof(full), "%s%s", method->name, method->signature ? method->signature : "");
                    if (strstr(full, method_obj->valuestring) == NULL) continue;
                }

                char *flow = smali_flow_method(method, mode);
                if (flow && strlen(flow) > 0) {
                    cJSON *mobj = cJSON_CreateObject();
                    snprintf(sig, sizeof(sig), "%s%s", method->name, method->signature ? method->signature : "");
                    cJSON_AddStringToObject(mobj, "name", sig);
                    cJSON_AddStringToObject(mobj, "kind", kind[mk]);
                    cJSON_AddStringToObject(mobj, "flowchart", flow);
                    cJSON_AddItemToArray(methods_arr, mobj);
                }
                free(flow);
            }
        }

        cJSON_AddItemToObject(cls_obj, "methods", methods_arr);
        cJSON_AddItemToArray(classes_arr, cls_obj);
    }

    cJSON_AddItemToObject(res, "classes", classes_arr);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    smali_ctx_free(&ctx);
    return res_str;
}

char* execute_read_dex(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    FILE *fp = fopen(path_obj->valuestring, "rb");
    if (!fp) return strdup("{\"error\": \"Failed to open file\"}");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz > 10 * 1024 * 1024) { fclose(fp); return strdup("{\"error\": \"File too large\"}"); }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return strdup("{\"error\": \"Memory allocation failed\"}"); }
    fread(buf, 1, sz, fp);
    fclose(fp);

    dex_ctx *ctx = dex_parse(buf, sz);
    if (!ctx) { free(buf); return strdup("{\"error\": \"Invalid DEX file\"}"); }

    cJSON* out_dir_obj = cJSON_GetObjectItem(args, "out_dir");
    if (out_dir_obj && cJSON_IsString(out_dir_obj)) {
        const char *out_dir = out_dir_obj->valuestring;
        uint32_t ci = 0;
        while (1) {
            char *smali_code = dex_to_smali_class(ctx, ci);
            if (!smali_code) break;
            
            char *class_line = strdup(smali_code);
            char *nl = strchr(class_line, '\n');
            if (nl) *nl = '\0';
            
            char *semi = strchr(class_line, ';');
            if (semi) {
                *semi = '\0';
                /* The class descriptor is the last whitespace-delimited token of the
                 * ".class <flags...> Lpkg/Name" line. Searching for the last 'L'
                 * anywhere in the line mangles names and drops the package path
                 * (e.g. "@DynamiteLoaderClassLoader" -> "oader"). */
                char *desc = strrchr(class_line, ' ');
                desc = desc ? desc + 1 : class_line;
                if (*desc == 'L') desc++; // Skip the 'L' of the descriptor
                /* The descriptor comes straight out of the DEX file, so a
                 * hostile APK can try to escape out_dir with ".." segments.
                 * Reject any path component that is not a plain name. */
                if (*desc) {
                    for (const char *q = desc; *q; ) {
                        const char *seg = q;
                        while (*q && *q != '/') q++;
                        size_t seg_len = (size_t)(q - seg);
                        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
                            desc = NULL;
                            break;
                        }
                        if (*q == '/') q++;
                    }
                }
                if (desc && *desc) {
                    char filepath[1024];
                    snprintf(filepath, sizeof(filepath), "%s/%s.smali", out_dir, desc);
                    
                    char dirpath[1024];
                    snprintf(dirpath, sizeof(dirpath), "%s/%s.smali", out_dir, desc);
                    char *last_slash = strrchr(dirpath, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        char tmp[1024];
                        snprintf(tmp, sizeof(tmp), "%s", dirpath);
                        for (char *p = tmp + 1; *p; p++) {
                            if (*p == '/') {
                                *p = '\0';
                                #if defined(_WIN32)
                                mkdir(tmp);
                                #else
                                mkdir(tmp, 0777);
                                #endif
                                *p = '/';
                            }
                        }
                        #if defined(_WIN32)
                        mkdir(tmp);
                        #else
                        mkdir(tmp, 0777);
                        #endif
                    }
                    
                    FILE *out_fp = fopen(filepath, "w");
                    if (out_fp) {
                        fputs(smali_code, out_fp);
                        fclose(out_fp);
                    }
                }
            }
            free(class_line);
            free(smali_code);
            ci++;
        }
        
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "status", "success");
        cJSON_AddNumberToObject(res, "class_count", ci);
        char *res_str = cJSON_PrintUnformatted(res);
        cJSON_Delete(res);
        dex_free(ctx);
        free(buf);
        return res_str;
    }

    char *dump = dex_dump(ctx);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "dump", dump ? dump : "");
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(dump);
    dex_free(ctx);
    free(buf);
    return res_str;
}

