#ifndef FORMAT_AXML_INTERNAL_H
#define FORMAT_AXML_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "formats.h"

#define AXML_MAGIC 0x00080003

#define CHUNK_STRING_POOL 0x0001
#define CHUNK_RESOURCE_MAP 0x0180
#define CHUNK_START_NS 0x0100
#define CHUNK_END_NS 0x0101
#define CHUNK_START_ELEM 0x0102
#define CHUNK_END_ELEM 0x0103
#define CHUNK_TEXT 0x0104

typedef struct {
    int count;
    int cap;
    char **strings;
} axml_strpool;

typedef struct {
    int count;
    uint32_t *ids;
} axml_resmap;

typedef struct {
    int ns;
    int name;
    int attr_count;
    int *attr_ns;
    int *attr_name;
    int *attr_raw;
    uint8_t *attr_type;
    uint32_t *attr_data;
} axml_element;

typedef struct axml_ctx {
    axml_strpool pool;
    axml_resmap resmap;
    int has_resmap;
    int event_count;
    int event_cap;
    int *event_types;    // 0=start_ns,1=end_ns,2=start_elem,3=end_elem,4=text
    int *event_lines;
    int *event_ns;
    int *event_name;
    axml_element *elements;
    char *xml;
    const struct arsc_ctx *arsc; // optional resource context for ref resolution
} axml_ctx;

int axml_parse_strpool(axml_ctx *ctx, const uint8_t *data, uint32_t size);
int axml_parse_resmap(axml_ctx *ctx, const uint8_t *data, uint32_t size);
int axml_add_event(axml_ctx *ctx, int type, int line, int ns, int name);
/* Text for a typed attribute value; NULL when the raw string should be used. */
const char *axml_fmt_typed(uint8_t type, uint32_t data, int raw_idx, const char *an,
                           const struct arsc_ctx *arsc, uint32_t attr_res_id);

#endif
