// css_parser.c - BASED ON LEXBOR EXAMPLE
#include "css_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"
#include "network.h"
#include "cjson.h"
#include "layout_engine.h"
#include "pauk_tls.h"
#include "js_executor_quickjs.h"
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/selectors.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>

#include <lexbor/css/unit/const.h>
#include <lexbor/css/unit/res.h>
#include <lexbor/css/value/const.h>
#include <lexbor/css/value/res.h>

// Global CSS parser
static lxb_css_parser_t *css_parser = NULL;


// ============================================================================
// LEXBOR CSS UNIT HELPERS
// ============================================================================

// Get CSS unit data using the PUBLIC API
const lxb_css_data_t* get_css_unit_data(const char *unit_str) {
    if (!unit_str || strlen(unit_str) == 0) return NULL;
    size_t len = strlen(unit_str);
    
    // Use the proper public API function
    return lxb_css_unit_absolute_relative_by_name((lxb_char_t*)unit_str, len);
}

// Get CSS unit type as string (simplified version)
const char* get_css_unit_type_name(uintptr_t unit_id) {
    // Convert the unit ID to a string based on enum ranges
    // Use uintptr_t to match Lexbor's type system
    uintptr_t id = unit_id;
    
    if (id >= (uintptr_t)LXB_CSS_UNIT_ABSOLUTE__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_ABSOLUTE__LAST_ENTRY) {
        return "absolute";
    }
    if (id >= (uintptr_t)LXB_CSS_UNIT_RELATIVE__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_RELATIVE__LAST_ENTRY) {
        return "relative";
    }
    if (id >= (uintptr_t)LXB_CSS_UNIT_ANGLE__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_ANGLE__LAST_ENTRY) {
        return "angle";
    }
    if (id >= (uintptr_t)LXB_CSS_UNIT_FREQUENCY__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_FREQUENCY__LAST_ENTRY) {
        return "frequency";
    }
    if (id >= (uintptr_t)LXB_CSS_UNIT_RESOLUTION__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_RESOLUTION__LAST_ENTRY) {
        return "resolution";
    }
    if (id >= (uintptr_t)LXB_CSS_UNIT_DURATION__BEGIN && 
        id < (uintptr_t)LXB_CSS_UNIT_DURATION__LAST_ENTRY) {
        return "duration";
    }
    return "unknown";
}

static int selector_matches_element(const char *selector, lxb_dom_element_t *element) {
    if (!selector || !element) return 0;

    size_t len;
    // 1. ID Selector (#id)
    if (selector[0] == '#') {
        const lxb_char_t *id = lxb_dom_element_id(element, &len);
        return (id && strcmp(selector + 1, (char*)id) == 0);
    }
    
    // 2. Class Selector (.class)
    if (selector[0] == '.') {
        const lxb_char_t *cls = lxb_dom_element_class(element, &len);
        // Simple implementation: check if the class exists in the class string
        return (cls && strstr((char*)cls, selector + 1) != NULL);
    }
    
    // 3. Tag Selector (div, p, h1)
    const lxb_char_t *tag = lxb_dom_element_qualified_name(element, &len);
    return (tag && strcasecmp(selector, (char*)tag) == 0);
}

// Parse CSS value with proper unit detection
cJSON* parse_css_value_with_unit(const char *value_str) {
    if (!value_str) return NULL;
    
    cJSON *result = cJSON_CreateObject();
    if (!result) return NULL;
    
    // Store raw value
    cJSON_AddStringToObject(result, "raw", value_str);
    
    // Try to parse numeric value
    char *endptr;
    double num_value = strtod(value_str, &endptr);
    
    if (endptr != value_str) {
        cJSON_AddNumberToObject(result, "numeric", num_value);
        
        // Check for unit
        const char *unit_part = endptr;
        while (*unit_part == ' ') unit_part++; // Skip spaces
        
        if (strlen(unit_part) > 0) {
            // Use the CORRECT Lexbor API function
            const lxb_css_data_t *unit_data = get_css_unit_data(unit_part);
            
            if (unit_data) {
                cJSON_AddStringToObject(result, "unit", (char*)unit_data->name);
                
                // Get unit type from the unit ID
                // Note: lxb_css_data_t has a 'type' field in Lexbor
                uintptr_t unit_id = (uintptr_t)unit_data->unique;
                const char *type_str = get_css_unit_type_name(unit_id);
                cJSON_AddStringToObject(result, "unit_type", type_str);
                
                // Check for specific units
                if (strcasecmp((char*)unit_data->name, "px") == 0) {
                    cJSON_AddNumberToObject(result, "px", num_value);
                }
                else if (strcasecmp((char*)unit_data->name, "em") == 0) {
                    cJSON_AddNumberToObject(result, "em", num_value);
                }
                else if (strcasecmp((char*)unit_data->name, "rem") == 0) {
                    cJSON_AddNumberToObject(result, "rem", num_value);
                }
                else if (strcasecmp((char*)unit_data->name, "%") == 0) {
                    cJSON_AddNumberToObject(result, "percent", num_value);
                }
                else if (strcasecmp((char*)unit_data->name, "pt") == 0) {
                    cJSON_AddNumberToObject(result, "pt", num_value);
                }
            } else {
                // Not a standard unit, store as-is
                cJSON_AddStringToObject(result, "unit", unit_part);
            }
        } else {
            // No unit, just a number
            cJSON_AddStringToObject(result, "unit", "number");
        }
    } else {
        // Not a numeric value (could be a keyword like "auto", "inherit", etc.)
        cJSON_AddStringToObject(result, "type", "keyword");
        
        // Check for common keywords
        if (strcasecmp(value_str, "auto") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "auto");
        }
        else if (strcasecmp(value_str, "inherit") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "inherit");
        }
        else if (strcasecmp(value_str, "initial") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "initial");
        }
        else if (strcasecmp(value_str, "unset") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "unset");
        }
        else if (strcasecmp(value_str, "none") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "none");
        }
        else if (strcasecmp(value_str, "bold") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "bold");
        }
        else if (strcasecmp(value_str, "normal") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "normal");
        }
        else if (strcasecmp(value_str, "italic") == 0) {
            cJSON_AddStringToObject(result, "keyword_type", "italic");
        }
        // Add more keyword checks as needed
    }
    
    return result;
}


