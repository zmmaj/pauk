
#ifndef LISTS_PARSER_H
#define LISTS_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

// List structure
typedef struct ListToExtract {
    lxb_dom_element_t *list_elem;
    char *filename;
    int list_index;
    const char *list_type; // "ul" or "ol"
} ListToExtract;

// Function prototypes
void store_list_for_extraction(lxb_dom_element_t *list_elem, const char *filename, const char *list_type);
cJSON* extract_list_structure(lxb_dom_element_t *list_elem);
cJSON* extract_list_items(lxb_dom_element_t *list_elem);
cJSON* extract_list_item(lxb_dom_element_t *item_elem, int item_index, int nesting_level);

// Getter functions
int get_list_count(void);
const char* get_list_filename(int index);
lxb_dom_element_t* get_list_element(int index);
const char* get_list_type(int index);

char* get_list_item_text(lxb_dom_element_t *elem);

// Cleanup
void cleanup_lists_storage(void);

#endif // LISTS_PARSER_H