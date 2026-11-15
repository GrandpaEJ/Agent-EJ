#ifndef SMALI_EVCMP_H
#define SMALI_EVCMP_H

#include <stdint.h>

/* Ordering of encoded data, read straight from its DEX encoding.
 *
 * smali.jar (dexlib2) sorts the encoded_array, annotation and annotation_set
 * sections with the compareTo() of its value classes: the value type first,
 * then the value (signed for integers, Float/Double.compare for floating
 * point, list size first for arrays). String/type/field/method references
 * compare by their pool index, which is the same order because each pool is
 * sorted the way dexlib2 compares the referenced items. */

uint32_t smali_read_uleb(const uint8_t **p);

/* Returns the pointer past one encoded_value. */
const uint8_t *smali_ev_skip(const uint8_t *p);

/* <0, 0, >0: one encoded_value against another. */
int smali_ev_cmp(const uint8_t *a, const uint8_t *b);

/* encoded_annotation (type_idx, size, name/value pairs) against another. */
int smali_ev_cmp_annotation(const uint8_t *a, const uint8_t *b);

/* encoded_array_item: element by element, a shorter prefix first. */
int smali_ev_cmp_array_item(const uint8_t *a, const uint8_t *b);

#endif