// Serialization callback
static lxb_status_t my_serialize_cb(const lxb_char_t *data, size_t len, void *ctx) {
    if (ctx) {
        lexbor_str_t *str = (lexbor_str_t *)ctx;
        size_t new_len = str->length + len;
        
        lxb_char_t *new_data = realloc(str->data, new_len + 1);
        if (!new_data) return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        
        memcpy(new_data + str->length, data, len);
        new_data[new_len] = '\0';
        str->data = new_data;
        str->length = new_len;
    } else {
        printf("%.*s", (int)len, (const char *)data);
    }
    
    return LXB_STATUS_OK;
}

// Enhanced function to parse CSS property with full Lexbor support
cJSON* parse_css_property_enhanced(const char *prop_name, const char *value_str) {
    if (!prop_name || !value_str) return NULL;
    
    cJSON *prop_obj = cJSON_CreateObject();
    if (!prop_obj) return NULL;
    
    // Basic info
    cJSON_AddStringToObject(prop_obj, "property", prop_name);
    cJSON_AddStringToObject(prop_obj, "raw_value", value_str);
    
    // Parse the value
    cJSON *parsed_value = parse_css_value_with_unit(value_str);
    if (parsed_value) {
        cJSON_AddItemToObject(prop_obj, "parsed", parsed_value);
        
        // Add specific info based on property type
        if (strcasecmp(prop_name, "font-size") == 0) {
            cJSON_AddStringToObject(prop_obj, "type", "font_size");
        }
        else if (strcasecmp(prop_name, "color") == 0) {
            cJSON_AddStringToObject(prop_obj, "type", "color");
            
            // Check color type
            cJSON *unit = cJSON_GetObjectItem(parsed_value, "unit");
            if (unit && strcmp(unit->valuestring, "hex") == 0) {
                cJSON_AddStringToObject(prop_obj, "color_format", "hex");
            }
        }
        else if (strcasecmp(prop_name, "width") == 0 || 
                 strcasecmp(prop_name, "height") == 0 ||
                 strcasecmp(prop_name, "margin") == 0 ||
                 strcasecmp(prop_name, "padding") == 0) {
            cJSON_AddStringToObject(prop_obj, "type", "dimension");
        }
    }
    
    return prop_obj;
}

// Get all CSS units as JSON for debugging
cJSON* get_all_css_units_json(void) {
    cJSON *units_array = cJSON_CreateArray();
    
    for (size_t i = 1; i < LXB_CSS_UNIT__LAST_ENTRY; i++) {
        const lxb_css_data_t *unit = &lxb_css_unit_data[i];
        
        cJSON *unit_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(unit_obj, "name", (char*)unit->name);
        cJSON_AddNumberToObject(unit_obj, "length", unit->length);
        cJSON_AddNumberToObject(unit_obj, "type", (double)unit->unique); 
        cJSON_AddStringToObject(unit_obj, "type_name", 
            get_css_unit_type_name(unit->unique)); 
        
        cJSON_AddItemToArray(units_array, unit_obj);
    }
    
    return units_array;
}



// CSS parser initialization based on Lexbor example
int css_parser_init(void) {
  //  printf("CSS PARSER: Initializing (Lexbor example style)...\n");
    
    if (css_parser != NULL) {
        return 1;
    }
    
    // Create parser
    css_parser = lxb_css_parser_create();
    if (css_parser == NULL) {
        fprintf(stderr, "CSS PARSER ERROR: Failed to create parser\n");
        return 0;
    }
    
    // Initialize parser (NULL means create default tokenizer)
    lxb_status_t status = lxb_css_parser_init(css_parser, NULL);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "CSS PARSER ERROR: Failed to initialize parser: %d\n", status);
        lxb_css_parser_destroy(css_parser, true);
        css_parser = NULL;
        return 0;
    }
    
    printf("CSS PARSER: Initialized successfully\n");
    return 1;
}

void css_parser_cleanup(void) {
    if (css_parser) {
        lxb_css_parser_destroy(css_parser, true);
        css_parser = NULL;
    }
}

// Get CSS property name
const char* get_css_property_name(lxb_css_property_type_t prop_id) {
    const lxb_css_entry_data_t *prop_data = lxb_css_property_by_id((uintptr_t)prop_id);
    return (prop_data && prop_data->name) ? (const char*)prop_data->name : "unknown";
}

// Get property data by name
const lxb_css_entry_data_t* get_css_property_by_name(const char *name) {
    return lxb_css_property_by_name((const lxb_char_t*)name, strlen(name));
}

