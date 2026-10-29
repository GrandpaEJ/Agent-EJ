#ifndef AXML_XML_PARSER_H
#define AXML_XML_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef struct xml_attr {
    char *ns;      // e.g., "android" (can be NULL)
    char *name;    // e.g., "versionCode"
    char *value;   // e.g., "15"
    struct xml_attr *next;
} xml_attr;

typedef struct xml_node {
    char *ns;      // e.g., "" (can be NULL)
    char *name;    // e.g., "manifest"; NULL for a text node
    char *text;    // character data of a text node (name == NULL)
    xml_attr *attrs;
    struct xml_node *children; // First child
    struct xml_node *next;     // Next sibling
} xml_node;

typedef struct xml_doc {
    xml_node *root;
} xml_doc;

// Parses a null-terminated XML string into an AST
xml_doc *xml_parse(const char *xml_text);

// Frees the parsed XML AST
void xml_free(xml_doc *doc);

#endif
