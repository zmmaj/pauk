
#ifndef MENUS_PARSER_H
#define MENUS_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

// Menu structure
typedef struct MenuToExtract {
    lxb_dom_element_t *menu_elem;
    char *filename;
    int menu_index;
    const char *menu_type; // "nav", "menu", or specific type
} MenuToExtract;

// Menu extraction functions
void store_menu_for_extraction(lxb_dom_element_t *menu_elem, const char *filename, const char *menu_type);
cJSON* extract_menu_structure(lxb_dom_element_t *menu_elem);
cJSON* extract_menu_items(lxb_dom_element_t *menu_elem);
cJSON* extract_menu_item(lxb_dom_element_t *item_elem, int item_index, int nesting_level);

// Getter functions
int get_menu_count(void);
const char* get_menu_filename(int index);
lxb_dom_element_t* get_menu_element(int index);
const char* get_menu_type(int index);

// Cleanup
void cleanup_menus_storage(void);

#endif // MENUS_PARSER_H