// Parse inline CSS styles (simple)
cJSON* parse_inline_styles(const char *style_str, void *css_proc) {
    if (!style_str || strlen(style_str) == 0) return NULL;
    
    cJSON *styles = cJSON_CreateObject();
    if (!styles) return NULL;
    
    char *copy = strdup(style_str);
    if (!copy) {
        cJSON_Delete(styles);
        return NULL;
    }
    
    char *saveptr;
    char *declaration = strtok_r(copy, ";", &saveptr);
    int count = 0;
    
    while (declaration) {
        char *colon = strchr(declaration, ':');
        if (colon) {
            *colon = '\0';
            char *prop = declaration;
            char *value = colon + 1;
            
            // Trim property name
            while (*prop == ' ' || *prop == '\t') prop++;
            char *prop_end = prop + strlen(prop) - 1;
            while (prop_end > prop && (*prop_end == ' ' || *prop_end == '\t')) {
                *prop_end = '\0';
                prop_end--;
            }
            
            // Trim value
            while (*value == ' ' || *value == '\t') value++;
            char *val_end = value + strlen(value) - 1;
            while (val_end > value && 
                   (*val_end == ' ' || *val_end == '\t' || *val_end == '\n')) {
                *val_end = '\0';
                val_end--;
            }
            
            if (strlen(prop) > 0 && strlen(value) > 0) {
                // Check for !important
                int is_important = 0;
                char *important_pos = strstr(value, "!important");
                if (important_pos) {
                    is_important = 1;
                    *important_pos = '\0';
                    // Trim after removal
                    val_end = important_pos - 1;
                    while (val_end > value && (*val_end == ' ' || *val_end == '\t')) {
                        *val_end = '\0';
                        val_end--;
                    }
                }
                
                // ========== HANDLE SHORTHAND PROPERTIES ==========
                if (strcmp(prop, "padding") == 0 || strcmp(prop, "margin") == 0) {
                    // Handle multi-value padding/margin
                    char *copy_val = strdup(value);
                    if (copy_val) {
                        char *parts[4];
                        int part_count = 0;
                        char *token = strtok(copy_val, " ");
                        while (token && part_count < 4) {
                            parts[part_count++] = token;
                            token = strtok(NULL, " ");
                        }
                        
                        int top, right, bottom, left;
                        
                        if (part_count == 1) {
                            top = right = bottom = left = parse_css_length(parts[0], 0);
                        } else if (part_count == 2) {
                            top = bottom = parse_css_length(parts[0], 0);
                            right = left = parse_css_length(parts[1], 0);
                        } else if (part_count == 3) {
                            top = parse_css_length(parts[0], 0);
                            right = left = parse_css_length(parts[1], 0);
                            bottom = parse_css_length(parts[2], 0);
                        } else if (part_count >= 4) {
                            top = parse_css_length(parts[0], 0);
                            right = parse_css_length(parts[1], 0);
                            bottom = parse_css_length(parts[2], 0);
                            left = parse_css_length(parts[3], 0);
                        } else {
                            top = right = bottom = left = 0;
                        }
                        
                        free(copy_val);
                        
                        // Store individual properties with !important flag
                        char prop_top[64], prop_right[64], prop_bottom[64], prop_left[64];
                        snprintf(prop_top, sizeof(prop_top), "%s-top", prop);
                        snprintf(prop_right, sizeof(prop_right), "%s-right", prop);
                        snprintf(prop_bottom, sizeof(prop_bottom), "%s-bottom", prop);
                        snprintf(prop_left, sizeof(prop_left), "%s-left", prop);
                        
                        set_json_number(styles, prop_top, top);
                        set_json_number(styles, prop_right, right);
                        set_json_number(styles, prop_bottom, bottom);
                        set_json_number(styles, prop_left, left);
                        
                        // Store !important flag for each property
                        if (is_important) {
                            char important_key[64];
                            snprintf(important_key, sizeof(important_key), "%s-top_important", prop);
                            set_json_bool(styles, important_key, 1);
                            
                            snprintf(important_key, sizeof(important_key), "%s-right_important", prop);
                            set_json_bool(styles, important_key, 1);
                            
                            snprintf(important_key, sizeof(important_key), "%s-bottom_important", prop);
                            set_json_bool(styles, important_key, 1);
                            
                            snprintf(important_key, sizeof(important_key), "%s-left_important", prop);
                            set_json_bool(styles, important_key, 1);
                        }
                        
                        count++;
                    }
                }
                // ========== END SHORTHAND HANDLING ==========
                else {
                    // Normal property - store value and !important flag
                    set_json_string(styles, prop, value);
                    
                    if (is_important) {
                        char important_key[64];
                        snprintf(important_key, sizeof(important_key), "%s_important", prop);
                        set_json_bool(styles, important_key, 1);
                    }
                    
                    count++;
                }
            }
        }
        declaration = strtok_r(NULL, ";", &saveptr);
    }
    
    free(copy);
    
    if (count > 0) {
        set_json_number(styles, "property_count", count);
        return styles;
    } else {
        cJSON_Delete(styles);
        return NULL;
    }
}


// Parse styles from attribute
cJSON* parse_styles_from_attr(lxb_dom_attr_t *style_attr) {
    if (!style_attr) return NULL;
    
    size_t style_len;
    const lxb_char_t *style_value = lxb_dom_attr_value(style_attr, &style_len);
    
    if (!style_value || style_len == 0) return NULL;
    
    char *str = malloc(style_len + 1);
    if (!str) return NULL;
    
    memcpy(str, style_value, style_len);
    str[style_len] = '\0';
    
    // Add NULL as second parameter
    cJSON *result = parse_inline_styles(str, NULL);
    
    free(str);
    return result;
}

// Parse element styles
cJSON* parse_element_styles(lxb_dom_element_t *element) {
    if (!element) return NULL;
    
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_name(element, (lxb_char_t*)"style", 5);
    if (!style_attr) return NULL;
    
    return parse_styles_from_attr(style_attr);
}

