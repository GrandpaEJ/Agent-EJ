#ifndef DEOBF_INTERNAL_H
#define DEOBF_INTERNAL_H

#include <stddef.h>

/* One renamed member (field or method) of a class. */
typedef struct deobf_member {
    char *obf;              /* obfuscated member name (lookup key) */
    char *rep;              /* replacement name */
    struct deobf_member *next;
} deobf_member;

/* One class: its obfuscated descriptor body (e.g. "a/b/c") and replacement. */
typedef struct deobf_class {
    char *obf;              /* obfuscated descriptor body (lookup key) */
    char *rep;              /* replacement descriptor body, or NULL to keep */
    deobf_member *members;  /* owned member renames */
    struct deobf_class *hnext;
} deobf_class;

/* A rename table: classes hashed by obfuscated descriptor body. */
struct deobf_table {
    deobf_class **buckets;
    size_t nbuckets;
    size_t nclasses;
};
typedef struct deobf_table deobf_table;

/* Finds the class entry for an obfuscated body, or NULL. */
deobf_class *deobf_find_class(deobf_table *t, const char *obf_body);
/* The replacement member name for (obf_body, obf_member), or NULL if none. */
const char *deobf_find_member(deobf_table *t, const char *obf_body, const char *obf_member);

#endif
