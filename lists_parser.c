
#include "lists_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"  // For parse_inline_styles_simple, get_element_text_simple

// Static storage
static ListToExtract *lists_to_extract = NULL;
static int list_count = 0;
static int list_capacity = 0;


// ===== STYLE HELPER FUNCTIONS =====
static void add_default_list_styles(cJSON *styles, const char *list_type) {
    if (!cJSON_GetObjectItem(styles, "margin")) {
        cJSON_AddStringToObject(styles, "margin", "16px 0");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "0 0 0 20px");
    }
    if (!cJSON_GetObjectItem(styles, "list-style-type")) {
        if (list_type && strcasecmp(list_type, "ol") == 0) {
            cJSON_AddStringToObject(styles, "list-style-type", "decimal");
        } else {
            cJSON_AddStringToObject(styles, "list-style-type", "disc");
        }
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#333333");
    }
}

static cJSON* create_default_list_styles(const char *list_type) {
    cJSON *styles = cJSON_CreateObject();
    
    cJSON_AddStringToObject(styles, "margin", "16px 0");
    cJSON_AddStringToObject(styles, "padding", "0 0 0 20px");
    
    if (list_type && strcasecmp(list_type, "ol") == 0) {
        cJSON_AddStringToObject(styles, "list-style-type", "decimal");
    } else {
        cJSON_AddStringToObject(styles, "list-style-type", "disc");
    }
    
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "color", "#333333");
    
    return styles;
}

static void add_default_list_item_styles(cJSON *styles, int nesting_level) {
    if (!cJSON_GetObjectItem(styles, "margin")) {
        cJSON_AddStringToObject(styles, "margin", "4px 0");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        // More padding for deeper nesting, but limit nesting
        if (nesting_level > 10) nesting_level = 10;
        int left_padding = 20 + (nesting_level * 15);
        char padding[32]; // Increased buffer
        snprintf(padding, sizeof(padding), "4px 0 4px %dpx", left_padding);
        cJSON_AddStringToObject(styles, "padding", padding);
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#333333");
    }
}

static cJSON* create_default_list_item_styles(int nesting_level) {
    cJSON *styles = cJSON_CreateObject();
    
    cJSON_AddStringToObject(styles, "margin", "4px 0");
    
    // Calculate padding based on nesting level, with limit
    if (nesting_level > 10) nesting_level = 10;
    int left_padding = 20 + (nesting_level * 15);
    char padding[32]; // Increased buffer
    snprintf(padding, sizeof(padding), "4px 0 4px %dpx", left_padding);
    cJSON_AddStringToObject(styles, "padding", padding);
    
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "color", "#333333");
    
    return styles;
}

static void calculate_list_dimensions(cJSON *list_json) {
    if (!list_json) return;
    
    cJSON *items = cJSON_GetObjectItem(list_json, "items");
    if (items && cJSON_IsArray(items)) {
        int item_count = cJSON_GetArraySize(items);
        
        // Estimate: 25px per list item + 20px padding
        int estimated_height = item_count * 25 + 20;
        
        cJSON_AddNumberToObject(list_json, "estimated_width", 400);
        cJSON_AddNumberToObject(list_json, "estimated_height", estimated_height);
    }
}



// ===== GETTER FUNCTIONS =====
int get_list_count(void) {
    return list_count;
}

const char* get_list_filename(int index) {
    if (index < 0 || index >= list_count) return NULL;
    return lists_to_extract[index].filename;
}

lxb_dom_element_t* get_list_element(int index) {
    if (index < 0 || index >= list_count) return NULL;
    return lists_to_extract[index].list_elem;
}

const char* get_list_type(int index) {
    if (index < 0 || index >= list_count) return NULL;
    return lists_to_extract[index].list_type;
}
// ============================

void store_list_for_extraction(lxb_dom_element_t *list_elem, const char *filename, const char *list_type) {
    if (!list_elem || !filename || !list_type) return;
    
    // Resize array if needed
    if (list_count >= list_capacity) {
        list_capacity = list_capacity == 0 ? 4 : list_capacity * 2;
        lists_to_extract = realloc(lists_to_extract, list_capacity * sizeof(ListToExtract));
    }
    
    // Store list
    lists_to_extract[list_count].list_elem = list_elem;
    lists_to_extract[list_count].filename = strdup(filename);
    lists_to_extract[list_count].list_type = strdup(list_type);
    lists_to_extract[list_count].list_index = list_count + 1;
    list_count++;
}