// Parse stylesheet - CORRECT implementation based on Lexbor example
cJSON* parse_stylesheet_lxb(const lxb_char_t *css_text, size_t css_len)
{
    if (css_text == NULL || css_len == 0 || css_parser == NULL) {
        return NULL;
    }

  //  printf("Parsing stylesheet (%zu bytes)...\n", css_len);

    lxb_css_parser_clean(css_parser);
    lxb_css_stylesheet_t *stylesheet =
        lxb_css_stylesheet_parse(css_parser, css_text, css_len);

    if (stylesheet == NULL || stylesheet->root == NULL) {
        printf("Failed to parse stylesheet\n");
        return NULL;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "type", "stylesheet");
    cJSON_AddNumberToObject(result, "length", css_len);

    cJSON *rules_array = cJSON_CreateArray();
    int rule_count = 0;

    lxb_css_rule_t *root_rule = stylesheet->root;
    
    if (root_rule->type == LXB_CSS_RULE_LIST) {
        lxb_css_rule_list_t *rule_list = (lxb_css_rule_list_t *)root_rule;
        lxb_css_rule_t *rule = rule_list->first;
        
        while (rule != NULL) {
            // FIXED: Call parse_css_rule_complete and add to array
            cJSON *rule_json = parse_css_rule_complete(rule);
            if (rule_json) {
                cJSON_AddNumberToObject(rule_json, "index", rule_count);
                cJSON_AddItemToArray(rules_array, rule_json);
                rule_count++;
            }
            rule = rule->next;
        }
    } else {
        // Handle single rule
        cJSON *rule_json = parse_css_rule_complete(root_rule);
        if (rule_json) {
            cJSON_AddNumberToObject(rule_json, "index", 0);
            cJSON_AddItemToArray(rules_array, rule_json);
            rule_count = 1;
        }
    }

    if (rule_count > 0) {
        cJSON_AddItemToObject(result, "rules", rules_array);
        cJSON_AddNumberToObject(result, "rule_count", rule_count);
      //  printf("Successfully parsed %d CSS rules\n", rule_count);
    } else {
        cJSON_Delete(rules_array);
        printf("No CSS rules found in stylesheet\n");
    }

    lxb_css_stylesheet_destroy(stylesheet, true);
    return result;
}

// Parse CSS styles with Lexbor
cJSON* parse_css_styles_lxb(const lxb_char_t *css_text, size_t css_len) {
    if (!css_text || css_len == 0) return NULL;
    
    // For inline styles, use simple parser
    char *str = malloc(css_len + 1);
    if (!str) return NULL;
    
    memcpy(str, css_text, css_len);
    str[css_len] = '\0';
    
    cJSON *result = parse_inline_styles(str, NULL);
    free(str);
    
    return result;
}

// Extract stylesheets from document
void extract_and_parse_stylesheets(lxb_html_document_t *doc, cJSON *output_json) {
    if (!doc || !output_json) return;
    
    printf("Extracting stylesheets from document...\n");
    
    lxb_dom_node_t *root = lxb_dom_interface_node(doc);
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc);
    
    lxb_dom_collection_t *style_collection = lxb_dom_collection_make(dom_doc, 10);
    if (!style_collection) return;
    
    lxb_dom_elements_by_tag_name(lxb_dom_interface_element(root),
                                style_collection, (lxb_char_t*)"style", 5);
    
    size_t count = lxb_dom_collection_length(style_collection);
 //  printf("Found %zu style elements\n", count);
    
    if (count > 0) {
        cJSON *stylesheets = cJSON_CreateArray();
        
        for (size_t i = 0; i < count; i++) {
            lxb_dom_element_t *style_elem = lxb_dom_collection_element(style_collection, i);
            lxb_dom_node_t *style_node = lxb_dom_interface_node(style_elem);
            
            size_t css_len;
            lxb_char_t *css_text = lxb_dom_node_text_content(style_node, &css_len);
            
            if (css_text && css_len > 0) {
                cJSON *stylesheet = parse_stylesheet_lxb(css_text, css_len);
                if (stylesheet) {
                    cJSON_AddNumberToObject(stylesheet, "index", i);
                    cJSON_AddItemToArray(stylesheets, stylesheet);
                }
                
                lxb_dom_document_destroy_text(style_node->owner_document, css_text);
            }
        }
        
        if (cJSON_GetArraySize(stylesheets) > 0) {
            cJSON_AddItemToObject(output_json, "stylesheets", stylesheets);
        } else {
            cJSON_Delete(stylesheets);
        }
    }
    
    lxb_dom_collection_destroy(style_collection, true);
}


// Get the global parser (already exists)
lxb_css_parser_t* get_global_css_parser(void) {
    return css_parser;
}

// Parse and apply styles from <style> tags
int parse_and_apply_document_styles(lxb_html_document_t *doc) {
    if (!css_parser) return 0;
    
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc);
    lxb_dom_node_t *root = lxb_dom_interface_node(doc);
    
    // Find all <style> elements
    lxb_dom_collection_t *style_collection = lxb_dom_collection_make(dom_doc, 10);
    if (!style_collection) return 0;
    
    lxb_dom_elements_by_tag_name(lxb_dom_interface_element(root),
                                style_collection, (lxb_char_t*)"style", 5);
    
    size_t style_count = lxb_dom_collection_length(style_collection);
   // printf("Found %zu style elements in document\n", style_count);
    
    // For now, just extract CSS text - not applying it yet
    for (size_t i = 0; i < style_count; i++) {
        lxb_dom_element_t *style_elem = lxb_dom_collection_element(style_collection, i);
        lxb_dom_node_t *style_node = lxb_dom_interface_node(style_elem);
        
        size_t css_len;
        lxb_char_t *css_text = lxb_dom_node_text_content(style_node, &css_len);
        
        if (css_text && css_len > 0) {
        //    printf("Style block %zu: %zu bytes\n", i, css_len);
            
            // Parse the stylesheet
            lxb_css_stylesheet_t *stylesheet = 
                lxb_css_stylesheet_parse(css_parser, css_text, css_len);
            
            if (stylesheet) {
              //  printf("Successfully parsed stylesheet with %p\n", (void*)stylesheet);
                // Could process rules here later
                lxb_css_stylesheet_destroy(stylesheet, true);
            }
        }
        
        if (css_text) {
            lxb_dom_document_destroy_text(dom_doc, css_text);
        }
    }
    
    lxb_dom_collection_destroy(style_collection, true);
    return 1;
}


