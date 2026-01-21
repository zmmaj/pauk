#ifndef HEADINGS_PARSER_H
#define HEADINGS_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

typedef struct {
    int level;           // 1-6 for h1-h6
    const char *text;
    const char *id;
    const char *class;
    int position;        // Order in document
    int section_depth;   // Nested sections depth
    cJSON *element_json; // Reference to full element
} HeadingInfo;

// Document structure tracking
typedef struct {
    HeadingInfo *headings;
    int count;
    int capacity;
    int current_section_depth;
    cJSON *outline;      // JSON outline structure
} DocumentOutline;

// Main functions
void init_document_outline(DocumentOutline *outline);
void parse_heading_element(lxb_dom_element_t *elem, cJSON *elem_json, DocumentOutline *outline);
void build_document_outline(DocumentOutline *outline);
cJSON* get_document_outline_json(DocumentOutline *outline);
void free_document_outline(DocumentOutline *outline);

// Helper functions
int get_heading_level(const char *tag_name);
const char* get_heading_level_name(int level);
int should_reset_heading_counter(const char *tag_name); // For article, section, etc.
void update_section_depth(const char *tag_name, DocumentOutline *outline, int is_closing);

#endif // HEADINGS_PARSER_H