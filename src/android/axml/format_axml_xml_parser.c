#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "format_axml_xml_parser.h"

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

/* XML NameChar: '.' and non-ASCII are legal, and layouts depend on both —
 * without '.', <androidx.constraintlayout.widget.ConstraintLayout> was cut
 * to <androidx> and inflation failed with ClassNotFoundException. */
static int is_name_char(char c) {
    unsigned char u = (unsigned char)c;
    return isalnum(u) || c == '_' || c == '-' || c == ':' || c == '.' || u >= 0x80;
}

static char* parse_name(const char **p) {
    skip_ws(p);
    const char *start = *p;
    while (**p && is_name_char(**p)) (*p)++;
    if (start == *p) return NULL;
    
    size_t len = *p - start;
    char *name = malloc(len + 1);
    memcpy(name, start, len);
    name[len] = '\0';
    return name;
}

// Splits "android:name" into ns="android", name="name"
static void split_ns(char *full, char **ns, char **name) {
    char *colon = strchr(full, ':');
    if (colon) {
        *ns = malloc(colon - full + 1);
        memcpy(*ns, full, colon - full);
        (*ns)[colon - full] = '\0';
        
        *name = strdup(colon + 1);
        free(full);
    } else {
        *ns = NULL;
        *name = full;
    }
}

static char* parse_string(const char **p) {
    skip_ws(p);
    if (**p != '"' && **p != '\'') return NULL;
    char quote = *(*p)++;
    const char *start = *p;
    while (**p && **p != quote) (*p)++;
    
    size_t len = *p - start;
    char *val = malloc(len + 1);
    /* Decode entities: the decoder escapes & < > " ' on output, so without
     * this every &amp;/&apos; came back literally in the rebuilt XML. */
    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        static const struct { const char *e; char c; } ents[] = {
            {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''},
        };
        int done = 0;
        if (start[i] == '&') {
            for (size_t k = 0; k < sizeof(ents) / sizeof(ents[0]); k++) {
                size_t el = strlen(ents[k].e);
                if (i + el <= len && strncmp(start + i, ents[k].e, el) == 0) {
                    val[j++] = ents[k].c; i += el; done = 1; break;
                }
            }
            if (!done && i + 2 < len && start[i + 1] == '#') {
                char *end = NULL;
                int hex = start[i + 2] == 'x' || start[i + 2] == 'X';
                unsigned long cp = strtoul(start + i + 2 + hex, &end, hex ? 16 : 10);
                if (end && (size_t)(end - start) < len && *end == ';' && cp && cp < 0x110000) {
                    /* UTF-8 is never longer than the "&#N;" text it replaces */
                    if (cp < 0x80) val[j++] = (char)cp;
                    else if (cp < 0x800) { val[j++] = (char)(0xC0 | (cp >> 6)); val[j++] = (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { val[j++] = (char)(0xE0 | (cp >> 12)); val[j++] = (char)(0x80 | ((cp >> 6) & 0x3F)); val[j++] = (char)(0x80 | (cp & 0x3F)); }
                    else { val[j++] = (char)(0xF0 | (cp >> 18)); val[j++] = (char)(0x80 | ((cp >> 12) & 0x3F)); val[j++] = (char)(0x80 | ((cp >> 6) & 0x3F)); val[j++] = (char)(0x80 | (cp & 0x3F)); }
                    i = (size_t)(end - start) + 1; done = 1;
                }
            }
        }
        if (!done) val[j++] = start[i++];
    }
    val[j] = '\0';

    if (**p == quote) (*p)++;
    return val;
}

static xml_node* parse_element(const char **p) {
    skip_ws(p);
    if (**p != '<') return NULL;
    if ((*p)[1] == '/' || (*p)[1] == '!' || (*p)[1] == '?') return NULL;
    
    (*p)++; // Skip '<'
    char *full_name = parse_name(p);
    if (!full_name) return NULL;
    
    xml_node *node = calloc(1, sizeof(xml_node));
    split_ns(full_name, &node->ns, &node->name);
    
    xml_attr **last_attr = &node->attrs;
    
    // Parse attributes
    while (1) {
        skip_ws(p);
        if (**p == '>' || **p == '/' || **p == '\0') break;
        
        char *attr_full = parse_name(p);
        if (!attr_full) break; // Error or unexpected char
        
        skip_ws(p);
        char *val = NULL;
        if (**p == '=') {
            (*p)++; // Skip '='
            val = parse_string(p);
        } else {
            val = strdup(""); // Empty value
        }
        
        xml_attr *attr = calloc(1, sizeof(xml_attr));
        split_ns(attr_full, &attr->ns, &attr->name);
        attr->value = val;
        
        *last_attr = attr;
        last_attr = &attr->next;
    }
    
    skip_ws(p);
    if (**p == '/') {
        // Self-closing
        (*p)++;
        if (**p == '>') (*p)++;
        return node;
    }
    
    if (**p == '>') {
        (*p)++;
        // Parse children
        xml_node **last_child = &node->children;
        while (1) {
            skip_ws(p);
            if (**p == '<' && (*p)[1] == '/') {
                // End tag
                (*p) += 2;
                char *end_name = parse_name(p);
                if (end_name) free(end_name);
                skip_ws(p);
                if (**p == '>') (*p)++;
                break;
            } else if (**p == '<' && (*p)[1] == '!') {
                // Comment <!...>
                (*p) += 2;
                while (**p && !(**p == '>' && (*p)[-1] == '-' && (*p)[-2] == '-')) (*p)++;
                if (**p == '>') (*p)++;
                continue;
            }
            
            xml_node *child = parse_element(p);
            if (!child) {
                // Text node or error - skip until '<'
                while (**p && **p != '<') (*p)++;
                if (**p == '\0') break;
                continue;
            }
            
            *last_child = child;
            last_child = &child->next;
        }
    }
    
    return node;
}

xml_doc *xml_parse(const char *xml_text) {
    const char *p = xml_text;
    
    // Skip XML declaration <?xml ... ?>
    skip_ws(&p);
    if (strncmp(p, "<?xml", 5) == 0) {
        while (*p && !(*p == '?' && *(p+1) == '>')) p++;
        if (*p) p += 2;
    }
    
    // Skip comments
    while (1) {
        skip_ws(&p);
        if (strncmp(p, "<!--", 4) == 0) {
            while (*p && !(*p == '>' && *(p-1) == '-' && *(p-2) == '-')) p++;
            if (*p) p++;
        } else {
            break;
        }
    }
    
    xml_node *root = parse_element(&p);
    if (!root) return NULL;
    
    xml_doc *doc = calloc(1, sizeof(xml_doc));
    doc->root = root;
    return doc;
}

static void free_attrs(xml_attr *attr) {
    while (attr) {
        xml_attr *next = attr->next;
        if (attr->ns) free(attr->ns);
        if (attr->name) free(attr->name);
        if (attr->value) free(attr->value);
        free(attr);
        attr = next;
    }
}

static void free_nodes(xml_node *node) {
    while (node) {
        xml_node *next = node->next;
        if (node->ns) free(node->ns);
        if (node->name) free(node->name);
        free_attrs(node->attrs);
        free_nodes(node->children);
        free(node);
        node = next;
    }
}

void xml_free(xml_doc *doc) {
    if (!doc) return;
    free_nodes(doc->root);
    free(doc);
}