char* clean_css_text(const char *css) {
    if (!css) return NULL;
    
    // Remove leading/trailing whitespace
    const char *start = css;
    while (*start && (*start == ' ' || *start == '\n' || *start == '\t')) {
        start++;
    }
    
    const char *end = css + strlen(css) - 1;
    while (end > start && (*end == ' ' || *end == '\n' || *end == '\t')) {
        end--;
    }
    
    size_t len = end - start + 1;
    char *cleaned = malloc(len + 1);
    if (!cleaned) return NULL;
    
    memcpy(cleaned, start, len);
    cleaned[len] = '\0';
    
    return cleaned;
}



// Apply styles from stylesheet to elements
static void apply_stylesheet_to_element(cJSON *stylesheet_json, lxb_dom_element_t *element, cJSON *element_styles) {
    cJSON *rules = cJSON_GetObjectItem(stylesheet_json, "rules");
    if (!rules) return;

    cJSON *rule;
    cJSON_ArrayForEach(rule, rules) {
        cJSON *selectors = cJSON_GetObjectItem(rule, "selectors");
        cJSON *selector;
        bool is_match = false;

        cJSON_ArrayForEach(selector, selectors) {
            if (selector_matches_element(selector->valuestring, element)) {
                is_match = true;
                break;
            }
        }

        if (is_match) {
            cJSON *declarations = cJSON_GetObjectItem(rule, "declarations");
            cJSON *decl;
            cJSON_ArrayForEach(decl, declarations) {
                const char *prop = cJSON_GetObjectItem(decl, "property")->valuestring;
                const char *val = cJSON_GetObjectItem(decl, "value")->valuestring;
                // Use ReplaceItem to ensure the latest rule (or higher specificity) wins
                cJSON_ReplaceItemInObject(element_styles, prop, cJSON_CreateString(val));
            }
        }
    }
}

/*
// Simple CSS selector matching
static int selector_matches_element(const char *selector, 
    const char *tag,
    const char *id,
    const char *classes) {
if (!selector) return 0;

// Basic selectors only
if (selector[0] == '*') {
return 1; // Universal selector
}

// Tag selector
if (tag && strcmp(selector, tag) == 0) {
return 1;
}

// ID selector (#id)
if (selector[0] == '#') {
if (id && strcmp(selector + 1, id) == 0) {
return 1;
}
}

// Class selector (.class)
if (selector[0] == '.') {
if (classes && strstr(classes, selector + 1)) {
return 1;
}
}

return 0;
}

*/

// Get computed styles for element (considering style tag + inline)
cJSON* get_computed_styles_for_element(lxb_dom_element_t *element, 
    cJSON *all_stylesheets) {
if (!element) return NULL;

cJSON *computed = cJSON_CreateObject();
if (!computed) return NULL;

// 1. Start with default styles
cJSON_AddStringToObject(computed, "display", "block");
cJSON_AddStringToObject(computed, "position", "static");
cJSON_AddStringToObject(computed, "font-size", "16px");

// 2. Apply styles from <style> tags (if any)
if (all_stylesheets && cJSON_IsArray(all_stylesheets)) {
int sheet_count = cJSON_GetArraySize(all_stylesheets);
for (int i = 0; i < sheet_count; i++) {
cJSON *sheet = cJSON_GetArrayItem(all_stylesheets, i);
apply_stylesheet_to_element(sheet, element, computed);
}
}

// 3. Apply inline styles (highest priority)
lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(element, LXB_DOM_ATTR_STYLE);
if (style_attr) {
cJSON *inline_styles = parse_styles_from_attr(style_attr);
if (inline_styles) {
cJSON *item = inline_styles->child;
while (item) {
if (cJSON_IsString(item)) {
// Check if !important is in the value
cJSON *details = cJSON_GetObjectItem(inline_styles, 
                    "important_details");
int is_important = 0;
if (details) {
cJSON *important_item = cJSON_GetObjectItem(details, item->string);
if (important_item) {
cJSON *important_flag = cJSON_GetObjectItem(important_item, "important");
if (cJSON_IsTrue(important_flag)) {
is_important = 1;
}
}
}

// Apply inline style (overwrites previous)
cJSON_AddStringToObject(computed, item->string, item->valuestring);
if (is_important) {
// Mark as important
char important_key[256];
snprintf(important_key, sizeof(important_key), 
"%s_important", item->string);
cJSON_AddBoolToObject(computed, important_key, true);
}
}
item = item->next;
}
cJSON_Delete(inline_styles);
}
}

return computed;
}

// Collect all stylesheets from document
cJSON* collect_all_stylesheets(lxb_html_document_t *doc) {
if (!doc) return NULL;

lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc);
lxb_dom_node_t *root = lxb_dom_interface_node(doc);

lxb_dom_collection_t *style_collection = lxb_dom_collection_make(dom_doc, 10);
if (!style_collection) return NULL;

lxb_dom_elements_by_tag_name(lxb_dom_interface_element(root),
style_collection, (lxb_char_t*)"style", 5);

size_t count = lxb_dom_collection_length(style_collection);
cJSON *all_sheets = cJSON_CreateArray();

