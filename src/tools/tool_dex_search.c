/* dex_search tool: the dex editor's search from the CLI / agent. Searches a
 * .dex file, or every root classesN.dex of an APK (or just entry=...). */
#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cJSON *hits;
    const char *dex;
    int left;
} collect;

static int on_hit(void *u, uint32_t cls, uint32_t line, const char *text) {
    collect *c = (collect *)u;
    cJSON *h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "dex", c->dex);
    cJSON_AddNumberToObject(h, "class", cls);
    cJSON_AddNumberToObject(h, "line", line);
    cJSON_AddStringToObject(h, "text", text);
    cJSON_AddItemToArray(c->hits, h);
    return --c->left <= 0;
}

static int kind_of(const char *s) {
    static const char *names[] = {"smali", "class", "field", "method", "string", "integer"};
    for (int i = 0; i < 6; i++)
        if (strcmp(s, names[i]) == 0) return i;
    return -1;
}

static int is_root_dex(const char *n) {
    if (strncmp(n, "classes", 7) != 0) return 0;
    n += 7;
    while (*n >= '0' && *n <= '9') n++;
    return strcmp(n, ".dex") == 0;
}

static int search_buf(uint8_t *buf, size_t len, const char *label, int kind, const char *q,
                      int flags, const char *scope, collect *c) {
    dex_ctx *ctx = dex_parse(buf, len);
    if (!ctx) return 0;
    c->dex = label;
    int rc = dex_find(ctx, kind, q, flags, scope, on_hit, c);
    dex_free(ctx);
    return rc;
}

static const char *arg_str(cJSON *a, const char *k, const char *def) {
    cJSON *o = cJSON_GetObjectItem(a, k);
    return o && cJSON_IsString(o) ? o->valuestring : def;
}

static int arg_bool(cJSON *a, const char *k) {
    cJSON *o = cJSON_GetObjectItem(a, k);
    return o && (cJSON_IsTrue(o) || (cJSON_IsString(o) && strcmp(o->valuestring, "true") == 0));
}

char *execute_dex_search(cJSON *args) {
    char num[32];
    const char *path = arg_str(args, "path", NULL), *q = arg_str(args, "query", NULL);
    cJSON *qo = cJSON_GetObjectItem(args, "query");
    if (!q && qo && cJSON_IsNumber(qo)) snprintf(num, sizeof(num), "%lld", (long long)qo->valuedouble), q = num;
    int kind = kind_of(arg_str(args, "type", "smali"));
    if (!path || !q || kind < 0) return strdup("{\"error\": \"need path, query and type smali|class|field|method|string|integer\"}");
    const char *entry = arg_str(args, "entry", NULL), *scope = arg_str(args, "scope", "");
    int flags = (arg_bool(args, "case") ? DEX_FIND_CASE : 0) | (arg_bool(args, "regex") ? DEX_FIND_REGEX : 0);
    const char *lim = arg_str(args, "limit", NULL);
    cJSON *lo = cJSON_GetObjectItem(args, "limit");
    collect c = { cJSON_CreateArray(), "", lo && cJSON_IsNumber(lo) ? (int)lo->valuedouble : lim ? atoi(lim) : 500 };
    if (c.left <= 0) c.left = 500;
    int rc = 0;

    zip_archive *za = zip_open(path);
    if (za) {
        for (int i = 0; i < zip_get_num_entries(za) && c.left > 0 && rc == 0; i++) {
            const char *n = zip_get_entry_name(za, i);
            if (entry ? strcmp(n, entry) != 0 : !is_root_dex(n)) continue;
            size_t len = 0;
            uint8_t *buf = zip_extract_entry(za, i, &len);
            if (buf) rc = search_buf(buf, len, n, kind, q, flags, scope, &c), free(buf);
        }
        zip_close(za);
    } else {
        FILE *fp = fopen(path, "rb");
        if (!fp) { cJSON_Delete(c.hits); return strdup("{\"error\": \"cannot open path\"}"); }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *buf = sz > 0 ? malloc((size_t)sz) : NULL;
        if (buf && fread(buf, 1, (size_t)sz, fp) == (size_t)sz) {
            const char *base = strrchr(path, '/');
            rc = search_buf(buf, (size_t)sz, base ? base + 1 : path, kind, q, flags, scope, &c);
        }
        free(buf);
        fclose(fp);
    }
    if (rc < 0) { cJSON_Delete(c.hits); return strdup("{\"error\": \"bad query (integer not a number, or invalid regex)\"}"); }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "count", cJSON_GetArraySize(c.hits));
    cJSON_AddItemToObject(res, "hits", c.hits);
    char *out = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return out;
}
