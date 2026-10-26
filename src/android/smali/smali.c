#include "smali_parser.h"
#include "smali_pool.h"
#include "smali_writer.h"
#include "smali_order.h"
#include "smali_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

static int collect_smali_files(const char *dir, char ***files, int *count) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s%c%s", dir, PATH_SEP, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            collect_smali_files(path, files, count);
        } else if (strstr(ent->d_name, ".smali")) {
            (*files) = (char **)realloc(*files, (*count + 1) * sizeof(char *));
            (*files)[*count] = strdup(path);
            (*count)++;
        }
    }
    closedir(d);
    return 0;
}

int smali_assemble_n(const char *src_dir, const char *out_dex, int *n_out) {
    if (!src_dir || !out_dex) return -1;
    if (n_out) *n_out = 0;
    smali_clear_error();

    smali_ctx_def_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    char **files = NULL;
    int file_count = 0;
    collect_smali_files(src_dir, &files, &file_count);

    int ret = 0;
    for (int i = 0; i < file_count; i++) {
        /* A parse error aborts the assembly: emitting a DEX that silently
         * dropped the broken file would hide the defect. */
        if (parse_smali_file_path(&ctx, files[i]) != 0) {
            if (!smali_last_error()[0])
                smali_fail("failed to parse %s", files[i]);
            ret = -1;
        }
        free(files[i]);
    }
    free(files);

    if (ret != 0) {
        smali_ctx_free(&ctx);
        return -1;
    }

    if (ctx.class_count == 0) {
        return -1;
    }

    smali_order_classes(&ctx);

    /* One DEX per run of classes that fits the 64K id tables; the sub-context
     * borrows its slice of the class array, so only its pools are released here. */
    uint32_t part = 0;
    for (uint32_t start = 0; start < ctx.class_count && ret == 0; part++) {
        uint32_t end = smali_next_dex_group(&ctx, start);
        smali_ctx_def_t sub;
        memset(&sub, 0, sizeof(sub));
        sub.classes = ctx.classes + start;
        sub.class_count = sub.class_cap = end - start;
        char name[4096];
        smali_dex_part_name(out_dex, part, name, sizeof(name));
        smali_pool_build_all(&sub);
        ret = write_assembled_dex(&sub, name);
        smali_pool_free(&sub.strings); smali_pool_free(&sub.types); smali_pool_free(&sub.protos);
        smali_pool_free(&sub.fields); smali_pool_free(&sub.methods);
        start = end;
    }
    if (n_out) *n_out = (int)part;

    smali_ctx_free(&ctx);
    return ret;
}

int smali_assemble(const char *src_dir, const char *out_dex) {
    return smali_assemble_n(src_dir, out_dex, NULL);
}