for (size_t i = 0; i < count; i++) {
lxb_dom_element_t *style_elem = lxb_dom_collection_element(style_collection, i);
lxb_dom_node_t *style_node = lxb_dom_interface_node(style_elem);

size_t css_len;
lxb_char_t *css_text = lxb_dom_node_text_content(style_node, &css_len);

if (css_text && css_len > 0) {
// Parse the stylesheet
cJSON *stylesheet = parse_stylesheet_lxb(css_text, css_len);
if (stylesheet) {
    cJSON_AddNumberToObject(stylesheet, "index", i);
    cJSON_AddStringToObject(stylesheet, "source", "style_tag");
    cJSON_AddItemToArray(all_sheets, stylesheet);
}

lxb_dom_document_destroy_text(style_node->owner_document, css_text);
}
}

lxb_dom_collection_destroy(style_collection, true);

if (cJSON_GetArraySize(all_sheets) > 0) {
return all_sheets;
} else {
cJSON_Delete(all_sheets);
return NULL;
}
}



cJSON* parse_css_rule_complete(lxb_css_rule_t *rule) {
    if (!rule) return NULL;
    
    cJSON *rule_json = cJSON_CreateObject();
    if (!rule_json) return NULL;
    
    const char *type_name = "unknown";
    
    switch (rule->type) {
    case LXB_CSS_RULE_STYLE:
        type_name = "style_rule";
        parse_style_rule_complete((lxb_css_rule_style_t *)rule, rule_json);
        break;
        
    case LXB_CSS_RULE_AT_RULE:
        type_name = "at_rule";
        parse_at_rule_complete((lxb_css_rule_at_t *)rule, rule_json);
        break;
        
    case LXB_CSS_RULE_BAD_STYLE:
        type_name = "bad_style_rule";
        break;
        
    case LXB_CSS_RULE_LIST:
        type_name = "rule_list";
        break;
        
    case LXB_CSS_RULE_DECLARATION:
        type_name = "declaration";
        break;
        
    case LXB_CSS_RULE_DECLARATION_LIST:
        type_name = "declaration_list";
        break;
        
    default:
        type_name = "unknown_rule";
        cJSON_AddNumberToObject(rule_json, "type_code", rule->type);
        break;
    }
    
    cJSON_AddStringToObject(rule_json, "type", type_name);
    cJSON_AddNumberToObject(rule_json, "type_code", rule->type);
    
    return rule_json;
}


void parse_style_rule_complete(lxb_css_rule_style_t *style_rule, cJSON *rule_json) {
    if (!style_rule || !rule_json) return;
    
    // Parse SELECTORS
    if (style_rule->selector && style_rule->selector->first) {
        cJSON *selectors_array = cJSON_CreateArray();
        lxb_css_selector_t *selector = style_rule->selector->first;
        int sel_count = 0;
        
        while (selector) {
            // ===== DODATAK ZA :root i :host SELEKTORE =====
            char *selector_text = NULL;
            
            // Pokušaj sa serijalizacijom
            lexbor_str_t sel_str = {0};
            if (lxb_css_selector_serialize(selector, my_serialize_cb, &sel_str) == LXB_STATUS_OK && 
                sel_str.data != NULL && sel_str.length > 0) {
                
                // Ako je selektor prazan ili sadrži samo ":" ili ":root" ili ":host", zameni ga
                if (sel_str.length == 1 && sel_str.data[0] == ':') {
                    selector_text = strdup("html");
                    if (INFO_MESSAGES) printf("🔧 [CSS] Zamenjen prazan pseudo-selektor sa html\n");
                } else if (strcmp((char *)sel_str.data, ":root") == 0) {
                    selector_text = strdup("html");
                    if (INFO_MESSAGES) printf("🔧 [CSS] Zamenjen :root sa html\n");
                } else if (strcmp((char *)sel_str.data, ":host") == 0) {
                    selector_text = strdup("body");
                    if (INFO_MESSAGES) printf("🔧 [CSS] Zamenjen :host sa body\n");
                } else {
                    selector_text = strdup((char *)sel_str.data);
                }
                lexbor_free(sel_str.data);
            }
            
            if (selector_text) {
                cJSON_AddItemToArray(selectors_array, cJSON_CreateString(selector_text));
                free(selector_text);
                sel_count++;
            }
            
            selector = selector->next;
        }
        
        if (sel_count > 0) {
            cJSON_AddItemToObject(rule_json, "selectors", selectors_array);
            cJSON_AddNumberToObject(rule_json, "selector_count", sel_count);
        } else {
            cJSON_Delete(selectors_array);
        }
    }
    
    // Parse DECLARATIONS (ostaje isto)
    if (style_rule->declarations) {
        cJSON *declarations_array = cJSON_CreateArray();
        lxb_css_rule_declaration_list_t *decl_list = style_rule->declarations;
        lxb_css_rule_declaration_t *decl = (lxb_css_rule_declaration_t *)decl_list->first;
        int decl_count = 0;
        
        while (decl) {
            cJSON *decl_json = parse_declaration_complete(decl);
            if (decl_json) {
                cJSON_AddItemToArray(declarations_array, decl_json);
                decl_count++;
            }
            decl = (lxb_css_rule_declaration_t *)decl->rule.next;
        }
        
        if (decl_count > 0) {
            cJSON_AddItemToObject(rule_json, "declarations", declarations_array);
            cJSON_AddNumberToObject(rule_json, "declaration_count", decl_count);
        } else {
            cJSON_Delete(declarations_array);
        }
    }
}