cJSON* extract_list_structure(lxb_dom_element_t *list_elem) {
    if (!list_elem) return NULL;
    
    cJSON *list_json = cJSON_CreateObject();
    
    // Determine list type
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(list_elem, &len);
    char *list_type = NULL;
    if (tag_name && len > 0) {
        list_type = malloc(len + 1);
        memcpy(list_type, tag_name, len);
        list_type[len] = '\0';
        cJSON_AddStringToObject(list_json, "list_type", list_type);
    } else {
        cJSON_AddStringToObject(list_json, "list_type", "ul"); // default
    }
    
    // Get list attributes
    const lxb_char_t *list_id = lxb_dom_element_id(list_elem, &len);
    if (list_id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, list_id, len);
        id_str[len] = '\0';
        cJSON_AddStringToObject(list_json, "id", id_str);
        free(id_str);
    }
    
    const lxb_char_t *list_class = lxb_dom_element_class(list_elem, &len);
    if (list_class && len > 0) {
        char *class_str = malloc(len + 1);
        memcpy(class_str, list_class, len);
        class_str[len] = '\0';
        cJSON_AddStringToObject(list_json, "class", class_str);
        free(class_str);
    }
    
    // For ordered lists, get start attribute
    if (list_type && strcasecmp(list_type, "ol") == 0) {
        lxb_dom_attr_t *start_attr = lxb_dom_element_attr_by_name(
            list_elem, (lxb_char_t*)"start", 5);
        if (start_attr) {
            const lxb_char_t *start = lxb_dom_attr_value(start_attr, &len);
            if (start && len > 0) {
                char *start_str = malloc(len + 1);
                memcpy(start_str, start, len);
                start_str[len] = '\0';
                cJSON_AddStringToObject(list_json, "start", start_str);
                free(start_str);
            }
        }
    }
    
    // Get list styles
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(list_elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            add_default_list_styles(styles, list_type);
            cJSON_AddItemToObject(list_json, "list_style", styles);
        } else {
            cJSON *styles = create_default_list_styles(list_type);
            cJSON_AddItemToObject(list_json, "list_style", styles);
        }
    } else {
        cJSON *styles = create_default_list_styles(list_type);
        cJSON_AddItemToObject(list_json, "list_style", styles);
    }
    
    // Extract list items
    cJSON *items = extract_list_items(list_elem);
    if (items) {
        cJSON_AddItemToObject(list_json, "items", items);
        cJSON_AddNumberToObject(list_json, "item_count", cJSON_GetArraySize(items));
    }
    
    // Calculate list dimensions
    calculate_list_dimensions(list_json);
    
    if (list_type) free(list_type);
    return list_json;
}

