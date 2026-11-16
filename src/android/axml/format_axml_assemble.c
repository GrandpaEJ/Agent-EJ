/* Text XML -> binary AXML entry points: from memory (the editor's save) and
 * from a file (the CLI and the decoded-APK rebuild). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "format_axml_xml_parser.h"
#include "formats.h"

uint8_t *axml_assemble_doc(xml_doc *doc, size_t *out_size);

uint8_t *axml_assemble_text(const char *xml, struct arsc_ctx *arsc, size_t *out_size) {
    *out_size = 0;
    xml_doc *doc = xml ? xml_parse(xml) : NULL;
    if (!doc) return NULL;
    axml_assemble_set_arsc(arsc);
    uint8_t *data = axml_assemble_doc(doc, out_size);
    axml_assemble_set_arsc(NULL);
    xml_free(doc);
    if (data && axml_assemble_error()[0]) {
        free(data);
        data = NULL;
        *out_size = 0;
    }
    return data;
}

int axml_assemble(const char *src_xml, const char *out_axml) {
    size_t len = 0;
    char *buf = NULL;
    FILE *f = fopen(src_xml, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        rewind(f);
        buf = sz >= 0 ? malloc((size_t)sz + 1) : NULL;
        if (buf) buf[len = fread(buf, 1, (size_t)sz, f)] = '\0';
    }
    fclose(f);
    if (!buf) return -1;
    /* keeps whatever arsc the caller set with axml_assemble_set_arsc */
    xml_doc *doc = xml_parse(buf);
    size_t out_sz = 0;
    uint8_t *data = doc ? axml_assemble_doc(doc, &out_sz) : NULL;
    int rc = -1;
    FILE *out = data ? fopen(out_axml, "wb") : NULL;
    if (out) {
        rc = fwrite(data, 1, out_sz, out) == out_sz ? 0 : -1;
        if (fclose(out) != 0) rc = -1;
    }
    free(data);
    xml_free(doc);
    free(buf);
    return rc;
}
