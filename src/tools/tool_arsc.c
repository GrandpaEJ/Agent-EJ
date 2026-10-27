#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include "format_arsc_model.h"
#include "format_arsc_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reads a file into memory. */
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)sz;
    return buf;
}

/* Loads resources.arsc from an APK or from a raw resource table. */
static uint8_t *load_arsc(const char *path, size_t *size) {
    size_t head = 0;
    uint8_t *probe = read_file(path, &head);
    if (!probe) return NULL;
    int is_zip = head > 4 && probe[0] == 'P' && probe[1] == 'K' &&
                 probe[2] == 0x03 && probe[3] == 0x04;
    if (!is_zip) {
        *size = head;
        return probe;
    }
    free(probe);

    zip_archive *za = zip_open(path);
    if (!za) return NULL;
    uint8_t *data = NULL;
    int count = zip_get_num_entries(za);
    for (int i = 0; i < count; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name || strcmp(name, "resources.arsc") != 0) continue;
        size_t sz = 0;
        void *buf = zip_extract_entry(za, i, &sz);
        if (buf) {
            data = malloc(sz);
            if (data) memcpy(data, buf, sz);
            free(buf);
            *size = sz;
        }
        break;
    }
    zip_close(za);
    return data;
}

/* Applies a "0x7f0a0000=text" list of string patches. */
static int apply_string_patches(arsc_model *m, const char *list, int *applied) {
    char *copy = strdup(list);
    if (!copy) return -1;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        uint32_t id = (uint32_t)strtoul(tok, NULL, 0);
        if (arsc_model_set_string(m, id, eq + 1) == 0) (*applied)++;
    }
    free(copy);
    return 0;
}

/* Applies a "0x7f0a0000=42" list of integer patches. */
static int apply_int_patches(arsc_model *m, const char *list, int *applied) {
    char *copy = strdup(list);
    if (!copy) return -1;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        uint32_t id = (uint32_t)strtoul(tok, NULL, 0);
        int32_t value = (int32_t)strtol(eq + 1, NULL, 0);
        if (arsc_model_set_int(m, id, value, ARSC_TYPE_INT_DEC) == 0) (*applied)++;
    }
    free(copy);
    return 0;
}

char* execute_arsc_encode(cJSON* args) {
    cJSON *path_obj = cJSON_GetObjectItem(args, "path");
    cJSON *out_obj = cJSON_GetObjectItem(args, "out");
    if (!path_obj || !cJSON_IsString(path_obj) || !out_obj || !cJSON_IsString(out_obj)) {
        return strdup("{\"error\": \"Missing path or out\"}");
    }

    size_t size = 0;
    uint8_t *data = load_arsc(path_obj->valuestring, &size);
    if (!data) return strdup("{\"error\": \"Failed to read resources.arsc\"}");

    arsc_model *model = arsc_model_load(data, size);
    free(data);
    if (!model) return strdup("{\"error\": \"Invalid resource table\"}");

    int patched = 0;
    cJSON *ss = cJSON_GetObjectItem(args, "set_string");
    if (ss && cJSON_IsString(ss)) apply_string_patches(model, ss->valuestring, &patched);
    cJSON *si = cJSON_GetObjectItem(args, "set_int");
    if (si && cJSON_IsString(si)) apply_int_patches(model, si->valuestring, &patched);

    size_t enc_size = 0;
    uint8_t *enc = arsc_encode(model, &enc_size);
    uint32_t pkg_count = model->pkg_count;
    arsc_model_free(model);
    if (!enc) return strdup("{\"error\": \"Failed to encode the resource table\"}");

    FILE *fp = fopen(out_obj->valuestring, "wb");
    if (!fp) {
        free(enc);
        return strdup("{\"error\": \"Failed to open the output file\"}");
    }
    size_t written = fwrite(enc, 1, enc_size, fp);
    fclose(fp);
    free(enc);
    if (written != enc_size) return strdup("{\"error\": \"Short write\"}");

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"status\": \"success\", \"output\": \"%s\", \"size\": %zu, \"packages\": %u, \"patched\": %d}",
             out_obj->valuestring, enc_size, pkg_count, patched);
    return strdup(buf);
}

/* Writes a summary of the resource table as JSON. */
char* execute_arsc_info(cJSON* args) {
    cJSON *path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");

    size_t size = 0;
    uint8_t *data = load_arsc(path_obj->valuestring, &size);
    if (!data) return strdup("{\"error\": \"Failed to read resources.arsc\"}");

    arsc_model *model = arsc_model_load(data, size);
    free(data);
    if (!model) return strdup("{\"error\": \"Invalid resource table\"}");

    cJSON *root = cJSON_CreateObject();
    cJSON *pkgs = cJSON_CreateArray();
    for (uint32_t p = 0; p < model->pkg_count; p++) {
        const arsc_model_pkg *pk = &model->pkgs[p];
        cJSON *po = cJSON_CreateObject();
        char idbuf[16];
        snprintf(idbuf, sizeof(idbuf), "0x%02x", pk->id);
        cJSON_AddStringToObject(po, "id", idbuf);
        cJSON_AddStringToObject(po, "name", pk->name);
        cJSON *types = cJSON_CreateArray();
        uint32_t variants = 0;
        uint32_t entries = 0;
        for (uint32_t t = 0; t < pk->type_count; t++) {
            const arsc_model_type *ty = &pk->types[t];
            variants += ty->variant_count;
            entries += ty->entry_count;
            cJSON *to = cJSON_CreateObject();
            char tid[16];
            snprintf(tid, sizeof(tid), "0x%02x", ty->id);
            cJSON_AddStringToObject(to, "id", tid);
            const char *name = arsc_pkg_type_string(pk, ty->id - 1);
            cJSON_AddStringToObject(to, "name", name ? name : "");
            cJSON_AddNumberToObject(to, "entries", ty->entry_count);
            cJSON_AddNumberToObject(to, "variants", ty->variant_count);
            cJSON_AddItemToArray(types, to);
        }
        cJSON_AddItemToObject(po, "types", types);
        cJSON_AddNumberToObject(po, "variants", variants);
        cJSON_AddNumberToObject(po, "entries", entries);
        cJSON_AddItemToArray(pkgs, po);
    }
    cJSON_AddItemToObject(root, "packages", pkgs);
    cJSON_AddNumberToObject(root, "values", model->value_count);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    arsc_model_free(model);
    return out;
}

/* Compiles the res values tree a decode produced back into a resources.arsc.
 * This is the half of the rebuild that used to be missing: build_apk copied
 * the original table, so an edit in one of the decoded values files never
 * reached the APK. */
char* execute_arsc_compile(cJSON* args) {
    cJSON *arsc_obj = cJSON_GetObjectItem(args, "arsc");
    cJSON *dir_obj = cJSON_GetObjectItem(args, "res_dir");
    cJSON *out_obj = cJSON_GetObjectItem(args, "out");
    if (!arsc_obj || !cJSON_IsString(arsc_obj) ||
        !dir_obj || !cJSON_IsString(dir_obj) ||
        !out_obj || !cJSON_IsString(out_obj)) {
        return strdup("{\"error\": \"Missing arsc, res_dir or out\"}");
    }

    int changed = 0, skipped = 0;
    if (arsc_compile_xml(arsc_obj->valuestring, dir_obj->valuestring,
                         out_obj->valuestring, &changed, &skipped) != 0) {
        return strdup("{\"error\": \"Failed to compile the res values tree\"}");
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"status\": \"success\", \"output\": \"%s\", \"changed\": %d, \"skipped\": %d}",
             out_obj->valuestring, changed, skipped);
    return strdup(buf);
}