cJSON* extract_list_items(lxb_dom_element_t *list_elem) {
    if (!list_elem) return NULL;
    
    cJSON *items_array = cJSON_CreateArray();
    int item_index = 0;
    
    lxb_dom_node_t *list_node = lxb_dom_interface_node(list_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(list_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "li") == 0) {
                    // Extract this list item
                    cJSON *item_json = extract_list_item(child_elem, item_index++, 0);
                    if (item_json) {
                        cJSON_AddItemToArray(items_array, item_json);
                    }
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    if (cJSON_GetArraySize(items_array) > 0) {
        return items_array;
    } else {
        cJSON_Delete(items_array);
        return NULL;
    }
}

cJSON* extract_list_item(lxb_dom_element_t *item_elem, int item_index, int nesting_level) {
    cJSON *item_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(item_json, "item_index", item_index);
    cJSON_AddNumberToObject(item_json, "nesting_level", nesting_level);
    
    // Get item attributes
    size_t len;
    const lxb_char_t *item_id = lxb_dom_element_id(item_elem, &len);
    if (item_id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, item_id, len);
        id_str[len] = '\0';
        cJSON_AddStringToObject(item_json, "id", id_str);
        free(id_str);
    }
    
    const lxb_char_t *item_class = lxb_dom_element_class(item_elem, &len);
    if (item_class && len > 0) {
        char *class_str = malloc(len + 1);
        memcpy(class_str, item_class, len);
        class_str[len] = '\0';
        cJSON_AddStringToObject(item_json, "class", class_str);
        free(class_str);
    }
    
    // Get value attribute (for ordered lists)
    lxb_dom_attr_t *value_attr = lxb_dom_element_attr_by_name(
        item_elem, (lxb_char_t*)"value", 5);
    if (value_attr) {
        const lxb_char_t *value = lxb_dom_attr_value(value_attr, &len);
        if (value && len > 0) {
            char *value_str = malloc(len + 1);
            memcpy(value_str, value, len);
            value_str[len] = '\0';
            cJSON_AddStringToObject(item_json, "value", value_str);
            free(value_str);
        }
    }
    
// Get item text content - IMPROVED VERSION
char *item_text = get_element_text_simple(item_elem);
if (item_text && strlen(item_text) > 0) {
    // More aggressive cleaning for list items
    char *cleaned = malloc(strlen(item_text) + 1);
    char *src = item_text;
    char *dst = cleaned;
    int last_was_space = 0;
    
    while (*src) {
        if (*src == '\n' || *src == '\r' || *src == '\t') {
            if (!last_was_space) {
                *dst++ = ' ';
                last_was_space = 1;
            }
        } else if (isspace(*src)) {
            if (!last_was_space) {
                *dst++ = ' ';
                last_was_space = 1;
            }
        } else {
            *dst++ = *src;
            last_was_space = 0;
        }
        src++;
    }
    
    *dst = '\0';
    
    // Trim
    char *start = cleaned;
    while (*start && isspace(*start)) start++;
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        *end = '\0';
        end--;
    }
    
    if (strlen(start) > 0) {
        cJSON_AddStringToObject(item_json, "text", start);
    }
    
    free(cleaned);
}
if (item_text) free(item_text);
    
    // Get item styles
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(item_elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            add_default_list_item_styles(styles, nesting_level);
            cJSON_AddItemToObject(item_json, "style", styles);
        } else {
            cJSON *styles = create_default_list_item_styles(nesting_level);
            cJSON_AddItemToObject(item_json, "style", styles);
        }
    } else {
        cJSON *styles = create_default_list_item_styles(nesting_level);
        cJSON_AddItemToObject(item_json, "style", styles);
    }
    
    // Check for nested lists within this item
    cJSON *nested_lists = cJSON_CreateArray();
    int nested_count = 0;
    
    lxb_dom_node_t *item_node = lxb_dom_interface_node(item_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(item_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "ul") == 0 || strcasecmp(tag, "ol") == 0) {
                    // Found a nested list!
                    cJSON *nested_list = extract_list_structure(child_elem);
                    if (nested_list) {
                        cJSON_AddNumberToObject(nested_list, "parent_item_index", item_index);
                        cJSON_AddNumberToObject(nested_list, "nesting_level", nesting_level + 1);
                        cJSON_AddItemToArray(nested_lists, nested_list);
                        nested_count++;
                    }
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    if (nested_count > 0) {
        cJSON_AddItemToObject(item_json, "nested_lists", nested_lists);
        cJSON_AddNumberToObject(item_json, "nested_list_count", nested_count);
    } else {
        cJSON_Delete(nested_lists);
    }
    
    return item_json;
}


void cleanup_lists_storage(void) {
    for (int i = 0; i < list_count; i++) {
        if (lists_to_extract[i].filename) {
            free(lists_to_extract[i].filename);
        }
        if (lists_to_extract[i].list_type) {
            free((char*)lists_to_extract[i].list_type);
        }
    }
    free(lists_to_extract);
    lists_to_extract = NULL;
    list_count = list_capacity = 0;
}

char* get_list_item_text(lxb_dom_element_t *elem) {
    if (!elem) return strdup("");
    
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    if (!node) return strdup("");
    
    size_t text_len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(node, &text_len);
    
    if (!text || text_len == 0) {
        if (text && node->owner_document) {
            lxb_dom_document_destroy_text(node->owner_document, text);
        }
        return strdup("");
    }
    
    char *result = malloc(text_len + 1);
    if (!result) {
        if (node->owner_document) {
            lxb_dom_document_destroy_text(node->owner_document, text);
        }
        return strdup("");
    }
    
    memcpy(result, text, text_len);
    result[text_len] = '\0';
    
    if (node->owner_document) {
        lxb_dom_document_destroy_text(node->owner_document, text);
    }
    
    // Trim whitespace
    char *start = result;
    while (*start && isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        *end = '\0';
        end--;
    }
    
    // Move trimmed result to beginning
    if (start != result) {
        memmove(result, start, strlen(start) + 1);
    }
    
    return result;
}