cJSON* parse_declaration_complete(lxb_css_rule_declaration_t *decl) {
    if (!decl) return NULL;
    
    // Use your existing parse_declaration logic from parse_stylesheet_lxb
    cJSON *decl_json = cJSON_CreateObject();
    
    // Get property name
    const lxb_css_entry_data_t *prop_data = lxb_css_property_by_id(decl->type);
    if (prop_data && prop_data->name) {
        cJSON_AddStringToObject(decl_json, "property", (char*)prop_data->name);
    }
    
    // Check !important
    if (decl->important) {
        cJSON_AddBoolToObject(decl_json, "important", true);
    }
    
    // Serialize value
    lexbor_str_t val_str = {0};
    if (lxb_css_rule_declaration_serialize(decl, my_serialize_cb, &val_str) == LXB_STATUS_OK && 
        val_str.data != NULL) {
        
        char *full_text = (char *)val_str.data;
        char *colon = strchr(full_text, ':');
        
        if (colon) {
            char *value_start = colon + 1;
            while (*value_start == ' ') value_start++;
            
            char *value_end = value_start + strlen(value_start) - 1;
            while (value_end > value_start && (*value_end == ';' || *value_end == ' ')) {
                *value_end = '\0';
                value_end--;
            }
            
            if (strlen(value_start) > 0) {
                cJSON_AddStringToObject(decl_json, "value", value_start);
            }
        }
        
        lexbor_free(val_str.data);
    }
    
    return decl_json;
}


void parse_media_rule_complete(lxb_css_at_rule_media_t *media_rule, cJSON *rule_json) {
    if (!media_rule || !rule_json) return;
    
    // Parse media query
    lexbor_str_t media_str = {0};
    if (lxb_css_at_rule_media_serialize(media_rule, my_serialize_cb, &media_str) == LXB_STATUS_OK && 
        media_str.data != NULL) {
        
        char *media_text = strdup((char *)media_str.data);
        cJSON_AddStringToObject(rule_json, "media_query", media_text);
        free(media_text);
        lexbor_free(media_str.data);
    }
}


void parse_at_rule_complete(lxb_css_rule_at_t *at_rule, cJSON *rule_json) {
    if (!at_rule || !rule_json) return;
    
    // Get at-rule type name
    const char *at_rule_name = "unknown";
    switch (at_rule->type) {
    case LXB_CSS_AT_RULE_MEDIA:
        at_rule_name = "media";
        parse_media_rule_complete(at_rule->u.media, rule_json);
        break;
    case LXB_CSS_AT_RULE_NAMESPACE:
        at_rule_name = "namespace";
        // Handle namespace
        break;
    case LXB_CSS_AT_RULE__CUSTOM:
        at_rule_name = "custom";
        break;
    case LXB_CSS_AT_RULE__UNDEF:
        at_rule_name = "undef";
        break;
    }
    
    cJSON_AddStringToObject(rule_json, "at_rule_type", at_rule_name);
    cJSON_AddNumberToObject(rule_json, "at_rule_type_code", at_rule->type);
    
    // Serialize the at-rule
    lexbor_str_t serialized = {0};
    if (lxb_css_rule_at_serialize(at_rule, my_serialize_cb, &serialized) == LXB_STATUS_OK && 
        serialized.data != NULL) {
        
        char *text = strdup((char *)serialized.data);
        cJSON_AddStringToObject(rule_json, "raw_text", text);
        free(text);
        lexbor_free(serialized.data);
    }
}

//u utf8 konverzija
void utf8_to_ascii(char *s)
{
    unsigned char *r = (unsigned char *)s;
    unsigned char *w = (unsigned char *)s;

    while (*r) {
        long code = 0;

        // ======================================================
        // Special case: &amp;#160; should become &#160; literally
        // ======================================================
        if (strncmp((char*)r, "&amp;#", 6) == 0) {
            *w++ = '&';
            r += 5; // consume only "&amp;"
            continue;
        }

        // ===== Handle named entities =====
        if (strncmp((char*)r, "&amp;#", 6) == 0) { *w++ = '&'; *w++ = '#'; r += 6; continue;}
        if (strncmp((char*)r, "&lt;", 4) == 0)   { *w++='<'; r+=4; continue; }
        if (strncmp((char*)r, "&gt;", 4) == 0)   { *w++='>'; r+=4; continue; }
        if (strncmp((char*)r, "&quot;", 6) == 0) { *w++='"'; r+=6; continue; }
        if (strncmp((char*)r, "&apos;", 6) == 0) { *w++='\''; r+=6; continue; }

        if (strncmp((char*)r, "&nbsp;", 6) == 0) { *w++=' '; r+=6; continue; }
        if (strncmp((char*)r, "&copy;", 6) == 0) { *w++=(unsigned char)0xA9; r+=6; continue; }
        if (strncmp((char*)r, "&euro;", 6) == 0) { *w++=(unsigned char)0x80; r+=6; continue; }

        // ===== Handle numeric entities =====
        if (r[0]=='&' && r[1]=='#') {
            unsigned char *end = r+2;
            unsigned char *num_start = end;
            int is_hex = 0;

            if ((*end=='x'||*end=='X') && isxdigit(end[1])) {
                is_hex = 1;
                end++;
                num_start = end;
            }

            if (is_hex) {
                while (isxdigit(*end)) {
                    char c=*end;
                    if (c>='0' && c<='9') code=code*16+(c-'0');
                    else if (c>='a' && c<='f') code=code*16+(c-'a'+10);
                    else if (c>='A' && c<='F') code=code*16+(c-'A'+10);
                    end++;
                }
            } else {
                while (isdigit(*end)) {
                    code=code*10+(*end-'0');
                    end++;
                }
            }

            if (end > num_start && *end == ';') {

                if (code == 160) { *w++ = '&'; *w++ = '#'; *w++ = '1'; *w++ = '6'; *w++ = '0'; *w++ = ';';}
                else if (code == 169) *w++ = (unsigned char)0xA9;     // ©
                else if (code == 8364) *w++ = (unsigned char)0x80;    // €
                else if (code < 256) *w++ = (unsigned char)code;
                else *w++ = '?';

                r = end + 1;
                continue;
            }
        }

        // ===== UTF-8 handling =====

        // © U+00A9 = C2 A9
        if (r[0]==0xC2 && r[1]==0xA9) {
            *w++ = (unsigned char)0xA9;
            r += 2;
            continue;
        }

        // € U+20AC = E2 82 AC
        if (r[0]==0xE2 && r[1]==0x82 && r[2]==0xAC) {
            *w++ = (unsigned char)0x80;
            r += 3;
            continue;
        }

        // Generic Latin-1 range C2 xx
        if (r[0]==0xC2 && r[1]) {
            *w++ = r[1];
            r += 2;
            continue;
        }

        // Generic Latin-1 range C3 xx
        if (r[0]==0xC3 && r[1]) {
            *w++ = r[1] + 0x40;
            r += 2;
            continue;
        }

        // Otherwise copy byte
        *w++ = *r++;
    }

    *w = '\0';
}


