#include "smali_staticval.h"
#include "smali_value.h"
#include <string.h>

/* The encoded value a static field starts with: its initializer coerced to
 * the field type, or the type's default value when it has none. */
static void field_value(const smali_field_def_t *f, smali_annotation_elem_t *el) {
    memset(el, 0, sizeof(*el));
    char t = f->type ? f->type[0] : 'I';
    int ref = (t == 'L' || t == '[');
    if (!f->has_init_value) {
        el->value_type = ref ? VALUE_TYPE_NULL
                       : t == 'Z' ? VALUE_TYPE_BOOL : t == 'J' ? VALUE_TYPE_LONG
                       : t == 'F' ? VALUE_TYPE_FLOAT : t == 'D' ? VALUE_TYPE_DOUBLE
                       : t == 'B' ? VALUE_TYPE_BYTE : t == 'C' ? VALUE_TYPE_CHAR
                       : t == 'S' ? VALUE_TYPE_SHORT : VALUE_TYPE_INT;
        return;
    }
    switch (f->value_type) {
    case VALUE_TYPE_ARRAY:
        if (f->init_array) *el = *f->init_array;
        else el->value_type = VALUE_TYPE_NULL;
        return;
    case VALUE_TYPE_NULL:
    case VALUE_TYPE_BOOL:
        el->value_type = f->value_type;
        el->value_int = f->value_int;
        return;
    case VALUE_TYPE_STRING:
    case VALUE_TYPE_ENUM:
    case VALUE_TYPE_TYPE:
        el->value_type = f->value_type;
        el->value_str = f->value_str;
        return;
    case VALUE_TYPE_BYTE: case VALUE_TYPE_SHORT: case VALUE_TYPE_CHAR:
    case VALUE_TYPE_INT: case VALUE_TYPE_LONG:
    case VALUE_TYPE_FLOAT: case VALUE_TYPE_DOUBLE:
        break;
    default:
        el->value_type = VALUE_TYPE_INT;
        return;
    }
    /* A numeric literal takes the representation of the field type. */
    el->value_double = f->value_double;
    el->value_int = f->value_type == VALUE_TYPE_LONG ? f->value_int : (int32_t)f->value_int;
    el->value_type = ref ? VALUE_TYPE_NULL
                   : t == 'Z' ? VALUE_TYPE_BOOL : t == 'F' ? VALUE_TYPE_FLOAT
                   : t == 'D' ? VALUE_TYPE_DOUBLE : t == 'B' ? VALUE_TYPE_BYTE
                   : t == 'S' ? VALUE_TYPE_SHORT : t == 'C' ? VALUE_TYPE_CHAR
                   : t == 'J' ? VALUE_TYPE_LONG : VALUE_TYPE_INT;
    if (el->value_type == VALUE_TYPE_BOOL) el->value_int = f->value_int != 0;
}

/* dexlib2 EncodedValueUtils.isDefaultValue (0.0 == -0.0 there as well). */
static int is_default(const smali_annotation_elem_t *el) {
    switch (el->value_type) {
    case VALUE_TYPE_NULL:   return 1;
    case VALUE_TYPE_BOOL:   case VALUE_TYPE_BYTE: case VALUE_TYPE_SHORT:
    case VALUE_TYPE_CHAR:   case VALUE_TYPE_INT:  case VALUE_TYPE_LONG:
        return el->value_int == 0;
    case VALUE_TYPE_FLOAT:  return (float)el->value_double == 0.0f;
    case VALUE_TYPE_DOUBLE: return el->value_double == 0.0;
    default:                return 0;
    }
}

int smali_static_values(smali_ctx_def_t *ctx, smali_class_def_t *c, smali_buf_t *out) {
    smali_annotation_elem_t el;
    int32_t last = -1;
    for (uint32_t j = 0; j < c->static_field_count; j++) {
        if (!c->static_fields[j].has_init_value) continue;
        field_value(&c->static_fields[j], &el);
        if (!is_default(&el)) last = (int32_t)j;
    }
    if (last < 0) return 0;
    buf_write_uleb128(out, (uint32_t)last + 1);
    for (int32_t j = 0; j <= last; j++) {
        field_value(&c->static_fields[j], &el);
        write_encoded_value(out, ctx, &el);
    }
    return 1;
}
