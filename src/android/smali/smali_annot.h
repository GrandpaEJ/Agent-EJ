#ifndef SMALI_ANNOT_H
#define SMALI_ANNOT_H

#include "smali_types.h"

/* A field reference is "Lclass;->name:type". The type is not decoration:
 * the field pool is keyed by the whole reference, so a reference without a
 * type cannot be resolved and the encoded value would point at an arbitrary
 * field of the pool. */
int smali_field_ref_valid(const char *ref);

/* Parse one annotation value. On a malformed value the diagnostic is printed
 * and -1 is returned; the caller has to release the element. */
int smali_parse_annot_value(char **p, smali_annotation_elem_t *el, uint32_t line);

/* Parse the body of an ".annotation" block: the visibility, the type and the
 * elements, up to and including the ".end annotation" terminator. */
int smali_parse_annot_block(char **p, smali_annotation_t *ann, uint32_t line);

void smali_annot_elem_free(smali_annotation_elem_t *e);
void smali_annot_free(smali_annotation_t *a);

#endif
