#ifndef FORMAT_ARSC_XML_INTERNAL_H
#define FORMAT_ARSC_XML_INTERNAL_H

/* Internal API of the XML resource compiler.  format_arsc_xml.c walks the
 * decoded res directory and calls into the two halves: the element scanner
 * (parse) and the entry lookup plus value conversion (entry). */

#include "format_arsc_internal.h"
#include "format_arsc_model.h"

/* Locates the entry `key` of `type_name` in the configuration `qual`, which is
 * "" for the default configuration and "-land" and friends otherwise.
 * `occurrence` selects which of the entries sharing that key is wanted,
 * counting from zero; a degenerate table may give several entries of one
 * configuration the same key.  Returns NULL when the type, the configuration
 * or that key does not exist. */
arsc_model_entry *arsc_xml_find_entry(arsc_model *m, const char *type_name,
                                      const char *qual, const char *key,
                                      uint32_t occurrence);

/* Stores the decoded text `text` in `e`, keeping the entry's data type when
 * the value fits it.  Returns 1 when the entry changed and 0 when it already
 * held that value, because an unchanged entry must not be touched: appending a
 * second copy of a string to the global pool would alter the table and break
 * the byte-for-byte round trip.  *skipped counts values that cannot be
 * represented and are left exactly as they were. */
int arsc_xml_set_value(arsc_model *m, arsc_ctx *ctx, arsc_model_entry *e,
                       const char *text, int *skipped);

/* Compiles one res values file of the decoded tree into the model.  `qual` is
 * the configuration suffix of the directory holding the file, "" for the
 * plain values directory. */
void arsc_xml_compile_file(const char *path, arsc_model *m, arsc_ctx *ctx,
                           const char *qual, int *changed, int *skipped);

#endif