void add_default_css_rules(void) {
    printf("=== Adding default CSS rules ===\n");
        // Default margins for block elements (as specified by HTML standards)
        add_css_rule("p", "margin-top", "1em");
        add_css_rule("p", "margin-bottom", "1em");
        add_css_rule("h1", "margin-top", "0.67em");
        add_css_rule("h1", "margin-bottom", "0.67em");
        add_css_rule("h2", "margin-top", "0.83em");
        add_css_rule("h2", "margin-bottom", "0.83em");
        add_css_rule("h3", "margin-top", "1em");
        add_css_rule("h3", "margin-bottom", "1em");
        add_css_rule("div", "margin-top", "0");
        add_css_rule("div", "margin-bottom", "0");
        add_css_rule("ul", "margin-top", "1em");
        add_css_rule("ul", "margin-bottom", "1em");
        add_css_rule("li", "margin-top", "0");
        add_css_rule("li", "margin-bottom", "0");
        
    add_css_rule("body", "max-width", "100%");
    add_css_rule("body", "overflow-x", "hidden");
    add_css_rule("p", "word-wrap", "break-word");
    add_css_rule("p", "white-space", "normal");
    add_css_rule("div", "word-wrap", "break-word");
    add_css_rule("div", "white-space", "normal");
    add_css_rule("span", "word-wrap", "break-word");
    add_css_rule("span", "white-space", "normal");
    add_css_rule("article", "word-wrap", "break-word");
    add_css_rule("article", "white-space", "normal");
    add_css_rule("section", "word-wrap", "break-word");
    add_css_rule("section", "white-space", "normal");
    add_css_rule("html", "max-width", "100%");
    add_css_rule("html", "overflow-x", "hidden");
    

}


void fetch_and_parse_external_css(const char *css_url, const char *base_url) {
  //  printf("📁 fetch_and_parse_external_css: url='%s', base='%s'\n", css_url, base_url);
    
    // Resolve relative URL
    char *absolute_url = resolve_relative_url(base_url, css_url);
    if (!absolute_url) {
        printf("❌ Failed to resolve URL\n");
        return;
    }
    
 //   printf("   Absolute URL: %s\n", absolute_url);
    
    // Fetch the CSS
    char *css_content = NULL;
    size_t css_size = 0;
    
    if (fetch_css_resource(absolute_url, &css_content, &css_size) == EOK) {
    //    printf("✅ Fetched CSS (%zu bytes)\n", css_size);
        parse_css_and_add_rules(css_content);
        free(css_content);
    } else {
        printf("❌ Failed to fetch CSS from %s\n", absolute_url);
    }
    
    free(absolute_url);
}


// Parse CSS from a string and add rules to the global css_rules array
void parse_css_string(const char *css_content, const char *source_url) {
    if (!css_content || strlen(css_content) == 0) return;
    
    // Use your existing parse_stylesheet_lxb function
    cJSON *stylesheet = parse_stylesheet_lxb((lxb_char_t*)css_content, strlen(css_content));
    
    if (!stylesheet) {
        printf("❌ Failed to parse CSS from %s\n", source_url ? source_url : "unknown");
        return;
    }
    
    cJSON *rules = cJSON_GetObjectItem(stylesheet, "rules");
    if (rules && cJSON_IsArray(rules)) {

        
        for (int r = 0; r < cJSON_GetArraySize(rules); r++) {
            cJSON *rule = cJSON_GetArrayItem(rules, r);
            cJSON *selectors = cJSON_GetObjectItem(rule, "selectors");
            cJSON *declarations = cJSON_GetObjectItem(rule, "declarations");
            
            if (selectors && declarations && 
                cJSON_IsArray(selectors) && cJSON_IsArray(declarations)) {
                
                // Reconstruct selector string
                char full_selector[512] = "";
                for (int s = 0; s < cJSON_GetArraySize(selectors); s++) {
                    cJSON *sel = cJSON_GetArrayItem(selectors, s);
                    if (cJSON_IsString(sel)) {
                        if (s > 0 && sel->valuestring[0] != ':') {
                            strcat(full_selector, " ");
                        }
                        strcat(full_selector, sel->valuestring);
                    }
                }
                
                // Add each declaration to global CSS rules
                for (int d = 0; d < cJSON_GetArraySize(declarations); d++) {
                    cJSON *decl = cJSON_GetArrayItem(declarations, d);
                    cJSON *prop = cJSON_GetObjectItem(decl, "property");
                    cJSON *val = cJSON_GetObjectItem(decl, "value");
                    
                    if (prop && val && cJSON_IsString(prop) && cJSON_IsString(val)) {
                        // Clean value (remove !important etc)
                        char clean_value[256];
                        strncpy(clean_value, val->valuestring, sizeof(clean_value) - 1);
                        clean_value[sizeof(clean_value) - 1] = '\0';
                        
                        char *important = strstr(clean_value, " !important");
                        if (important) *important = '\0';
                        
                        add_css_rule(full_selector, prop->valuestring, clean_value);
                    }
                }
            }
        }
    }
    
    cJSON_Delete(stylesheet);
}
