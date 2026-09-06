#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fibril.h>
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>

#include "cjson.h"
#include "css_parser.h"
#include "js_executor_quickjs.h"
#include "forms_parser.h"
#include "tables_parser.h"
#include "lists_parser.h"
#include "menus_parser.h"
#include "img_parser.h"
#include "media_parser.h"
#include "link_parsing.h"
#include "headings_parser.h"

#include "render_func.h"
#include "pauk_tls.h"
#include "url_utils.h"

#include "main.h"
#include "layout_engine.h"
#include "gui.h"
#include "font_manager.h"
#include "text_rules.h"
#include "bookmarks.h"
#include "buttons.h"
#include "image_cache.h"
#include "image_downloader.h"
#include "time_utils.h"
#include "resource_filter.h"
#include "rendering_elements/defaults.h"

//#include "mbedssl/library/psa/crypto.h"



cJSON *global_computed_layout = NULL;
static DocumentOutline global_document_outline;
cJSON *global_css_stylesheets = NULL;
static int g_next_element_id = 1; 
 int hover_timer_running=0;
static int enable_js = 1;// Set to 0 to disable JavaScript
//static int viewport_width = 800;
cJSON *rendering_output = NULL;
JSContext *js_ctx = NULL;
lxb_html_document_t *current_doc = NULL;
char *g_current_base_url = NULL;
int g_max_events_processed = 500;  
static perf_timing_t page_timing;
extern cJSON *global_document_json;
// Simplified element structure for rendering
typedef struct {
    const char *tag;
    const char *type;      // "block", "inline", "image", "table_ref", "form_ref", "menu_ref"
    const char *text;
    const char *id;
    const char *bg_color;
    const char *border_color;
    const char *color;
    const char *font_style;
    const char *font_weight;
    const char *text_decoration;
    const char *style;
    const char *href;
    const char *src;
    int font_size;
    int x;
    int y;
    const char *width;
    const char *height;
} RenderingElement;




// Global array to store all CSS rules
CSSRule *css_rules = NULL;
int css_rule_count = 0;

static inline void srbinos_delay(unsigned int ms) {
    fibril_usleep(ms * 1000);
}




void add_css_rule(const char *selector, const char *property, const char *value) {
    css_rules = realloc(css_rules, (css_rule_count + 1) * sizeof(CSSRule));
    css_rules[css_rule_count].selector = strdup(selector);
    css_rules[css_rule_count].property = strdup(property);
    css_rules[css_rule_count].value = strdup(value);
    css_rule_count++;
    
   // printf("[CSS] Stored rule: %s { %s: %s }\n", selector, property, value);
}

const char* get_current_base_url(void) {
    return g_current_base_url;
}

void set_current_base_url(const char *url) {
    printf("🔵 set_current_base_url called with: '%s'\n", url ? url : "(NULL)");
    
    if (g_current_base_url) {
        printf("   freeing old: '%s'\n", g_current_base_url);
        free(g_current_base_url);
    }
    
    g_current_base_url = url ? strdup(url) : NULL;
    printf("   new g_current_base_url = '%s'\n", g_current_base_url ? g_current_base_url : "(NULL)");
}


void apply_all_css_rules_to_element(cJSON *element) {
    if (!element || css_rule_count == 0) return;

    // Get element properties for matching
    const char *element_tag = "";
    const char *element_id = "";
    const char *element_classes = "";

    cJSON *tag_item = cJSON_GetObjectItem(element, "tag");
    if (tag_item && cJSON_IsString(tag_item)) {
        element_tag = tag_item->valuestring;
    }

    cJSON *id_item = cJSON_GetObjectItem(element, "id");
    if (id_item && cJSON_IsString(id_item)) {
        element_id = id_item->valuestring;
    }

    cJSON *class_item = cJSON_GetObjectItem(element, "class_string");
    if (class_item && cJSON_IsString(class_item)) {
        element_classes = class_item->valuestring;
    }

    if(INFO_MESSAGES) printf("[CSS] Checking rules for element: tag='%s', id='%s'\n", 
                          element_tag, element_id);

    // Check all CSS rules
    for (int i = 0; i < css_rule_count; i++) {
        CSSRule *rule = &css_rules[i];
        int matches = 0;

        // ID selector (#div1)
        if (rule->selector[0] == '#') {
            if (strcmp(rule->selector + 1, element_id) == 0) {
                matches = 1;
                if(INFO_MESSAGES) printf("[CSS] ✓ ID match: %s\n", rule->selector);
            }
        }
        // Class selector (.myclass)
        else if (rule->selector[0] == '.') {
            if (element_classes && strstr(element_classes, rule->selector + 1) != NULL) {
                matches = 1;
                if(INFO_MESSAGES) printf("[CSS] ✓ Class match: %s\n", rule->selector);
            }
        }
        // Tag selector (div, p, etc.)
        else {
            if (strcmp(rule->selector, element_tag) == 0) {
                matches = 1;
                if(INFO_MESSAGES) printf("[CSS] ✓ Tag match: %s\n", rule->selector);
            }
        }

        // Apply matching rule
        if (matches) {
            if(INFO_MESSAGES) printf("[CSS] Applying: %s { %s: %s }\n", 
                                  rule->selector, rule->property, rule->value);

            // Map ALL CSS properties to JSON fields
            if (strcmp(rule->property, "background-color") == 0) {
                set_json_string(element, "bg_color", rule->value);
            }
            else if (strcmp(rule->property, "color") == 0) {
                set_json_string(element, "color", rule->value);
            }
            else if (strcmp(rule->property, "border") == 0) {
                // Example: "1px solid #3366CC"
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = 0;
            
                char *w = strtok(tmp, " ");
                char *s = strtok(NULL, " ");
                char *c = strtok(NULL, " ");
            
                if (w) set_json_string(element, "border_width", w);
                if (s) set_json_string(element, "border_style", s);
                if (c) set_json_string(element, "border_color", c);
            }
            else if (strcmp(rule->property, "width") == 0) {
                set_json_string(element, "width", rule->value);
                set_json_string(element, "attr_width", rule->value);
            }
            else if (strcmp(rule->property, "height") == 0) {
                set_json_string(element, "height", rule->value);
                set_json_string(element, "attr_height", rule->value);
            }
            else if (strcmp(rule->property, "padding") == 0) {
                if (strstr(rule->value, "px")) {
                    int padding_val = atoi(rule->value);
                    set_json_number(element, "padding", padding_val);
                    set_json_number(element, "padding_top", padding_val);
                    set_json_number(element, "padding_right", padding_val);
                    set_json_number(element, "padding_bottom", padding_val);
                    set_json_number(element, "padding_left", padding_val);
                }
            }
            else if (strcmp(rule->property, "margin-bottom") == 0) {
                if (strstr(rule->value, "px")) {
                    int margin_val = atoi(rule->value);
                    set_json_number(element, "margin_bottom", margin_val);
                }
            }
            else if (strcmp(rule->property, "font-size") == 0) {
                if (strstr(rule->value, "px")) {
                    int font_val = atoi(rule->value);
                    set_json_number(element, "font_size", font_val);
                }
            }
            else if (strcmp(rule->property, "font-weight") == 0) {
                set_json_string(element, "font_weight", rule->value);
            }
            else if (strcmp(rule->property, "display") == 0) {
                set_json_string(element, "display", rule->value);
            }
            // Add more as needed...
        }
    }
}
void apply_css_to_tree(cJSON *element) {
    if (!element) return;
    
    // Apply CSS to this element
    apply_all_css_rules_to_element(element);
    
    // Recursively apply to children
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            apply_css_to_tree(child);
        }
    }
}

// Function to extract and parse CSS from <style> tags
void extract_and_parse_css_styles(lxb_html_document_t *document, cJSON *output_json) {
    if (!document) return;
    
    if(INFO_MESSAGES) printf("\n=== EXTRACTING CSS FROM <STYLE> TAGS ===\n");
    
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(document);
    
    lxb_dom_collection_t *style_coll = lxb_dom_collection_make(dom_doc, 10);
    if (!style_coll) {
        printf("Failed to create collection for style tags\n");
        return;
    }
    
    lxb_dom_element_t *root = lxb_dom_document_element(dom_doc);
    lxb_dom_elements_by_tag_name(root, style_coll, (lxb_char_t*)"style", 5);
    
    size_t style_count = lxb_dom_collection_length(style_coll);
    if(INFO_MESSAGES) printf("Found %zu <style> tag(s)\n", style_count);
    
    cJSON *css_stylesheets = cJSON_CreateArray();
    
    for (size_t i = 0; i < style_count; i++) {
        lxb_dom_element_t *style_elem = lxb_dom_collection_element(style_coll, i);
        lxb_dom_node_t *style_node = lxb_dom_interface_node(style_elem);
        
        size_t css_len = 0;
        lxb_char_t *css_text = lxb_dom_node_text_content(style_node, &css_len);
        
        if (!css_text || css_len == 0) {
            if (css_text) lexbor_free(css_text);
            continue;
        }

        cJSON *stylesheet_json = parse_stylesheet_lxb(css_text, css_len);
            
        if (stylesheet_json) {
            cJSON *rules = cJSON_GetObjectItem(stylesheet_json, "rules");
            if (rules && cJSON_IsArray(rules)) {
                if(INFO_MESSAGES) printf("  Extracting CSS rules from stylesheet...\n");
                
                for (int r = 0; r < cJSON_GetArraySize(rules); r++) {
                    cJSON *rule = cJSON_GetArrayItem(rules, r);
                    cJSON *selectors = cJSON_GetObjectItem(rule, "selectors");
                    cJSON *declarations = cJSON_GetObjectItem(rule, "declarations");
                    
                    if (selectors && declarations && 
                        cJSON_IsArray(selectors) && cJSON_IsArray(declarations)) {
                        
                        // ===== FIXED: Scaled up to 2048 to safely handle massive stacked Next.js selectors =====
                        char full_selector[2048] = "";
                        int selector_count = cJSON_GetArraySize(selectors);
                        
                        for (int s = 0; s < selector_count; s++) {
                            cJSON *sel = cJSON_GetArrayItem(selectors, s);
                            if (!sel || !cJSON_IsString(sel)) continue;
                            
                            // Check bounds to ensure concatenation doesn't smash the stack
                            if (strlen(full_selector) + strlen(sel->valuestring) + 2 >= sizeof(full_selector)) {
                                break; 
                            }

                            if (sel->valuestring[0] == ':') {
                                strcat(full_selector, sel->valuestring);
                            } else {
                                if (s > 0 && full_selector[strlen(full_selector) - 1] != ' ') {
                                    strcat(full_selector, " ");
                                }
                                strcat(full_selector, sel->valuestring);
                            }
                        }
                        
                        for (int d = 0; d < cJSON_GetArraySize(declarations); d++) {
                            cJSON *decl = cJSON_GetArrayItem(declarations, d);
                            cJSON *property = cJSON_GetObjectItem(decl, "property");
                            cJSON *value = cJSON_GetObjectItem(decl, "value");
                            
                            if (property && value &&
                                cJSON_IsString(property) && 
                                cJSON_IsString(value)) {
                                
                                // FIXED: Scaled value copy allocation window to 512 bytes
                                char clean_value[512];
                                strncpy(clean_value, value->valuestring, sizeof(clean_value) - 1);
                                clean_value[sizeof(clean_value) - 1] = '\0';
                                
                                char *important_pos = strstr(clean_value, " !important");
                                if (important_pos) *important_pos = '\0';
                                
                                // Pass clean explicit string duplicates into your engine tracker 
                                // to stop stack parameter bleeding across loop layers
                                char *saved_prop = strdup(property->valuestring);
                                char *saved_val = strdup(clean_value);
                                char *saved_sel = strdup(full_selector);

                                if (saved_prop && saved_val && saved_sel) {
                                    add_css_rule(saved_sel, saved_prop, saved_val);
                                    
                                    if(INFO_MESSAGES) printf("  Added rule: '%s' { %s: %s }\n", 
                                                            saved_sel, saved_prop, saved_val);
                                }
                                
                                // Safely free localized string tokens after adding rules
                                if (saved_prop) free(saved_prop);
                                if (saved_val) free(saved_val);
                                if (saved_sel) free(saved_sel);
                            }
                        }
                    }
                }
            }
            
            cJSON_AddNumberToObject(stylesheet_json, "style_tag_index", i+1);
            cJSON_AddNumberToObject(stylesheet_json, "source_length", css_len);
            cJSON_AddItemToArray(css_stylesheets, stylesheet_json);
            
            if(INFO_MESSAGES) printf("  ✓ Successfully parsed stylesheet\n");
        } else {
            if(INFO_MESSAGES) printf("  ✗ Failed to parse stylesheet\n");
        }
        
    }
    
    if (cJSON_GetArraySize(css_stylesheets) > 0) {
        cJSON_AddItemToObject(output_json, "css_stylesheets", css_stylesheets);
        if(INFO_MESSAGES) printf("Added %d CSS stylesheets to output\n", 
                                cJSON_GetArraySize(css_stylesheets));
    } else {
        if(INFO_MESSAGES) printf("Brisem CSS stylesheet.\n");
        cJSON_Delete(css_stylesheets);
    }
    
    lxb_dom_collection_destroy(style_coll, true);
    if(INFO_MESSAGES) printf("Zavrsio extract and parse css.\n");
}

//kopiraj fajl
int kopiraj_fajl(const char *src_file) {
    if (!KOPIRAJ) return 0;
    
    // Extract just the filename from source path
    const char *filename = strrchr(src_file, '/');
    if (filename) {
        filename++; // Skip '/'
    } else {
        filename = src_file;
    }
    
    // Build destination path
    char dst_path[512];
    snprintf(dst_path, sizeof(dst_path), "/data/web/%s", filename);
    
   // printf("Copying %s -> %s\n", src_file, dst_path);
    
    FILE *src = fopen(src_file, "rb");
    if (!src) {
        printf("  ERROR: Cannot open source file\n");
        return -1;
    }
    
    // Get file size
    fseek(src, 0, SEEK_END);
    long size = ftell(src);
    fseek(src, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(src);
        printf("  WARNING: Source file is empty\n");
        return -1;
    }
    
    // Read entire file
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(src);
        return -1;
    }
    
    size_t read = fread(buffer, 1, size, src);
    fclose(src);
    
    if (read != (size_t)size) {
        free(buffer);
        printf("  ERROR: Read failed\n");
        return -1;
    }
    
    // Write to destination
    FILE *dst = fopen(dst_path, "wb");
    if (!dst) {
        free(buffer);
        printf("  ERROR: Cannot create destination file\n");
        return -1;
    }
    
    size_t written = fwrite(buffer, 1, size, dst);
    fclose(dst);
    free(buffer);
    
    if (written != (size_t)size) {
        printf("  ERROR: Write failed\n");
        return -1;
    }
    
    printf("  Success: %ld bytes copied\n", size);
    return 0;
}

// Convert Lexbor element to simple rendering Jelement_to_rendering_jsonSON
cJSON* element_to_rendering_json(lxb_dom_element_t *elem, int is_inline) {
    if (!elem) return NULL;
    
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    
    // Get tag name
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &len);
    char *tag = NULL;
    if (tag_name && len > 0) {
        tag = malloc(len + 1);
        memcpy(tag, tag_name, len);
        tag[len] = '\0';
        
        // Determine type based on tag and display
        const char *type = "block";
        
        // Inline elements
        const char *inline_tags[] = {
"span", "a","strong", "em", "b", "i", "u", "s",
"code", "kbd", "samp", "var","mark", "small",
"sub", "sup","abbr", "cite", "q","label",
"dfn", "time", "data","ruby", "rt", "rp",
"br", "wbr", "img", "svg", "math", NULL
        };
        
        for (int i = 0; inline_tags[i] != NULL; i++) {
            if (strcasecmp(tag, inline_tags[i]) == 0) {
                type = "inline";
                break;
            }
        }
        
        // Special element types
  
        if (strcasecmp(tag, "table") == 0) type = "table_ref";
        if (strcasecmp(tag, "form") == 0) type = "form_ref";
        if (strcasecmp(tag, "nav") == 0 || 
            strcasecmp(tag, "menu") == 0 || 
            strcasecmp(tag, "ul") == 0) type = "menu_ref";
        
        cJSON_AddStringToObject(json, "tag", tag);
        cJSON_AddStringToObject(json, "type", type);
        
        // Override with inline flag if provided
        if (is_inline) {
            cJSON_AddStringToObject(json, "type", "inline");
        }
        
        free(tag);
    }
    
    // Get ID
    const lxb_char_t *id = lxb_dom_element_id(elem, &len);
    if (id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, id, len);
        id_str[len] = '\0';
        if (strlen(id_str) > 0) {
            cJSON_AddStringToObject(json, "id", id_str);
        }
        free(id_str);
    }
    
    // Get simple text content (only direct text, not children)
    char *text = get_element_text_simple(elem);
    if (text && strlen(text) > 0) {
        // Trim whitespace
        char *start = text;
        while (*start && isspace((unsigned char)*start)) start++;
        
        char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(json, "text", start);
        }
    }
    if (text) free(text);
    
    // Get attributes for rendering
    lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(elem);
    while (attr) {
        size_t attr_len;
        const lxb_char_t *attr_name = lxb_dom_attr_qualified_name(attr, &attr_len);
        const lxb_char_t *attr_value = lxb_dom_attr_value(attr, NULL);
        
        if (attr_name && attr_value) {
            char *name_str = malloc(attr_len + 1);
            memcpy(name_str, attr_name, attr_len);
            name_str[attr_len] = '\0';
            
            char *value_str = lexbor_to_cstr(attr_value, strlen((char*)attr_value));
            
            if (name_str && value_str) {
                // Get href for links
                if (strcmp(name_str, "href") == 0) {
                    cJSON_AddStringToObject(json, "href", value_str);
                }
                // Get src for images
                else if (strcmp(name_str, "src") == 0) {
                    cJSON_AddStringToObject(json, "src", value_str);
                }
                // Get alt for images
                else if (strcmp(name_str, "alt") == 0) {
                    // Use alt as text if no other text
                    if (!cJSON_HasObjectItem(json, "text")) {
                        cJSON_AddStringToObject(json, "text", value_str);
                    }
                }
                
                free(name_str);
                free(value_str);
            }
        }
        
        attr = lxb_dom_element_next_attribute(attr);
    }
    
    // Get inline styles for rendering
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            // Extract common style properties
            cJSON *bg_color = cJSON_GetObjectItem(styles, "background-color");
            if (bg_color && cJSON_IsString(bg_color)) {
                cJSON_AddStringToObject(json, "bg_color", bg_color->valuestring);
            }
            
            cJSON *color = cJSON_GetObjectItem(styles, "color");
            if (color && cJSON_IsString(color)) {
                cJSON_AddStringToObject(json, "color", color->valuestring);
            }
            
            cJSON *font_size = cJSON_GetObjectItem(styles, "font-size");
            if (font_size && cJSON_IsString(font_size)) {
                // Convert font-size to integer if it's in px
                char *value = font_size->valuestring;
                char *endptr;
                int size = strtol(value, &endptr, 10);
                if (endptr != value && strstr(value, "px")) {
                    cJSON_AddNumberToObject(json, "font_size", size);
                }
            }
            
            cJSON_Delete(styles);
        }
    }
    
    // Set default values if not specified
    if (!cJSON_HasObjectItem(json, "font_size")) {
        // Default font sizes based on tag
        cJSON *tag_json = cJSON_GetObjectItem(json, "tag");
        if (tag_json && cJSON_IsString(tag_json)) {
            const char *tag = tag_json->valuestring;
            if (strcasecmp(tag, "h1") == 0) cJSON_AddNumberToObject(json, "font_size", 32);
            else if (strcasecmp(tag, "h2") == 0) cJSON_AddNumberToObject(json, "font_size", 24);
            else if (strcasecmp(tag, "h3") == 0) cJSON_AddNumberToObject(json, "font_size", 19);
            else if (strcasecmp(tag, "h4") == 0) cJSON_AddNumberToObject(json, "font_size", 16);
            else if (strcasecmp(tag, "h5") == 0) cJSON_AddNumberToObject(json, "font_size", 13);
            else if (strcasecmp(tag, "h6") == 0) cJSON_AddNumberToObject(json, "font_size", 11);
            else cJSON_AddNumberToObject(json, "font_size", 16);
        } else {
            cJSON_AddNumberToObject(json, "font_size", 16);
        }
    }
    
    // Set font style based on tag
    cJSON *tag_json = cJSON_GetObjectItem(json, "tag");
    if (tag_json && cJSON_IsString(tag_json)) {
        const char *tag = tag_json->valuestring;
        if (strcasecmp(tag, "b") == 0 || strcasecmp(tag, "strong") == 0) {
            cJSON_AddStringToObject(json, "font_style", "bold");
        } else if (strcasecmp(tag, "i") == 0 || strcasecmp(tag, "em") == 0) {
            cJSON_AddStringToObject(json, "font_style", "italic");
        } else if (strcasecmp(tag, "u") == 0) {
            cJSON_AddStringToObject(json, "style", "underline");
        }
    }
    
    // Default color if not specified
    if (!cJSON_HasObjectItem(json, "color")) {
        cJSON_AddStringToObject(json, "color", "#000000");
    }
    
    // Default background if not specified
    if (!cJSON_HasObjectItem(json, "bg_color")) {
        cJSON_AddStringToObject(json, "bg_color", "#ffffff");
    }
    
    return json;
}

// Get simple text content (no recursion)
char* get_element_text_simple(lxb_dom_element_t *elem) {
    if (!elem) return strdup("");
    
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    if (!node) return strdup("");
    
    // Get tag name
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
    char *tag = NULL;
    
    if (tag_name && tag_len > 0) {
        tag = malloc(tag_len + 1);
        memcpy(tag, tag_name, tag_len);
        tag[tag_len] = '\0';
        
        // ===== IMPROVED CHECK: Does this element have ELEMENT children? =====
        int has_element_children = 0;
        lxb_dom_node_t *child = node->first_child;
        while (child) {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                has_element_children = 1;
                break;
            }
            child = child->next;
        }
        
        // Only skip text extraction if element has ELEMENT children
        if (has_element_children) {
            const char *container_tags[] = {
                "div", "p", "h1", "h2", "h3", "h4", "h5", "h6",
                "ul", "ol", "li", "table", "form", "section",
                "article", "aside", "nav", "header", "footer",
                "main", "figure", "blockquote", "pre",
                "a", "span", "strong", "em", "i", "b",
                NULL
            };
            
            for (int i = 0; container_tags[i] != NULL; i++) {
                if (strcasecmp(tag, container_tags[i]) == 0) {
                    free(tag);
                    return strdup(""); // Has element children, text will be in child nodes
                }
            }
        }
        free(tag);
    }
    
    // Get direct text content (for elements without element children)
    size_t text_len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(node, &text_len);
    
    if (!text || text_len == 0) {
        if (text) {
            lxb_dom_document_t *owner_doc = node->owner_document;
            if (owner_doc) {
                lxb_dom_document_destroy_text(owner_doc, text);
            }
        }
        return strdup("");
    }
    
    char *result = malloc(text_len + 1);
    if (!result) {
        lxb_dom_document_t *owner_doc = node->owner_document;
        if (owner_doc) {
            lxb_dom_document_destroy_text(owner_doc, text);
        }
        return strdup("");
    }
    
    memcpy(result, text, text_len);
    result[text_len] = '\0';
    
    lxb_dom_document_t *owner_doc = node->owner_document;
    if (owner_doc) {
        lxb_dom_document_destroy_text(owner_doc, text);
    }
    
    // ========== IMPROVED TEXT CLEANING ==========
    char *src = result;
    char *dst = result;
    int last_was_space = 0;
    int in_trim = 1; // Start in trim mode
    
    while (*src) {
        if (isspace((unsigned char)*src)) {
            if (!last_was_space && !in_trim) {
                *dst++ = ' '; // Replace multiple spaces/whitespace with single space
                last_was_space = 1;
            }
        } else {
            *dst++ = *src;
            last_was_space = 0;
            in_trim = 0; // We've found non-space content
        }
        src++;
    }
    
    // Null terminate
    if (dst > result && last_was_space) {
        dst--; // Remove trailing space
    }
    *dst = '\0';
    
    // Trim from start
    char *start = result;
    while (*start && isspace((unsigned char)*start)) start++;
    
    if (start != result) {
        memmove(result, start, strlen(start) + 1);
    }
    // ========== END TEXT CLEANING ==========
    
    return result;
}

// Parse inline styles simply
cJSON* parse_inline_styles_simple(lxb_dom_attr_t *style_attr) {
    if (!style_attr) return NULL;
    
    size_t style_len;
    const lxb_char_t *style_value = lxb_dom_attr_value(style_attr, &style_len);
    
    if (!style_value || style_len == 0) return NULL;
    
    char *style_str = malloc(style_len + 1);
    if (!style_str) return NULL;
    
    memcpy(style_str, style_value, style_len);
    style_str[style_len] = '\0';
    
    cJSON *styles = cJSON_CreateObject();
    char *copy = strdup(style_str);
    free(style_str);
    
    if (!copy) {
        cJSON_Delete(styles);
        return NULL;
    }
    
    char *saveptr;
    char *declaration = strtok_r(copy, ";", &saveptr);
    
    while (declaration) {
        char *colon = strchr(declaration, ':');
        if (colon) {
            *colon = '\0';
            char *prop = declaration;
            char *value = colon + 1;
            
            // Trim
            while (*prop == ' ') prop++;
            char *prop_end = prop + strlen(prop) - 1;
            while (prop_end > prop && (*prop_end == ' ' || *prop_end == '\t')) {
                *prop_end = '\0';
                prop_end--;
            }
            
            while (*value == ' ') value++;
            char *val_end = value + strlen(value) - 1;
            while (val_end > value && (*val_end == ' ' || *val_end == '\t')) {
                *val_end = '\0';
                val_end--;
            }
            
            if (strlen(prop) > 0 && strlen(value) > 0) {
                // Only keep rendering-relevant styles
                const char *relevant_styles[] = {
                    "color", "background-color", "font-size", "font-weight",
                    "font-style", "text-decoration", "width", "height",
                    "display", "position", "margin", "padding", NULL
                };
                
                for (int i = 0; relevant_styles[i] != NULL; i++) {
                    if (strcasecmp(prop, relevant_styles[i]) == 0) {
                        cJSON_AddStringToObject(styles, prop, value);
                        break;
                    }
                }
            }
        }
        declaration = strtok_r(NULL, ";", &saveptr);
    }
    
    free(copy);
    return styles;
}


// Helper function
char* lexbor_to_cstr(const lxb_char_t *lb_str, size_t len) {
    if (!lb_str || len == 0) return strdup("");
    
    char *cstr = malloc(len + 1);
    if (!cstr) return NULL;
    
    memcpy(cstr, lb_str, len);
    cstr[len] = '\0';
    return cstr;
}

char* get_element_text(lxb_dom_element_t *elem) {
    if (!elem) {
        return strdup("");
    }
    
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    if (!node) {
        return strdup("");
    }
    
    // Check if this is an element that shouldn't have text
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
    if (tag_name && tag_len > 0) {
        char tag[256];
        size_t copy_len = tag_len < 255 ? tag_len : 255;
        memcpy(tag, tag_name, copy_len);
        tag[copy_len] = '\0';
        
        // Skip text extraction for form elements
        const char *no_text_tags[] = {
            "input", "textarea", "select", "button", "form",
            "fieldset", "legend", "option", "optgroup", 
            "datalist", "output", "progress", "meter",
            "iframe", "canvas", "audio", "video", "img",
            "br", "hr", "meta", "link", "style", "script",
            NULL
        };
        
        for (int i = 0; no_text_tags[i] != NULL; i++) {
            if (strcasecmp(tag, no_text_tags[i]) == 0) {
                return strdup("");
            }
        }
    }
    
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
    while (*start && isspace((unsigned char)*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    
    // If result is empty after trimming, return empty string
    if (strlen(start) == 0) {
        free(result);
        return strdup("");
    }
    
    // Move trimmed result to beginning
    if (start != result) {
        memmove(result, start, strlen(start) + 1);
    }
    
    // =========================================================================
    // 🧼 HIRURŠKI TEXT SANITIZER (KONAČNA POPRAVKA ZA ČISTE STRINGOVE I NAVODNIKE)
    // Ne briše elemente unapred, već samo čisti zaostali JS kod iz izvučenog teksta!
    // Ovo trajno oslobađa linkove i rešava problem sa slomljenim navodnicima.
    // =========================================================================
    if (result && strlen(result) > 0) {
        // Ako string sadrži prepoznatljive tragove Google-ovih inline skripti
        if (strstr(result, "(function()") != NULL || 
            strstr(result, "var id=") != NULL || 
            strstr(result, "document.getElementById") != NULL) {
            
            // Tražimo gde se skripta završava (nakon zatvaranja funkcije i poziva)
            char *script_end = strstr(result, "})();");
            if (script_end) {
                // Pomeramo preostali, regularni tekst (ako ga ima nakon skripte) na početak stringa
                char *pravi_tekst = script_end + 5;
                memmove(result, pravi_tekst, strlen(pravi_tekst) + 1);
            } else {
                // Ako je skripta prekinuta na pola usled mrežnog bafera, bezbedno praznimo string
                result[0] = '\0';
            }
        }
        
        // 🛡️ UKLANJANJE ČUDNIH KARAKTERA: Čisti slomljeni bajt 'Â' iz teksta
        char *clean_src = result;
        char *clean_dst = result;
        while (*clean_src) {
            // Ako naiđemo na izolovani slomljeni UTF-8 marker 'Â' (bajt 194 ili 0xC2)
            if ((unsigned char)*clean_src == 194 || (unsigned char)*clean_src == 0xC2) {
                clean_src++; // Preskačemo ga i čistimo string!
                continue;
            }
            *clean_dst++ = *clean_src++;
        }
        *clean_dst = '\0';
    }
    // =========================================================================
    
    return result;
}


char* get_element_text_recursive(lxb_dom_element_t *elem) {
    if (!elem) return strdup("");
    
    char *result = strdup("");
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elem));
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            // Direct access to text node data - NO LIMIT!
            lxb_dom_character_data_t *text_node = (lxb_dom_character_data_t*)child;
            if (text_node->data.data && text_node->data.length > 0) {
                char *text_str = malloc(text_node->data.length + 1);
                if (text_str) {
                    memcpy(text_str, text_node->data.data, text_node->data.length);
                    text_str[text_node->data.length] = '\0';
                    
                    char *new_result = malloc(strlen(result) + text_node->data.length + 1);
                    if (new_result) {
                        strcpy(new_result, result);
                        strcat(new_result, text_str);
                        free(result);
                        result = new_result;
                    }
                    free(text_str);
                }
            }
        }
        else if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            char *child_text = get_element_text_recursive(child_elem);
            if (child_text && strlen(child_text) > 0) {
                char *new_result = malloc(strlen(result) + strlen(child_text) + 1);
                if (new_result) {
                    strcpy(new_result, result);
                    strcat(new_result, child_text);
                    free(result);
                    result = new_result;
                }
            }
            free(child_text);
        }
        child = lxb_dom_node_next(child);
    }
    
    // Trim whitespace (your existing trimming code)
    if (result && strlen(result) > 0) {
        char *start = result;
        while (*start && isspace((unsigned char)*start)) start++;
        
        if (start != result) {
            memmove(result, start, strlen(start) + 1);
        }
        
        char *end = result + strlen(result) - 1;
        while (end > result && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }
    }
    
    // =========================================================================
    // 🧼 AGRESIVNI UKLANJAČ INLINE KODA (ŠAH-MAT ZA GEOMETRIJU TABELA)
    // Direktno seče programske ostatke u stringu, čisti Â i oslobađa čist tekst!
    // =========================================================================
    if (result && strlen(result) > 0) {
        // 1. Sečemo prekinute cikluse funkcija ako ih ima
        if (strstr(result, "(function()") != NULL || 
            strstr(result, "var id=") != NULL || 
            strstr(result, "document.getElementById") != NULL) {
            
            char *script_end = strstr(result, "})();");
            if (script_end) {
                char *pravi_tekst = script_end + 5;
                memmove(result, pravi_tekst, strlen(pravi_tekst) + 1);
            } else {
                result[0] = '\0'; // Sigurno praznimo đubre
            }
        }
        
        // 2. Hirurški rez na inline dodelama koje kvare Google tabelu
        char *inline_code = strstr(result, "b=new");
        if (inline_code) {
            *inline_code = '\0'; // Odsecamo sve od tog mesta!
        }
        
        char *inline_code2 = strstr(result, "?(n=");
        if (inline_code2) {
            *inline_code2 = '\0'; // Odsecamo i drugi zaostali deo!
        }

        // 3. Čišćenje slomljenog UTF-8 markera 'Â' (bajt 194 / 0xC2)
        char *clean_src = result;
        char *clean_dst = result;
        while (*clean_src) {
            if ((unsigned char)*clean_src == 194 || (unsigned char)*clean_src == 0xC2) {
                clean_src++; 
                continue;
            }
            *clean_dst++ = *clean_src++;
        }
        *clean_dst = '\0';
    }
    // =========================================================================


    return result;
}



cJSON* procesuiraj_elemente(lxb_dom_node_t *node, int depth, cJSON *parent_json) {
    if (!node || depth > 20) return NULL;

    // ========== 1. HANDLE TEXT NODES ==========
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_node_t *parent_node = node->parent;
        int parent_is_skip = 0;
        
        if (parent_node && parent_node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *parent_elem = lxb_dom_interface_element(parent_node);
            size_t parent_tag_len;
            const lxb_char_t *parent_tag_name = lxb_dom_element_qualified_name(parent_elem, &parent_tag_len);
            
            if (parent_tag_name && parent_tag_len > 0) {
                char parent_tag[64];
                size_t copy_len = parent_tag_len < 63 ? parent_tag_len : 63;
                memcpy(parent_tag, parent_tag_name, copy_len);
                parent_tag[copy_len] = '\0';

                const char *skip_tags[] = {
                    "style", "meta", "title",
                    "head", "html", "noscript", "template", NULL
                };
                
                for (int i = 0; skip_tags[i] != NULL; i++) {
                    if (strcasecmp(parent_tag, skip_tags[i]) == 0) {
                        parent_is_skip = 1;
                        break;
                    }
                }
            }
        }
        
        if (parent_is_skip) {
            return NULL;
        }
        
        lxb_dom_character_data_t *text_node = (lxb_dom_character_data_t*)node;
        if (!text_node->data.data || text_node->data.length == 0) {
            return NULL;
        }
        
        int has_non_space = 0;
        for (size_t i = 0; i < text_node->data.length; i++) {
            if (!isspace((unsigned char)text_node->data.data[i])) {
                has_non_space = 1;
                break;
            }
        }
        
        if (!has_non_space) return NULL;
        
        char *text_str = malloc(text_node->data.length + 1);
        if (!text_str) return NULL;
        
        memcpy(text_str, text_node->data.data, text_node->data.length);
        text_str[text_node->data.length] = '\0';
        
        // 🚀 EMOTICON REPLACER SHIELD: Menja sve višebajtne UTF-8 karaktere sa '*'
 // 🚀 SIGURNOSNI SHIELD: Čisti teške emotikone, čuva naša slova i ćirilicu
 unsigned char *src = (unsigned char *)text_str;
 unsigned char *dst = (unsigned char *)text_str;
 while (*src) {
     // 1. Klasika ASCII (0 - 127) -> Propuštaj odmah
     if (*src < 0x80) {
         *dst++ = *src++;
     }
     // 2. Dvobajtni karakteri (0xC2 - 0xDF) -> OVDE SU NAŠA SLOVA I ĆIRILICA!
     else if (*src >= 0xC2 && *src <= 0xDF) {
         // Proveri da li postoji prateći bajt da ne pukne memorija
         if (*(src + 1) >= 0x80 && *(src + 1) <= 0xBF) {
             *dst++ = *src++; // Kopiraj vodeći bajt
             *dst++ = *src++; // Kopiraj prateći bajt
         } else {
             src++; // Korumpiran UTF-8, preskoči
         }
     }
     // 3. Trobajtni karakteri (0xE0 - 0xEF) -> Azijski simboli, specijalni matematički znaci
     else if (*src >= 0xE0 && *src <= 0xEF) {
         // Ovde možeš birati: propustiti ili zameniti sa '*'. 
         // Pošto tvoj font verovatno nema ove glifove, menjamo ih sa '*' radi stabilnosti renderera.
         *dst++ = '*';
         src++;
         while (*src >= 0x80 && *src <= 0xBF) {
             src++; // Preskačemo preostale prateće bajtove simbola
         }
     }
     // 4. Četvorobajtni karakteri (0xF0 - 0xF4) -> REALNI EMOTIKONI (Smajliji, životinje, zastave)
     else if (*src >= 0xF0 && *src <= 0xF4) {
         *dst++ = '*'; // Bezbedna zamena za stb_truetype!
         src++;
         while (*src >= 0x80 && *src <= 0xBF) {
             src++; // Preskačemo prateće bajtove emotikona
         }
     }
     // 5. Sve ostalo van standarda -> Preskoči
     else {
         src++;
     }
 }
 *dst = '\0';
        *dst = '\0';
        
        // Trim whitespace sa UTF-8 zaštitom
        char *start = text_str;
        while (*start && (unsigned char)*start < 128 && isspace((unsigned char)*start)) {
            start++;
        }
        
        char *end = start + strlen(start) - 1;
        while (end > start && (unsigned char)*end < 128 && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }
        
        if (strlen(start) > 0) {
            cJSON *text_json = cJSON_CreateObject();
            
            set_json_string(text_json, "tag", "text");
            set_json_string(text_json, "type", "text");
            set_json_string(text_json, "display", "inline");
            set_json_string(text_json, "content", start);
            set_json_string(text_json, "text", start);
            
            if (parent_json) {
                int p_font_size = get_json_number(parent_json, "font_size", DEFAULT_FONT_SIZE);
                set_json_number(text_json, "font_size", p_font_size);
                
                const char *p_font_weight = get_json_string(parent_json, "font_weight", "normal");
                set_json_string(text_json, "font_weight", p_font_weight);
                
                const char *p_color = get_json_string(parent_json, "color", "#000000");
                set_json_string(text_json, "color", p_color);
            } else {
                set_json_number(text_json, "font_size", DEFAULT_FONT_SIZE);
                set_json_string(text_json, "font_weight", "normal");
                set_json_string(text_json, "color", "#000000");
            }
            
            set_json_number(text_json, "x", DEFAULT_X);
            set_json_number(text_json, "y", DEFAULT_Y);
            
            set_json_bool(text_json, "is_text", 1);
            set_json_bool(text_json, "is_inline", 1);
            set_json_bool(text_json, "has_layout", 0);
            set_json_bool(text_json, "needs_layout", 1);
            set_json_number(text_json, "layout_calculated", 0);
            set_json_number(text_json, "width", 0);
            set_json_number(text_json, "height", 0);
            
            int text_id = g_next_element_id++;
            set_json_number(text_json, "id", text_id);
            set_json_number(text_json, "element_id", text_id);
            
            int parent_id = parent_json ? get_json_number(parent_json, "element_id", -1) : -1;
            set_json_number(text_json, "parent_id", parent_id);
            
            free(text_str);
            return text_json;
        }
        
        free(text_str);
        return NULL;
    }

    // ========== 2. HANDLE ELEMENT NODES ==========

    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return NULL;

    lxb_dom_element_t *elem = lxb_dom_interface_element(node);

    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
    if (!tag_name || tag_len == 0) return NULL;

    char *tag = malloc(tag_len + 1);
    memcpy(tag, tag_name, tag_len);
    tag[tag_len] = '\0';

if (strcasecmp(tag, "script") == 0) {
        // Don't add to JSON, don't render anything
        // Script will be executed by js_execute_script_elements
        free(tag);
        return NULL;  // Return NULL so nothing gets added to rendering_output
    }

 // ========== HANDLE <link> TAG (external CSS) ==========
if (strcasecmp(tag, "link") == 0) {
    // Get rel attribute

    lxb_dom_attr_t *rel_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"rel", 3);
    if (rel_attr) {
        size_t rel_len;
    
        const lxb_char_t *rel_value = lxb_dom_attr_value(rel_attr, &rel_len);
        if (rel_value && rel_len > 0 && strncasecmp((const char*)rel_value, "stylesheet", rel_len) == 0) {
            // Get href attribute
            lxb_dom_attr_t *href_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"href", 4);
            if (href_attr) {
                size_t href_len;
                const lxb_char_t *href_value = lxb_dom_attr_value(href_attr, &href_len);
                if (href_value && href_len > 0) {
                    char *href_str = malloc(href_len + 1);
                    memcpy(href_str, href_value, href_len);
                    href_str[href_len] = '\0';
                    
                    printf("🎨 External CSS detected: %s\n", href_str);

   // ===== USE CENTRALIZED FILTER =====
   if (is_resource_blocked(href_str)) {
    printf("⏭️ Skipping blocked resource (%s): %s\n", 
           get_block_reason(href_str), href_str);
    free(href_str);
    free(tag);
    return NULL;
}
                    
      // ========== RESOLVE RELATIVE URL ==========
                    char *absolute_url = NULL;
                    
                    // Check if it's already absolute (starts with http://, https://, or file://)
                    if (strncmp(href_str, "http://", 7) == 0 || 
                        strncmp(href_str, "https://", 8) == 0 ||
                        strncmp(href_str, "file://", 7) == 0) {
                        absolute_url = strdup(href_str);
                    } else {
                        // Need to resolve relative to current page URL
                        // You need to pass the current page URL to this function
                        // For now, assume file:// protocol
                        const char *base_url = get_current_base_url();
                   //     printf("🔍 g_current_base_url = '%s'\n", base_url ? base_url : "(NULL)");
                        if (base_url) {
                            absolute_url = resolve_relative_url(g_current_base_url, href_str);
                          //  printf("   Resolved U: %s\n", absolute_url);
                        } else {
                            // Fallback for when no base URL is set
                            absolute_url = malloc(512);
                            snprintf(absolute_url, 512, "file:///%s", href_str);
                           // printf("   No base URL, using: %s\n", absolute_url);
                        }
                    }
                    
                    // Fetch the CSS
                    char *css_content = NULL;
                    size_t css_size = 0;

                    
                    if (absolute_url && fetch_css_resource(absolute_url, &css_content, &css_size) == EOK) {
                        parse_css_and_add_rules(css_content);
                        free(css_content);
                    } else {
                        printf("⚠️ Failed to fetch CSS from %s\n", absolute_url ? absolute_url : href_str);
                    }
                    
                    if (absolute_url) free(absolute_url);
                    free(href_str);
                }
            }
        }
    }
    
    // Link tags don't render anything, so return NULL
    free(tag);
    return NULL;
}

// ========== PUT STYLE HANDLER HERE ==========
if (strcasecmp(tag, "style") == 0) {
    printf("🎨 Found <style> tag, extracting CSS...\n");
    
    // Extract CSS from text nodes inside style tag
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_character_data_t *text_node = (lxb_dom_character_data_t*)child;
            char *css_content = malloc(text_node->data.length + 1);
            memcpy(css_content, text_node->data.data, text_node->data.length);
            css_content[text_node->data.length] = '\0';
            
            parse_css_and_add_rules(css_content);
            free(css_content);
        }
        child = lxb_dom_node_next(child);
    }
    
    free(tag);
    return NULL;
}

// ===== SKIP MODAL DIALOGS (ADD THIS BLOCK HERE) =====
if (strcasecmp(tag, "div") == 0) {
    // Check for modal ID
    lxb_dom_attr_t *id_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"id", 2);
    if (id_attr) {
        size_t id_len;
        const lxb_char_t *id_value = lxb_dom_attr_value(id_attr, &id_len);
        if (id_value && id_len > 0) {
            char id_str[64];
            size_t copy_len = id_len < 63 ? id_len : 63;
            memcpy(id_str, id_value, copy_len);
            id_str[copy_len] = '\0';
            
            if (strstr(id_str, "modal") != NULL) {
                printf("⏭️ Skipping modal dialog: id=%s\n", id_str);
                free(tag);
                return NULL;
            }
        }
    }
    
    // Check for modal class
    lxb_dom_attr_t *class_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"class", 5);
    if (class_attr) {
        size_t class_len;
        const lxb_char_t *class_value = lxb_dom_attr_value(class_attr, &class_len);
        if (class_value && class_len > 0) {
            char class_str[128];
            size_t copy_len = class_len < 127 ? class_len : 127;
            memcpy(class_str, class_value, copy_len);
            class_str[copy_len] = '\0';
            
            if (strstr(class_str, "modal") != NULL || strstr(class_str, "fade") != NULL) {
                printf("⏭️ Skipping modal dialog: class=%s\n", class_str);
                free(tag);
                return NULL;
            }
        }
    }
}
// =================================================


    // Skip non-rendering elements
    const char *skip_tags[] = {
        "meta", "link", "title",
        "head", "html", "!doctype", "noscript", "template",
        "colgroup", "col",
        "path",        // SVG paths (I don't render SVG)
        "svg",         // SVG containers
        "defs",        // SVG definitions
        "symbol",      // SVG symbols
        "use",         // SVG use references
        "iframe",      // Iframes (V2 feature)
        "canvas",      // Canvas (V2 feature)
        "style",       // Already handled separately
        NULL
    };
    for (int i = 0; skip_tags[i] != NULL; i++) {
        if (strcasecmp(tag, skip_tags[i]) == 0) {
            free(tag);
            return NULL;
        }
    }

    // Create element JSON
    cJSON *elem_json = cJSON_CreateObject();
    init_json_with_all_defaults(elem_json, tag);
    set_element_specific_defaults(elem_json, tag);

    int element_id = g_next_element_id++;
    set_json_number(elem_json, "element_id", element_id);
    set_json_number(elem_json, "id", element_id);

    // Set display based on tag type
    if (strcasecmp(tag, "img") == 0 || strcasecmp(tag, "image") == 0) {
        // Keep as is"
    }
    else if (strcasecmp(tag, "div") == 0 || strcasecmp(tag, "p") == 0 ||
             strcasecmp(tag, "h1") == 0 || strcasecmp(tag, "h2") == 0 ||
             strcasecmp(tag, "h3") == 0 || strcasecmp(tag, "h4") == 0 ||
             strcasecmp(tag, "h5") == 0 || strcasecmp(tag, "h6") == 0 ||
             strcasecmp(tag, "ul") == 0 || strcasecmp(tag, "ol") == 0 ||
             strcasecmp(tag, "li") == 0 || strcasecmp(tag, "table") == 0 ||
             strcasecmp(tag, "form") == 0 || strcasecmp(tag, "section") == 0 ||
             strcasecmp(tag, "article") == 0 || strcasecmp(tag, "header") == 0 ||
             strcasecmp(tag, "footer") == 0 || strcasecmp(tag, "nav") == 0 ||
             strcasecmp(tag, "aside") == 0 || strcasecmp(tag, "main") == 0 ||
             strcasecmp(tag, "figure") == 0 || strcasecmp(tag, "blockquote") == 0 ||
             strcasecmp(tag, "pre") == 0 || strcasecmp(tag, "address") == 0 ||
             strcasecmp(tag, "hr") == 0) {
        set_json_string(elem_json, "display", "block");
    } else if (is_inline(tag)) {
        set_json_string(elem_json, "display", "inline");
        set_json_string(elem_json, "type", "inline");
        set_json_bool(elem_json, "is_inline", 1);
    } else {
        set_json_string(elem_json, "display", "block");
    }

    // Get ID
    const lxb_char_t *id = lxb_dom_element_id(elem, &tag_len);
    if (id && tag_len > 0) {
        char *id_str = malloc(tag_len + 1);
        memcpy(id_str, id, tag_len);
        id_str[tag_len] = '\0';
        set_json_string(elem_json, "id", id_str);
        free(id_str);
    }

    // Get classes
    const lxb_char_t *cls = lxb_dom_element_class(elem, &tag_len);
    if (cls && tag_len > 0) {
        char *cls_str = malloc(tag_len + 1);
        memcpy(cls_str, cls, tag_len);
        cls_str[tag_len] = '\0';
        set_json_string(elem_json, "class_string", cls_str);

        cJSON *classes_array = cJSON_CreateArray();
        char *saveptr;
        char *token = strtok_r(cls_str, " \t\n\r", &saveptr);
        while (token) {
            if (strlen(token) > 0) {
                cJSON_AddItemToArray(classes_array, cJSON_CreateString(token));
            }
            token = strtok_r(NULL, " \t\n\r", &saveptr);
        }
        cJSON_ReplaceItemInObject(elem_json, "classes", classes_array);
        free(cls_str);
    } else {
        set_json_string(elem_json, "class_string", "");
        cJSON_ReplaceItemInObject(elem_json, "classes", cJSON_CreateArray());
    }

// Parse onclick attribute for all elements
lxb_dom_attr_t *onclick_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"onclick", 7);
if (onclick_attr) {
    size_t onclick_len;
    const lxb_char_t *onclick_value = lxb_dom_attr_value(onclick_attr, &onclick_len);
    if (onclick_value && onclick_len > 0) {
        char *onclick_str = malloc(onclick_len + 1);
        memcpy(onclick_str, onclick_value, onclick_len);
        onclick_str[onclick_len] = '\0';
        set_json_string(elem_json, "onclick", onclick_str);
        set_json_bool(elem_json, "has_onclick", 1);
        free(onclick_str);
    }
}

      // ========== ADD THIS: PARSE INLINE STYLES (SAFER VERSION) ==========
    // Parse style attribute manually to avoid crash
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"style", 5);
    if (style_attr) {        
        size_t style_len;
        const lxb_char_t *style_value = lxb_dom_attr_value(style_attr, &style_len);
        if (style_value && style_len > 0) {
            char *style_str = malloc(style_len + 1);
            if (style_str) {
                memcpy(style_str, style_value, style_len);
                style_str[style_len] = '\0';
                
                // Parse manually (simple semicolon-separated)
                char *saveptr;
                char *declaration = strtok_r(style_str, ";", &saveptr);
                while (declaration) {
                    // Trim whitespace
                    while (*declaration == ' ' || *declaration == '\t') declaration++;
                    
                    char *colon = strchr(declaration, ':');
                    if (colon) {
                        *colon = '\0';
                        char *prop = declaration;
                        char *value = colon + 1;
                        
                        // Trim property
                        while (*prop == ' ' || *prop == '\t') prop++;
                        char *prop_end = prop + strlen(prop) - 1;
                        while (prop_end > prop && (*prop_end == ' ' || *prop_end == '\t')) {
                            *prop_end-- = '\0';
                        }
                        
                        // Trim value
                        while (*value == ' ' || *value == '\t') value++;
                        char *val_end = value + strlen(value) - 1;
                        while (val_end > value && (*val_end == ' ' || *val_end == '\t')) {
                            *val_end-- = '\0';
                        }
                        
                
// Apply properties
if (strcmp(prop, "padding") == 0) {
    // Handle multi-value padding
    char *copy_val = strdup(value);
    if (copy_val) {
        char *parts[4] = {NULL, NULL, NULL, NULL};
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
        } else {
            top = parse_css_length(parts[0], 0);
            right = parse_css_length(parts[1], 0);
            bottom = parse_css_length(parts[2], 0);
            left = parse_css_length(parts[3], 0);
        }
        
        free(copy_val);
        
        set_json_number(elem_json, "padding_top", top);
        set_json_number(elem_json, "padding_right", right);
        set_json_number(elem_json, "padding_bottom", bottom);
        set_json_number(elem_json, "padding_left", left);
    }
}
else if (strcmp(prop, "margin") == 0) {
    // Handle multi-value margin the same way
    char *copy_val = strdup(value);
    if (copy_val) {
        char *parts[4] = {NULL, NULL, NULL, NULL};  // ← INITIALIZED!
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
        } else {
            top = parse_css_length(parts[0], 0);
            right = parse_css_length(parts[1], 0);
            bottom = parse_css_length(parts[2], 0);
            left = (part_count > 3) ? parse_css_length(parts[3], 0) : 0;
        }
        
        free(copy_val);
        
        set_json_number(elem_json, "margin_top", top);
        set_json_number(elem_json, "margin_right", right);
        set_json_number(elem_json, "margin_bottom", bottom);
        set_json_number(elem_json, "margin_left", left);
    }
}
                        else if (strcmp(prop, "background-color") == 0) {
                            set_json_string(elem_json, "bg_color", value);
                        }
                        else if (strcmp(prop, "text-align") == 0) {
                            set_json_string(elem_json, "text_align", value);
                        }
                        else if (strcmp(prop, "color") == 0) {
                            set_json_string(elem_json, "color", value);
                        }
                        else if (strcmp(prop, "text-decoration") == 0) {
                            set_json_string(elem_json, "text_decoration", value);
                        }
                    }
                    declaration = strtok_r(NULL, ";", &saveptr);
                }
                free(style_str);
            }
        }
    }
    // ========== END INLINE STYLES PARSING ==========
    
    // ========== FORME =====================
 if (strcasecmp(tag, "form") == 0 ||
 strcasecmp(tag, "input") == 0 ||
 strcasecmp(tag, "datalist") == 0 ||
 strcasecmp(tag, "output") == 0) {
process_form_element(elem_json, elem, tag);
}

    // ========== BDO ATTRIBUTE ==========
    if (strcasecmp(tag, "bdo") == 0) {
        lxb_dom_attr_t *dir_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"dir", 3);
        if (dir_attr) {
            size_t dir_len;
            const lxb_char_t *dir_value = lxb_dom_attr_value(dir_attr, &dir_len);
            if (dir_value && dir_len > 0) {
                char *dir_str = malloc(dir_len + 1);
                memcpy(dir_str, dir_value, dir_len);
                dir_str[dir_len] = '\0';
                set_json_string(elem_json, "dir", dir_str);
                free(dir_str);
            }
        }
    }

    // ========== DATA ELEMENT ==========
    if (strcasecmp(tag, "data") == 0) {
        lxb_dom_attr_t *value_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"value", 5);
        if (value_attr) {
            size_t value_len;
            const lxb_char_t *value = lxb_dom_attr_value(value_attr, &value_len);
            if (value && value_len > 0) {
                char *value_str = malloc(value_len + 1);
                memcpy(value_str, value, value_len);
                value_str[value_len] = '\0';
                set_json_string(elem_json, "data_value", value_str);
                free(value_str);
            }
        }
    }

   // ========== HANDLE TABLE ELEMENTS ==========
if (strcasecmp(tag, "table") == 0) {
    // ===== KORISTI NOVI TABLES PARSER =====
    cJSON *table_data = parse_table_complete(elem);
    if (table_data) {
        cJSON_AddItemToObject(elem_json, "table_data", table_data);
        
        // Preuzmi atribute iz table_data
        int border = get_json_number(table_data, "border", 1);
        int cellspacing = get_json_number(table_data, "cellspacing", 2);
        int cellpadding = get_json_number(table_data, "cellpadding", 1);
        
        set_json_number(elem_json, "border", border);
        set_json_number(elem_json, "cellspacing", cellspacing);
        set_json_number(elem_json, "cellpadding", cellpadding);
        
        // Dodaj matricu (za layout engine)
        cJSON *matrix = cJSON_GetObjectItem(table_data, "matrix");
        if (matrix) {
            cJSON_AddItemToObject(elem_json, "table_matrix", cJSON_Duplicate(matrix, 1));
        }
        
        // Postavi flagove
        set_json_bool(elem_json, "is_table", 1);
        set_json_number(elem_json, "row_count", get_json_number(table_data, "row_count", 0));
        set_json_number(elem_json, "col_count", get_json_number(table_data, "col_count", 0));
        set_json_bool(elem_json, "use_direct_recursion", 0);
        // ===== KREIRAJ DECU OD ĆELIJA (za renderovanje) =====
        cJSON *children_array = cJSON_CreateArray();
        cJSON *rows = cJSON_GetObjectItem(table_data, "rows");
        
        if (rows && cJSON_IsArray(rows)) {
            int row_count = cJSON_GetArraySize(rows);
            for (int r = 0; r < row_count; r++) {
                cJSON *row = cJSON_GetArrayItem(rows, r);
                cJSON *cells = cJSON_GetObjectItem(row, "cells");
                
                if (cells && cJSON_IsArray(cells)) {
                    int cell_count = cJSON_GetArraySize(cells);
                    for (int c = 0; c < cell_count; c++) {
                        cJSON *cell = cJSON_GetArrayItem(cells, c);
                        
                        // Kreiraj ravan element za svaku ćeliju
                        cJSON *flat_child = cJSON_CreateObject();
                        
                        // Prenesi bitne podatke
                        const char *cell_id = get_json_string(cell, "id", "");
                        if (cell_id && strlen(cell_id) > 0) {
                            set_json_string(flat_child, "id", cell_id);
                        }
                        
                        // Ako ćelija ima direktan tekst, dodaj ga
                        cJSON *text_item = cJSON_GetObjectItem(cell, "text");
                        if (text_item && cJSON_IsString(text_item)) {
                            set_json_string(flat_child, "tag", "text");
                            set_json_string(flat_child, "type", "text");
                            set_json_string(flat_child, "content", text_item->valuestring);
                            set_json_string(flat_child, "text", text_item->valuestring);
                        } else {
                            // Inače, označi kao ćeliju
                            set_json_string(flat_child, "tag", "td");
                            set_json_string(flat_child, "type", "table-cell");
                        }
                        
                        // Prenesi poziciju i dimenzije (iz matrice)
                        int matrix_row = get_json_number(cell, "matrix_row", r);
                        int matrix_col = get_json_number(cell, "matrix_col", c);
                        int colspan = get_json_number(cell, "colspan", 1);
                        int rowspan = get_json_number(cell, "rowspan", 1);
                        
                        set_json_number(flat_child, "matrix_row", matrix_row);
                        set_json_number(flat_child, "matrix_col", matrix_col);
                        set_json_number(flat_child, "colspan", colspan);
                        set_json_number(flat_child, "rowspan", rowspan);
                        set_json_number(flat_child, "row_index", r);
                        set_json_number(flat_child, "cell_index", c);
                        
                        // Dimenzije (biće ažurirane u layout-u)
                        set_json_number(flat_child, "width", get_json_number(cell, "width", 100));
                        set_json_number(flat_child, "height", get_json_number(cell, "height", 30));
                        set_json_string(flat_child, "display", "inline-block");
                        
                        // Dodaj decu ćelije (ako ih ima)
                        cJSON *cell_children = cJSON_GetObjectItem(cell, "children");
                        if (cell_children && cJSON_IsArray(cell_children) && cJSON_GetArraySize(cell_children) > 0) {
                            cJSON *child_array = cJSON_CreateArray();
                            for (int k = 0; k < cJSON_GetArraySize(cell_children); k++) {
                                cJSON *child = cJSON_GetArrayItem(cell_children, k);
                                cJSON_AddItemToArray(child_array, cJSON_Duplicate(child, 1));
                            }
                            cJSON_AddItemToObject(flat_child, "children", child_array);
                        }
                        
                        cJSON_AddItemToArray(children_array, flat_child);
                    }
                }
            }
        }
        
        if (cJSON_GetArraySize(children_array) > 0) {
            cJSON_AddItemToObject(elem_json, "children", children_array);
        } else {
            cJSON_Delete(children_array);
        }
        
        // Postavi display na block (tabele su blok elementi)
        set_json_string(elem_json, "display", "block");
        
        return elem_json;
    } else {
        // Fallback: ako novi parser ne radi, napravi praznu tabelu
        set_json_bool(elem_json, "is_table", 1);
        set_json_string(elem_json, "display", "block");
        return elem_json;
    }
}


// ========== HANDLE BUTTON ELEMENTS ==========
if (strcasecmp(tag, "button") == 0) {
    // Process like a link/inline element
    cJSON *button_json = elem_json;
    init_button_defaults(button_json);
    process_button_attributes(button_json, elem);
    
    // Set display to inline-block (like links)
    set_json_string(button_json, "display", "inline-block");
    set_json_string(button_json, "type", "inline");
    set_json_bool(button_json, "is_inline", 1);
    set_json_bool(button_json, "is_clickable", 1);
    
    // Don't set is_menu or menu_orientation
}
    // ========== TIME DATETIME ==========
    if (strcasecmp(tag, "time") == 0) {
        lxb_dom_attr_t *dt_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"datetime", 8);
        if (dt_attr) {
            size_t dt_len;
            const lxb_char_t *dt_value = lxb_dom_attr_value(dt_attr, &dt_len);
            if (dt_value && dt_len > 0) {
                char *dt_str = malloc(dt_len + 1);
                memcpy(dt_str, dt_value, dt_len);
                dt_str[dt_len] = '\0';
                set_json_string(elem_json, "datetime", dt_str);
                free(dt_str);
            }
        }
    }

// ========== IMAGE ATTRIBUTES ==========
// ========== IMAGE ATTRIBUTES ==========
if (strcasecmp(tag, "img") == 0 || strcasecmp(tag, "image") == 0) {
    lxb_dom_attr_t *src_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"src", 3);
    if (src_attr) {
        size_t src_len;
        const lxb_char_t *src = lxb_dom_attr_value(src_attr, &src_len);
        if (src && src_len > 0) {
            char *src_str = malloc(src_len + 1);
            if (src_str) {
                memcpy(src_str, src, src_len);
                src_str[src_len] = '\0';
                
                // ===== RESOLVE TO ABSOLUTE URL =====
                char *absolute_src = NULL;
                int is_remote = (g_current_base_url && 
                                (strncmp(g_current_base_url, "http://", 7) == 0 ||
                                 strncmp(g_current_base_url, "https://", 8) == 0));
                
                if (is_remote && strstr(src_str, "://") == NULL) {
                    absolute_src = resolve_url(g_current_base_url, src_str);
                } else {
                    absolute_src = strdup(src_str);
                }
                
                // ===== STORE BASE CONTENT =====
                if (absolute_src) {
                    set_json_string(elem_json, "src", absolute_src);
                    
                    // Ako slika na vrhu ima pun protokol (kao na djurkovicdent.me), guramo je ODMAH u red
                    if (strncmp(absolute_src, "http://", 7) == 0 || strncmp(absolute_src, "https://", 8) == 0) {
                        set_json_bool(elem_json, "needs_download", 1);
                        set_json_string(elem_json, "original_url", absolute_src);
                        queue_media_download(absolute_src, elem_json);
                        printf("🖼️ [Queue Tracker] Standardna slika ubačena u red: %s\n", absolute_src);
                    } else {
                        set_json_bool(elem_json, "needs_download", 0);
                    }
                    free(absolute_src); // Bezbedno oslobađamo lokalnu kopiju nakon upisa
                }
                free(src_str);
            }
        }
    }

    // --- Svi vaši originalni atributi ostaju 100% netaknuti i bezbedni ---
    lxb_dom_attr_t *alt_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"alt", 3);
    if (alt_attr) {
        size_t alt_len; const lxb_char_t *alt = lxb_dom_attr_value(alt_attr, &alt_len);
        if (alt && alt_len > 0) {
            char *alt_str = malloc(alt_len + 1);
            if (alt_str) {
                memcpy(alt_str, alt, alt_len); alt_str[alt_len] = '\0';
                set_json_string(elem_json, "alt", alt_str); free(alt_str);
            }
        }
    }

    lxb_dom_attr_t *width_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"width", 5);
    if (width_attr) {
        size_t width_len; const lxb_char_t *width = lxb_dom_attr_value(width_attr, &width_len);
        if (width && width_len > 0) {
            char *width_str = malloc(width_len + 1);
            if (width_str) {
                memcpy(width_str, width, width_len); width_str[width_len] = '\0';
                int width_px = parse_css_length(width_str, 400); set_json_number(elem_json, "attr_width", width_px); free(width_str);
            }
        }
    }

    lxb_dom_attr_t *height_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"height", 6);
    if (height_attr) {
        size_t height_len; const lxb_char_t *height = lxb_dom_attr_value(height_attr, &height_len);
        if (height && height_len > 0) {
            char *height_str = malloc(height_len + 1);
            if (height_str) {
                memcpy(height_str, height, height_len); height_str[height_len] = '\0';
                int height_px = parse_css_length(height_str, 300); set_json_number(elem_json, "attr_height", height_px); free(height_str);
            }
        }
    }

    lxb_dom_attr_t *srcset_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"srcset", 6);
    if (srcset_attr) {
        size_t srcset_len; const lxb_char_t *srcset_value = lxb_dom_attr_value(srcset_attr, &srcset_len);
        if (srcset_value && srcset_len > 0) {
            char *srcset_str = malloc(srcset_len + 1);
            if (srcset_str) {
                memcpy(srcset_str, srcset_value, srcset_len); srcset_str[srcset_len] = '\0';
                int is_remote = (g_current_base_url && (strncmp(g_current_base_url, "http://", 7) == 0 || strncmp(g_current_base_url, "https://", 8) == 0));
                if (is_remote) {
                    char *resolved_srcset = resolve_urls_in_srcset(g_current_base_url, srcset_str);
                    if (resolved_srcset) {
                        set_json_string(elem_json, "srcset", resolved_srcset);
                        set_json_string(elem_json, "original_srcset", srcset_str);
                        set_json_bool(elem_json, "srcset_needs_download", 1);
                        free(resolved_srcset);
                    } else { set_json_string(elem_json, "srcset", srcset_str); }
                } else { set_json_string(elem_json, "srcset", srcset_str); }
                free(srcset_str);
            }
        }
    }
    
    lxb_dom_attr_t *sizes_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"sizes", 5);
    if (sizes_attr) {
        size_t sizes_len; const lxb_char_t *sizes_value = lxb_dom_attr_value(sizes_attr, &sizes_len);
        if (sizes_value && sizes_len > 0) {
            char *sizes_str = malloc(sizes_len + 1);
            if (sizes_str) {
                memcpy(sizes_str, sizes_value, sizes_len); sizes_str[sizes_len] = '\0';
                set_json_string(elem_json, "sizes", sizes_str); free(sizes_str);
            }
        }
    }
    
    lxb_dom_attr_t *loading_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"loading", 7);
    if (loading_attr) {
        size_t loading_len; const lxb_char_t *loading_value = lxb_dom_attr_value(loading_attr, &loading_len);
        if (loading_value && loading_len > 0) {
            char *loading_str = malloc(loading_len + 1);
            if (loading_str) {
                memcpy(loading_str, loading_value, loading_len); loading_str[loading_len] = '\0';
                set_json_string(elem_json, "loading", loading_str); free(loading_str);
            }
        }
    }
    
    lxb_dom_attr_t *decoding_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"decoding", 8);
    if (decoding_attr) {
        size_t decoding_len; const lxb_char_t *decoding_value = lxb_dom_attr_value(decoding_attr, &decoding_len);
        if (decoding_value && decoding_len > 0) {
            char *decoding_str = malloc(decoding_len + 1);
            if (decoding_str) {
                memcpy(decoding_str, decoding_value, decoding_len); decoding_str[decoding_len] = '\0';
                set_json_string(elem_json, "decoding", decoding_str); free(decoding_str);
            }
        }
    }

    calculate_image_dimensions(elem_json);

    // =========================================================================
    // 🚀 UNIVERZALNI FILTER: DINAMIČKO DODAVANJE DOMENA ISPRED RELATIVNIH PUTANJA
    // Potpuno očišćen od hardkodovanih sajtova i bezbedan za sve domene!
    // =========================================================================
    const char *final_src = get_json_string(elem_json, "src", NULL);
    
    if (final_src && final_src[0] == '/' && strstr(final_src, "://") == NULL && strncmp(final_src, "data:", 5) != 0) {
        
        // 1. Dinamički određujemo najbolji mogući bazni URL bez hardkodovanja
        const char *base_domain = NULL;
        if (g_current_base_url && strncmp(g_current_base_url, "http", 4) == 0) {
            base_domain = g_current_base_url;
        } else if (global_pauk_ui && global_pauk_ui->current_address && strncmp(global_pauk_ui->current_address, "http", 4) == 0) {
            base_domain = global_pauk_ui->current_address;
        }

        // Ako imamo važeći mrežni kontekst, prelazimo na sklapanje
        if (base_domain) {
            // clean_root je ispravan niz karaktera na steku
            char clean_root[256];
            strncpy(clean_root, base_domain, sizeof(clean_root) - 1);
            clean_root[sizeof(clean_root) - 1] = '\0';
            
            // Hirurški sečemo sve nakon domena (ostaje samo koren, npr. https://djurkovicdent.me)
            char *proto_end = strstr(clean_root, "://");
            if (proto_end) {
                char *slash_ptr = strchr(proto_end + 3, '/');
                if (slash_ptr) {
                    *slash_ptr = '\0'; // Sečemo sve nakon domena context-a
                }
            }

            // full_absolute_url je ispravan niz karaktera na steku
            char full_absolute_url[1024];
            snprintf(full_absolute_url, sizeof(full_absolute_url), "%s%s", clean_root, final_src);

            // Ažuriramo JSON i šaljemo sliku u red SAMO OVDE, jednom, bezbedno i bez dupliranja!
            set_json_string(elem_json, "src", full_absolute_url);
            set_json_string(elem_json, "original_url", full_absolute_url);
            set_json_bool(elem_json, "needs_download", 1);

            queue_media_download(full_absolute_url, elem_json);
            printf("🖼️ [Universal Image Filter] Dinamički dodat domen ispred relativne putanje: %s\n", full_absolute_url);
        } else {
            // Ako pretraživač radi u offline/lokalnom režimu, slika ostaje lokalna
            set_json_bool(elem_json, "needs_download", 0);
        }
    }
}


// ========== HANDLE DETAILS/SUMMARY ==========

else if (strcasecmp(tag, "summary") == 0) {
    set_json_string(elem_json, "display", "block");
    set_json_bool(elem_json, "is_summary", 1);
    set_json_string(elem_json, "font_weight", "bold");
    set_json_string(elem_json, "cursor", "pointer");
    set_json_number(elem_json, "padding_left", 20);
}


if (strcasecmp(tag, "details") == 0) {
    set_json_string(elem_json, "display", "block");
    set_json_bool(elem_json, "is_details", 1);
    
    // Check for 'open' attribute
    lxb_dom_attr_t *open_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"open", 4);
    int is_open = (open_attr != NULL);
    set_json_bool(elem_json, "details_open", is_open);
    
    set_json_number(elem_json, "margin_top", 10);
    set_json_number(elem_json, "margin_bottom", 10);
    set_json_string(elem_json, "border_style", "solid");
    set_json_number(elem_json, "border_width", 1);
    set_json_string(elem_json, "border_color", "#CCCCCC");
    set_json_number(elem_json, "padding", 10);
}


    // ========== HANDLE NAV (navigation menus) ==========
    if (strcasecmp(tag, "nav") == 0) {
        const char *orientation = detect_menu_orientation(elem);
        set_json_string(elem_json, "menu_orientation", orientation);
        set_json_bool(elem_json, "is_menu", 1);
        
        // 🚀 ŠTIT: Ako je CSS već sakrio meni, ne dozvoli da ga default prebriše
        const char *existing_display = get_json_string(elem_json, "display", "");
        if (strcmp(existing_display, "none") == 0 || get_json_bool(elem_json, "is_visible", 1) == 0) {
            set_json_string(elem_json, "display", "none");
            set_json_bool(elem_json, "is_visible", 0);
        } else if (strcmp(orientation, "horizontal") == 0) {
            set_json_string(elem_json, "display", "flex");
            set_json_string(elem_json, "flex_direction", "row");
            set_json_string(elem_json, "flex_wrap", "nowrap");
            set_json_string(elem_json, "justify_content", "flex-start");
            set_json_string(elem_json, "align_items", "center");
        } else {
            set_json_string(elem_json, "display", "block");
        }
    }

    // ========== HANDLE UL ==========
    if (strcasecmp(tag, "ul") == 0) {
        const char *parent_orientation = NULL;
        int parent_id = get_json_number(elem_json, "parent_id", 0);
        cJSON *parent_elem = find_parent_element_by_id(global_computed_layout, parent_id);
        int parent_is_menu = get_json_bool(parent_elem, "is_menu", 0);
        
        // 🚀 ŠTIT: Ako je CSS već sakrio listu, zadrži display: none i is_visible: 0
        const char *existing_display = get_json_string(elem_json, "display", "");
        if (strcmp(existing_display, "none") == 0 || get_json_bool(elem_json, "is_visible", 1) == 0) {
            set_json_string(elem_json, "display", "none");
            set_json_bool(elem_json, "is_visible", 0);
        } else if (parent_is_menu) {
            int is_in_menu = get_json_bool(parent_json, "is_menu", 0);
            parent_orientation = get_json_string(parent_json, "menu_orientation", NULL);
            set_json_string(elem_json, "menu_orientation", parent_orientation);
            set_json_bool(elem_json, "is_menu", is_in_menu);
            
            if (strcmp(parent_orientation, "horizontal") == 0) {
                set_json_string(elem_json, "display", "flex");
                set_json_string(elem_json, "flex_direction", "row");
                set_json_string(elem_json, "flex_wrap", "nowrap");
                set_json_string(elem_json, "justify_content", "flex-start");
                set_json_string(elem_json, "align_items", "center");
                set_json_string(elem_json, "list_style_type", "none");
                set_json_number(elem_json, "padding_left", 0);
                set_json_number(elem_json, "margin_left", 0);
                set_json_number(elem_json, "margin_top", 0);
                set_json_number(elem_json, "margin_bottom", 0);
            }
        } else {
            set_json_bool(elem_json, "is_menu", 0);
            set_json_string(elem_json, "display", "block");
            
            if (!cJSON_HasObjectItem(elem_json, "list_style_type")) {
                set_json_string(elem_json, "list_style_type", "disc");
            }
            if (!cJSON_HasObjectItem(elem_json, "padding_left")) {
                set_json_number(elem_json, "padding_left", 30);
            }
        }
    }

    // ========== HANDLE LI ==========
    if (strcasecmp(tag, "li") == 0) {
        const char *parent_orientation = NULL;
        
        if (parent_json) {
            parent_orientation = get_json_string(parent_json, "menu_orientation", NULL);
            if (!parent_orientation) {
                parent_orientation = get_json_string(parent_json, "menu_orientation", NULL);
            }
        }
        
        int is_menu_item = (parent_orientation != NULL);
        
        // 🚀 ŠTIT: Ako je roditelj sakriven (display: none), i dete mora naslediti nevidljivost u startu
        const char *parent_display = parent_json ? get_json_string(parent_json, "display", "") : "";
        int parent_skriven = (strcmp(parent_display, "none") == 0);
        
        const char *existing_display = get_json_string(elem_json, "display", "");
        if (strcmp(existing_display, "none") == 0 || get_json_bool(elem_json, "is_visible", 1) == 0 || parent_skriven) {
            set_json_string(elem_json, "display", "none");
            set_json_bool(elem_json, "is_visible", 0);
        } else if (is_menu_item) {
            set_json_bool(elem_json, "is_menu_item", 1);
            
            if (parent_orientation) {
                set_json_string(elem_json, "menu_orientation", parent_orientation);
            } else {
                set_json_string(elem_json, "menu_orientation", "vertical");
            }
            
            const char *orientation = get_json_string(elem_json, "menu_orientation", "vertical");
            
            if (strcmp(orientation, "horizontal") == 0) {
                set_json_string(elem_json, "display", "inline-block");
                set_json_number(elem_json, "flex_grow", 0);
                set_json_number(elem_json, "flex_shrink", 1);
                cJSON_DeleteItemFromObject(elem_json, "bullet_char");
                cJSON_DeleteItemFromObject(elem_json, "bullet_x");
                cJSON_DeleteItemFromObject(elem_json, "bullet_y");
                set_json_number(elem_json, "margin_right", 10);
                set_json_number(elem_json, "margin_left", 0);
                set_json_number(elem_json, "margin_top", 2);
                set_json_number(elem_json, "margin_bottom", 2);
            } else {
                set_json_string(elem_json, "display", "block");
                cJSON_DeleteItemFromObject(elem_json, "bullet_char");
                cJSON_DeleteItemFromObject(elem_json, "bullet_x");
                cJSON_DeleteItemFromObject(elem_json, "bullet_y");
            }
            
            set_json_string(elem_json, "hover_bg", "#555555");
            set_json_string(elem_json, "hover_color", "#ffffff");
            
            if (!cJSON_HasObjectItem(elem_json, "padding_left")) {
                set_json_number(elem_json, "padding_left", 5);
            }
            if (!cJSON_HasObjectItem(elem_json, "padding_right")) {
                set_json_number(elem_json, "padding_right", 5);
            }
            if (!cJSON_HasObjectItem(elem_json, "padding_top")) {
                set_json_number(elem_json, "padding_top", 2);
            }
            if (!cJSON_HasObjectItem(elem_json, "padding_bottom")) {
                set_json_number(elem_json, "padding_bottom", 2);
            }
        } else {
            set_json_bool(elem_json, "is_menu_item", 0);
            set_json_string(elem_json, "display", "block");
            
            const char *list_style = "disc";
            if (parent_json) {
                list_style = get_json_string(parent_json, "list_style_type", "disc");
            }
            
            const char *bullet = "*";
            if (strcmp(list_style, "circle") == 0) bullet = "o";
            else if (strcmp(list_style, "square") == 0) bullet = "#";
            else if (strcmp(list_style, "none") == 0) bullet = "";
            
            set_json_string(elem_json, "bullet_char", bullet);
            set_json_number(elem_json, "bullet_x", 0);
            set_json_number(elem_json, "bullet_y", 0);
        }
    }


// ========== HANDLE LINK ATTRIBUTES ==========
if (strcasecmp(tag, "a") == 0) {
    lxb_dom_attr_t *href_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"href", 4);
    if (href_attr) {
        size_t href_len;
        const lxb_char_t *href = lxb_dom_attr_value(href_attr, &href_len);
        if (href && href_len > 0) {
            char *href_str = malloc(href_len + 1);
            memcpy(href_str, href, href_len);
            href_str[href_len] = '\0';
            set_json_string(elem_json, "href", href_str);
            free(href_str);
        }
    }
    set_json_bool(elem_json, "is_link", 1);
    set_json_bool(elem_json, "is_clickable", 1);
    
    // ===== COLLECT FULL TEXT FROM ALL CHILDREN =====
    char *full_text = get_element_text_recursive(elem);
    if (full_text && strlen(full_text) > 0) {
        // Store the full text directly in the link element
        set_json_string(elem_json, "text", full_text);
        set_json_string(elem_json, "content", full_text);
        free(full_text);
    }
}

    // ========== PROPAGATE MENU ORIENTATION FROM PARENT ==========
    if (parent_json) {
        const char *parent_orientation = get_json_string(parent_json, "menu_orientation", NULL);
        if (parent_orientation) {
            set_json_string(elem_json, "menu_orientation", parent_orientation);
        }
    }
// ========== HANDLE MENU (command lists, toolbars) ==========
else if (strcasecmp(tag, "menu") == 0) {
    set_json_string(elem_json, "display", "block");
    set_json_string(elem_json, "type", "block");
    set_json_bool(elem_json, "is_menu", 0);
    // Don't set is_nav, don't set menu_orientation
    // Don't set any background
}

// ========== HANDLE IFRAME ==========
if (strcasecmp(tag, "iframe") == 0) {
    set_json_string(elem_json, "display", "inline-block");
    set_json_bool(elem_json, "is_iframe", 1);
    
    // Parse src
    lxb_dom_attr_t *src_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"src", 3);
    if (src_attr) {
        size_t src_len;
        const lxb_char_t *src = lxb_dom_attr_value(src_attr, &src_len);
        if (src && src_len > 0) {
            char *src_str = malloc(src_len + 1);
            memcpy(src_str, src, src_len);
            src_str[src_len] = '\0';
            set_json_string(elem_json, "iframe_src", src_str);
            free(src_str);
        }
    }
    
    // Parse width
    lxb_dom_attr_t *width_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"width", 5);
    if (width_attr) {
        size_t width_len;
        const lxb_char_t *width = lxb_dom_attr_value(width_attr, &width_len);
        if (width && width_len > 0) {
            char *width_str = malloc(width_len + 1);
            memcpy(width_str, width, width_len);
            width_str[width_len] = '\0';
            int width_px = parse_css_length(width_str, 300);
            set_json_number(elem_json, "width", width_px);
            free(width_str);
        }
    } else {
        set_json_number(elem_json, "width", 300);
    }
    
    // Parse height
    lxb_dom_attr_t *height_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"height", 6);
    if (height_attr) {
        size_t height_len;
        const lxb_char_t *height = lxb_dom_attr_value(height_attr, &height_len);
        if (height && height_len > 0) {
            char *height_str = malloc(height_len + 1);
            memcpy(height_str, height, height_len);
            height_str[height_len] = '\0';
            int height_px = parse_css_length(height_str, 150);
            set_json_number(elem_json, "height", height_px);
            free(height_str);
        }
    } else {
        set_json_number(elem_json, "height", 150);
    }
}

// ========== HANDLE AUDIO ==========
if (strcasecmp(tag, "audio") == 0) {
    set_json_string(elem_json, "display", "inline-block");
    set_json_bool(elem_json, "is_audio", 1);
    
    // Parse width and height (if specified)
    lxb_dom_attr_t *width_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"width", 5);
    if (width_attr) {
        size_t width_len;
        const lxb_char_t *width = lxb_dom_attr_value(width_attr, &width_len);
        if (width && width_len > 0) {
            char *width_str = malloc(width_len + 1);
            memcpy(width_str, width, width_len);
            width_str[width_len] = '\0';
            int width_px = parse_css_length(width_str, 300);
            set_json_number(elem_json, "width", width_px);
            free(width_str);
        }
    }
}

// ========== HANDLE VIDEO ==========
if (strcasecmp(tag, "video") == 0) {
    set_json_string(elem_json, "display", "inline-block");
    set_json_bool(elem_json, "is_video", 1);
    
    // Parse width and height
    lxb_dom_attr_t *width_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"width", 5);
    if (width_attr) {
        size_t width_len;
        const lxb_char_t *width = lxb_dom_attr_value(width_attr, &width_len);
        if (width && width_len > 0) {
            char *width_str = malloc(width_len + 1);
            memcpy(width_str, width, width_len);
            width_str[width_len] = '\0';
            int width_px = parse_css_length(width_str, 320);
            set_json_number(elem_json, "width", width_px);
            free(width_str);
        }
    }
    
    lxb_dom_attr_t *height_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"height", 6);
    if (height_attr) {
        size_t height_len;
        const lxb_char_t *height = lxb_dom_attr_value(height_attr, &height_len);
        if (height && height_len > 0) {
            char *height_str = malloc(height_len + 1);
            memcpy(height_str, height, height_len);
            height_str[height_len] = '\0';
            int height_px = parse_css_length(height_str, 240);
            set_json_number(elem_json, "height", height_px);
            free(height_str);
        }
    }
}

// ========== HANDLE CANVAS ==========
if (strcasecmp(tag, "canvas") == 0) {
    set_json_string(elem_json, "display", "inline-block");
    set_json_bool(elem_json, "is_canvas", 1);
    
    // Parse width and height
    lxb_dom_attr_t *width_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"width", 5);
    if (width_attr) {
        size_t width_len;
        const lxb_char_t *width = lxb_dom_attr_value(width_attr, &width_len);
        if (width && width_len > 0) {
            char *width_str = malloc(width_len + 1);
            memcpy(width_str, width, width_len);
            width_str[width_len] = '\0';
            int width_px = parse_css_length(width_str, 200);
            set_json_number(elem_json, "width", width_px);
            free(width_str);
        }
    }
    
    lxb_dom_attr_t *height_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"height", 6);
    if (height_attr) {
        size_t height_len;
        const lxb_char_t *height = lxb_dom_attr_value(height_attr, &height_len);
        if (height && height_len > 0) {
            char *height_str = malloc(height_len + 1);
            memcpy(height_str, height, height_len);
            height_str[height_len] = '\0';
            int height_px = parse_css_length(height_str, 100);
            set_json_number(elem_json, "height", height_px);
            free(height_str);
        }
    }
}

// ========== TEXTAREA ==========
if (strcasecmp(tag, "textarea") == 0) {
    // Defaults are already set in set_element_specific_defaults
    // Now parse specific attributes
    
        // Get required attribute
        lxb_dom_attr_t *required_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"required", 8);
        if (required_attr) {
            set_json_bool(elem_json, "required", 1);
        }
    
    // Get rows attribute
    lxb_dom_attr_t *rows_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"rows", 4);
    if (rows_attr) {
        size_t rows_len;
        const lxb_char_t *rows_value = lxb_dom_attr_value(rows_attr, &rows_len);
        if (rows_value && rows_len > 0) {
            char *rows_str = malloc(rows_len + 1);
            memcpy(rows_str, rows_value, rows_len);
            rows_str[rows_len] = '\0';
            int rows = atoi(rows_str);
            set_json_number(elem_json, "textarea_rows", rows);
            // Adjust height based on rows
            set_json_number(elem_json, "height", rows * 20);
            free(rows_str);
        }
    }
    
    // Get cols attribute
    lxb_dom_attr_t *cols_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"cols", 4);
    if (cols_attr) {
        size_t cols_len;
        const lxb_char_t *cols_value = lxb_dom_attr_value(cols_attr, &cols_len);
        if (cols_value && cols_len > 0) {
            char *cols_str = malloc(cols_len + 1);
            memcpy(cols_str, cols_value, cols_len);
            cols_str[cols_len] = '\0';
            int cols = atoi(cols_str);
            set_json_number(elem_json, "textarea_cols", cols);
            // Adjust width based on cols
            set_json_number(elem_json, "width", cols * 8);
            free(cols_str);
        }
    }
    
    // Get placeholder
    lxb_dom_attr_t *placeholder_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"placeholder", 11);
    if (placeholder_attr) {
        size_t placeholder_len;
        const lxb_char_t *placeholder_value = lxb_dom_attr_value(placeholder_attr, &placeholder_len);
        if (placeholder_value && placeholder_len > 0) {
            char *placeholder_str = malloc(placeholder_len + 1);
            memcpy(placeholder_str, placeholder_value, placeholder_len);
            placeholder_str[placeholder_len] = '\0';
            set_json_string(elem_json, "placeholder", placeholder_str);
            free(placeholder_str);
        }
    }
    
    // Get name attribute
    lxb_dom_attr_t *name_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"name", 4);
    if (name_attr) {
        size_t name_len;
        const lxb_char_t *name_value = lxb_dom_attr_value(name_attr, &name_len);
        if (name_value && name_len > 0) {
            char *name_str = malloc(name_len + 1);
            memcpy(name_str, name_value, name_len);
            name_str[name_len] = '\0';
            set_json_string(elem_json, "input_name", name_str);
            free(name_str);
        }
    }
    
    // Get text content (default value between tags)
    char *content = get_element_text_simple(elem);
    if (content && strlen(content) > 0) {
        char *start = content;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        if (strlen(start) > 0) {
            set_json_string(elem_json, "value", start);
        }
        free(content);
    }
}

 if (strcasecmp(tag, "figure") == 0) {
    set_json_string(elem_json, "display", "block");
    set_json_number(elem_json, "margin_top", 10);
    set_json_number(elem_json, "margin_bottom", 10);
    set_json_number(elem_json, "padding", 10);
    set_json_string(elem_json, "text_align", "center");
}
 if (strcasecmp(tag, "figcaption") == 0) {
    set_json_string(elem_json, "display", "block");
    set_json_number(elem_json, "font_size", 14);
    set_json_string(elem_json, "font_style", "italic");
    set_json_number(elem_json, "margin_top", 5);
}

   // 🚀 SIVI BOX ISPRAVKA: Upisujemo ispravan tekstualni heksadecimalni format stringa!
    const char *class_str = get_json_string(elem_json, "class_string", "");
    if (strstr(class_str, "result") != NULL) {
        set_json_string(elem_json, "bg_color", "#E0E0E0");
        set_json_string(elem_json, "background_color", "#E0E0E0");
    }
    // ========== HANDLE FONT ATTRIBUTES ==========
    if (strcasecmp(tag, "font") == 0) {
        lxb_dom_attr_t *face_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"face", 4);
        if (face_attr) {
            size_t face_len;
            const lxb_char_t *face_value = lxb_dom_attr_value(face_attr, &face_len);
            if (face_value && face_len > 0) {
                char *face_str = malloc(face_len + 1);
                memcpy(face_str, face_value, face_len);
                face_str[face_len] = '\0';
                set_json_string(elem_json, "font_family", face_str);
                free(face_str);
            }
        }



        lxb_dom_attr_t *size_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"size", 4);
        if (size_attr) {
            size_t size_len;
            const lxb_char_t *size_value = lxb_dom_attr_value(size_attr, &size_len);
            if (size_value && size_len > 0) {
                char *size_str = malloc(size_len + 1);
                memcpy(size_str, size_value, size_len);
                size_str[size_len] = '\0';
                int size_num = atoi(size_str);
                if (size_num >= 1 && size_num <= 7) {
                    int pixel_sizes[] = {10, 12, 14, 16, 18, 24, 32};
                    set_json_number(elem_json, "font_size", pixel_sizes[size_num-1]);
                }
                free(size_str);
            }
        }
        
        lxb_dom_attr_t *color_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"color", 5);
        if (color_attr) {
            size_t color_len;
            const lxb_char_t *color_value = lxb_dom_attr_value(color_attr, &color_len);
            if (color_value && color_len > 0) {
                char *color_str = malloc(color_len + 1);
                memcpy(color_str, color_value, color_len);
                color_str[color_len] = '\0';
                set_json_string(elem_json, "color", color_str);
                free(color_str);
            }
        }
    }

    // ========== PROCESS CHILDREN RECURSIVELY ==========
    cJSON *children_array = cJSON_CreateArray();
    int child_count = 0;

    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    while (child) {
        // Skip table-specific elements if this is a table element (they're already in table_data)
        int skip = 0;
        if (strcasecmp(tag, "table") == 0 && child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t child_tag_len;
            const lxb_char_t *child_tag_name = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
            if (child_tag_name) {
                char child_tag[16];
                int len = child_tag_len < 15 ? child_tag_len : 15;
                memcpy(child_tag, child_tag_name, len);
                child_tag[len] = '\0';
                
                // ===== PRESKOČI SAMO ONE KOJE NE SADRŽE INPUT =====
                // caption, thead, tfoot se preskaču (već obrađeni u table_data)
                // tr i td se NE preskaču - rekurzivno se obrađuju
                if (strcasecmp(child_tag, "caption") == 0 ||
                    strcasecmp(child_tag, "thead") == 0 ||
                    strcasecmp(child_tag, "tfoot") == 0) {
                    skip = 1;
                }
                // ===== NE PRESKAČI tr, td, tbody =====
                // (oni će biti obrađeni rekurzivno)
            }
        }
        
        if (!skip) {
            cJSON *child_json = procesuiraj_elemente(child, depth + 1, elem_json);
            if (child_json) {
                cJSON_AddItemToArray(children_array, child_json);
                child_count++;
            }
        }
        child = lxb_dom_node_next(child);
    }

    if (child_count > 0) {
        cJSON_AddItemToObject(elem_json, "children", children_array);
    } else {
        cJSON_Delete(children_array);
    }

    // Set parent_id again (in case it was overwritten)
    int final_parent_id = parent_json ? get_json_number(parent_json, "element_id", -1) : -1;
    set_json_number(elem_json, "parent_id", final_parent_id);

    free(tag);
    return elem_json;
}

void dodaj_css(cJSON *element) {
    if (!element) return;
    
    // If this is an array, process each item
    if (cJSON_IsArray(element)) {
        cJSON *child;
        cJSON_ArrayForEach(child, element) {
            dodaj_css(child);  // Recurse into each array item
        }
        return;
    }
    
    // Now element is an object with a tag
    apply_css_to_element(element);
    
    // Recurse into children
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            dodaj_css(child);
        }
    }
}


void add_attribute_to_children(cJSON *root, const char *parent_tag, 
    const char *attr_name, const char *attr_value, 
    int mode) {
if (!root) return;

cJSON *child = NULL;
cJSON_ArrayForEach(child, root) {
if (!cJSON_IsObject(child)) continue;

cJSON *tag = cJSON_GetObjectItem(child, "tag");
if (tag && cJSON_IsString(tag) && strcmp(tag->valuestring, parent_tag) == 0) {
// Found parent element, mark ALL descendants recursively
mark_all_descendants(child, attr_name, attr_value, mode);
}

// Recurse into children to find more parent elements
cJSON *children = cJSON_GetObjectItem(child, "children");
if (children && cJSON_IsArray(children)) {
add_attribute_to_children(children, parent_tag, attr_name, attr_value, mode);
}
}
}

void mark_all_descendants(cJSON *element, const char *attr_name, 
    const char *attr_value, int mode) {
    if (!element) return;

    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (!children || !cJSON_IsArray(children)) return;

    // 🚀 INŽENJERSKI ŠTIT: Proveravamo da li je roditeljski element trenutno sakriven u CSS-u
    const char *parent_display = get_json_string(element, "display", "");
    int parent_is_hidden = (strcmp(parent_display, "none") == 0 || get_json_bool(element, "is_visible", 1) == 0);

    cJSON *child;
    cJSON_ArrayForEach(child, children) {
        if (!cJSON_IsObject(child)) continue;

        // 🚀 KASKADNO SAKRIVANJE: Ako je roditelj sakriven, dete MORA naslediti display: none
        if (parent_is_hidden) {
            set_json_string(child, "display", "none");
            set_json_bool(child, "is_visible", 0);
            
            // Postavi i geometrijske dimenzije na 0 da layout engine odmah preskoči proračun
            set_json_number(child, "width", 0);
            set_json_number(child, "height", 0);
            set_json_number(child, "layout_calculated", 1);
        }

        // Apply attribute to this child (Tvoja originalna logika)
        if (mode == 1) {  // String
            // Ako je roditelj sakriven, ne dozvoli da mu defaultni "block" ili "flex" prepise "none"
            if (!(parent_is_hidden && strcmp(attr_name, "display") == 0)) {
                cJSON *existing = cJSON_GetObjectItem(child, attr_name);
                if (existing) {
                    cJSON_ReplaceItemInObject(child, attr_name, cJSON_CreateString(attr_value));
                } else {
                    cJSON_AddStringToObject(child, attr_name, attr_value);
                }
            }
        }
        else if (mode == 2) {  // Boolean
            // Ako je roditelj sakriven, ne dozvoli da mu kaskada postavi is_visible na true (1)
            if (!(parent_is_hidden && strcmp(attr_name, "is_visible") == 0)) {
                int bool_val = (strcasecmp(attr_value, "true") == 0 ||
                                strcasecmp(attr_value, "yes") == 0 ||
                                strcasecmp(attr_value, "1") == 0 ||
                                strcasecmp(attr_value, "on") == 0);
                
                cJSON *existing = cJSON_GetObjectItem(child, attr_name);
                if (existing) {
                    cJSON_ReplaceItemInObject(child, attr_name, cJSON_CreateBool(bool_val));
                } else {
                    cJSON_AddBoolToObject(child, attr_name, bool_val);
                }
            }
        }
        else if (mode == 0) {  // Delete
            // Ako je roditelj sakriven, nemoj brisati display i is_visible jer su nam ključni
            if (!(parent_is_hidden && (strcmp(attr_name, "display") == 0 || strcmp(attr_name, "is_visible") == 0))) {
                cJSON_DeleteItemFromObject(child, attr_name);
            }
        }

        // Rekurzivno prenesi pravilo na sledeći nivo dece
        mark_all_descendants(child, attr_name, attr_value, mode);
    }
}



// =========================================================================
// 🚀 POMOĆNA FUNKCIJA: PROVERA KLASA REČ PO REČ
// =========================================================================
static int ima_tacnu_klasu(const char *sve_klase, const char *trazena_klasa) {
    // OSIGURAČ: Ako je bilo koji pokazivač NULL ili prazan, odmah vraćamo 0!
    if (!sve_klase || !trazena_klasa || sve_klase[0] == '\0' || trazena_klasa[0] == '\0') {
        return 0;
    }
    
    const char *ptr = strstr(sve_klase, trazena_klasa);
    while (ptr) {
        // Provera ispred: početak stringa ili razmak
        int ispred_ok = (ptr == sve_klase || *(ptr - 1) == ' ');
        
        // Provera iza: kraj stringa ili razmak
        size_t len = strlen(trazena_klasa);
        int iza_ok = (*(ptr + len) == '\0' || *(ptr + len) == ' ');
        
        if (ispred_ok && iza_ok) {
            return 1; // Klasa je pronađena kao cela, izolovana reč!
        }
        
        // Tražimo dalje ako je ovo bila samo pod-reč
        ptr = strstr(ptr + 1, trazena_klasa);
    }
    return 0;
}


void apply_css_to_element(cJSON *elem_json) {
    if (!elem_json) return;
    
    const char *tag = get_json_string(elem_json, "tag", "");
    
    const char *element_id = get_json_string(elem_json, "id", "");
    const char *element_classes = get_json_string(elem_json, "class_string", "");
    int is_menu=0;
    is_menu = get_json_bool(elem_json, "is_menu", 0);


 
    // Loop through all CSS rules
    for (int i = 0; i < css_rule_count; i++) {
        CSSRule *rule = &css_rules[i];
        int matches = 0;
    
        
        // ---------------- Copy selector ----------------
        char selector_copy[256];
        strncpy(selector_copy, rule->selector, sizeof(selector_copy)-1);
        selector_copy[sizeof(selector_copy)-1] = '\0';
        
        char *selector = selector_copy;
        
        // ---------------- REMOVE PSEUDO ----------------
        char *colon = strchr(selector, ':');
        if (colon) *colon = '\0';
        
        // ---------------- TRIM WHITESPACE ----------------
        while (*selector == ' ') selector++;
        size_t len = strlen(selector);
        while (len > 0 && selector[len-1] == ' ') selector[--len] = '\0';
        
       // ---------------- SPLIT SELECTOR ----------------
char *parts[10];
int part_count = 0;
char *token = strtok(selector, " ");
while (token && part_count < 10) {
    parts[part_count++] = token;
    token = strtok(NULL, " ");
}

        
  // ---------------- MATCH SELECTOR ----------------

// FIRST: Descendant selector (nav a, .vertical-nav a, etc.)
// For a selector like ".horizontal-nav a"
// parts[0] = ".horizontal-nav"
// parts[1] = "a"

// FIRST: Descendant selector (nav a, .vertical-nav a, etc.)
if (part_count >= 2) {
    // Last part must match current element
    if (strcasecmp(tag, parts[part_count-1]) == 0) {
        // Get the orientation from the element (already propagated)
        const char *orientation = get_json_string(elem_json, "menu_orientation", NULL);
    
        // Skip rules that contain :hover
        if (strstr(rule->selector, ":hover") != NULL ||
        strstr(rule->selector, ":visited") != NULL ||
        strstr(rule->selector, ":active") != NULL ||
        strstr(rule->selector, ":focus") != NULL ||
        strstr(rule->selector, ":link") != NULL) {
        continue;  // Skip all dynamic pseudo-class rules
    } 
        // Check if this is a horizontal or vertical menu rule
        if (strstr(rule->selector, "horizontal") != NULL) {
            if (orientation && strcmp(orientation, "horizontal") == 0) {
                matches = 1;
            }
        }
        else if (strstr(rule->selector, "vertical") != NULL) {
            if (orientation && strcmp(orientation, "vertical") == 0) {
                matches = 1;
            }
        }

    }
}

// THAN: ID selector (#outer)
else if (rule->selector[0] == '#') {
    if (strlen(element_id) > 0 && 
        strcmp(rule->selector + 1, element_id) == 0) {
        matches = 1;
    }
}
// THEN: Class selector (.myclass) - POPRAVLJENO sa preciznom proverom reč po reč i podrškom za višestruke klase (.gb_7.gb_8)
else if (rule->selector[0] == '.') {
    const char *full_class_selector = rule->selector + 1;
    
    if (strchr(full_class_selector, '.') == NULL) {
        if (strlen(element_classes) > 0 && ima_tacnu_klasu(element_classes, full_class_selector)) {
            matches = 1;
        }
    } 
    else {
        char comb_copy[256];
        strncpy(comb_copy, full_class_selector, sizeof(comb_copy) - 1);
        comb_copy[sizeof(comb_copy) - 1] = '\0';
        
        int all_classes_match = 1;
        char *sub_saveptr;
        char *single_class = strtok_r(comb_copy, ".", &sub_saveptr);
        
        while (single_class) {
            if (strlen(element_classes) == 0 || !ima_tacnu_klasu(element_classes, single_class)) {
                all_classes_match = 0;
                break;
            }
            single_class = strtok_r(NULL, ".", &sub_saveptr);
        }
        
        if (all_classes_match && strlen(full_class_selector) > 0) {
            matches = 1;
        }
    }
}
// PODRŠKA ZA TAG I JEDNU ILI VIŠE KLASA (npr. div.gb_Ra.gb_cb)
else if (strchr(rule->selector, '.') != NULL && rule->selector[0] != '.') {
    char t_copy[256];
    strncpy(t_copy, rule->selector, sizeof(t_copy) - 1);
    t_copy[sizeof(t_copy) - 1] = '\0';
    char *dot_ptr = strchr(t_copy, '.');
    if (dot_ptr) {
        *dot_ptr = '\0';
        char *classes_part = dot_ptr + 1;
        
        if (strcasecmp(tag, t_copy) == 0 && strlen(element_classes) > 0) {
            int all_sub_classes_match = 1;
            char *sub_cls_saveptr;
            char *single_sub_class = strtok_r(classes_part, ".", &sub_cls_saveptr);
            
            while (single_sub_class) {
                if (!ima_tacnu_klasu(element_classes, single_sub_class)) {
                    all_sub_classes_match = 0;
                    break;
                }
                single_sub_class = strtok_r(NULL, ".", &sub_cls_saveptr);
            }
            
            if (all_sub_classes_match) {
                matches = 1;
            }
        }
    }
}
// THEN: Simple tag selector
else if (part_count == 1) {
    if (strcmp(rule->selector, tag) == 0) {
        matches = 1;
    }
}

  
        // ---------------- APPLY PROPERTY ----------------
        if (matches) {
    // ========== SIDEBAR DETECTION ==========
    // Check if this rule indicates a sidebar
    const char *selector = rule->selector;
    
// Detect vertical navigation (potential sidebar)
if (strstr(selector, "vertical") != NULL || 
    strstr(selector, "sidebar") != NULL ||
    strstr(selector, "side-bar") != NULL) {
    
    // Mark as sidebar candidate
    set_json_bool(elem_json, "is_sidebar", 1);
    
    // Determine placement based on selector or class
    if (strstr(selector, "left") != NULL) {
        set_json_string(elem_json, "sidebar_place", "left");
    } else if (strstr(selector, "right") != NULL) {
        set_json_string(elem_json, "sidebar_place", "right");
    } else {
        // Default to left if not specified
        set_json_string(elem_json, "sidebar_place", "left");
    }
    
    // Set sidebar width if specified in CSS
    int width = get_json_number(elem_json, "width", 0);
    if (width > 0) {
        set_json_number(elem_json, "sidebar_width", width);
    } else {
        set_json_number(elem_json, "sidebar_width", 200);  // Default width
    }
}

// Also check for role="complementary" or role="navigation" with vertical orientation
const char *role = get_json_string(elem_json, "role", "");
if (strcmp(role, "complementary") == 0) {
    set_json_bool(elem_json, "is_sidebar", 1);
    
    // Only set if not already set
    const char *current_place = get_json_string(elem_json, "sidebar_place", "");
    if (strlen(current_place) == 0 || strcmp(current_place, "none") == 0) {
        set_json_string(elem_json, "sidebar_place", "right");  // complementary often right
    }
}

if (strcmp(rule->property, "color") == 0) {
    set_json_string(elem_json, "color", rule->value);
}
else if (strcmp(rule->property, "background-color") == 0) {
    set_json_string(elem_json, "bg_color", rule->value);
}
else if (strcmp(rule->property, "font-size") == 0) {
    int fs = parse_css_length(rule->value, 16);
    set_json_number(elem_json, "font_size", fs);
}
else if (strcmp(rule->property, "font-weight") == 0) {
    set_json_string(elem_json, "font_weight", rule->value);
}
else if (strcmp(rule->property, "font-style") == 0) {
    set_json_string(elem_json, "font_style", rule->value);
}
else if (strcmp(rule->property, "font-family") == 0) {
    set_json_string(elem_json, "font_family", rule->value);
}
else if (strcmp(rule->property, "text-align") == 0) {
    set_json_string(elem_json, "text_align", rule->value);
}
else if (strcmp(rule->property, "text-decoration") == 0) {
    set_json_string(elem_json, "text_decoration", rule->value);
}
else if (strcmp(rule->property, "text-transform") == 0) {
    set_json_string(elem_json, "text_transform", rule->value);
}

// ========== SAKRIVANJE MOBILNIH VERZIJA (DISPLAY: NONE) ==========
else if (strcmp(rule->property, "display") == 0) {
    set_json_string(elem_json, "display", rule->value);
    
    // Koristimo tvoju set_json_bool funkciju za vidljivost elemenata
    if (strcmp(rule->value, "none") == 0) {
        set_json_bool(elem_json, "is_visible", 0); // Sakrij element!
    } else {
        set_json_bool(elem_json, "is_visible", 1); // Prikaži element!
    }
}

// ========== BOX MODEL I STRUCTURALNA SVOJSTVA ==========
else if (strcmp(rule->property, "margin") == 0) {
    set_json_string(elem_json, "margin", rule->value);
}
else if (strcmp(rule->property, "padding") == 0) {
    set_json_string(elem_json, "padding", rule->value);
}
else if (strcmp(rule->property, "white-space") == 0) {
    set_json_string(elem_json, "white-space", rule->value);
}
else if (strcmp(rule->property, "width") == 0) {
    int width_value = parse_css_length(rule->value, 800);
    set_json_number(elem_json, "width", width_value);
    set_json_number(elem_json, "attr_width", width_value);
}
else if (strcmp(rule->property, "height") == 0) {
    int height_value = parse_css_length(rule->value, 0);
    set_json_number(elem_json, "height", height_value);
}
else if (strcmp(rule->property, "border-radius") == 0) {
    set_json_string(elem_json, "border-radius", rule->value);
}
else if (strcmp(rule->property, "box-shadow") == 0) {
    set_json_string(elem_json, "box-shadow", rule->value);
}
else if (strcmp(rule->property, "background-image") == 0) {
    set_json_string(elem_json, "background-image", rule->value);
}
else if (strcmp(rule->property, "background-size") == 0) {
    set_json_string(elem_json, "background-size", rule->value);
}
else if (strcmp(rule->property, "background-position") == 0) {
    set_json_string(elem_json, "background-position", rule->value);
}
else if (strcmp(rule->property, "background-repeat") == 0) {
    set_json_string(elem_json, "background-repeat", rule->value);
}
else if (strcmp(rule->property, "background-attachment") == 0) {
    set_json_string(elem_json, "background-attachment", rule->value);
}
            
            // Box model properties
            else if (strcmp(rule->property, "width") == 0) {
                // Parse CSS width to pixels
                int width_value = parse_css_length(rule->value, 800); // 800 = default container width
                cJSON_ReplaceItemInObject(elem_json, "width", cJSON_CreateNumber(width_value));
                cJSON_AddNumberToObject(elem_json, "attr_width", width_value);
            }
            else if (strcmp(rule->property, "height") == 0) {
                int height_value = parse_css_length(rule->value, 600);
                cJSON_ReplaceItemInObject(elem_json, "height", cJSON_CreateNumber(height_value));
                cJSON_AddNumberToObject(elem_json, "attr_height", height_value);
            }
            else if (strcmp(rule->property, "margin") == 0) {
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = 0;
                
                char *values[4] = {0};
                int val_count = 0;
                char *token = strtok(tmp, " ");
                while (token && val_count < 4) {
                    values[val_count++] = token;
                    token = strtok(NULL, " ");
                }
                
                int top, right, bottom, left;
                
                if (val_count == 1) {
                    top = right = bottom = left = parse_css_length(values[0], 0);
                } else if (val_count == 2) {
                    top = bottom = parse_css_length(values[0], 0);
                    right = left = parse_css_length(values[1], 0);
                } else if (val_count == 3) {
                    top = parse_css_length(values[0], 0);
                    right = left = parse_css_length(values[1], 0);
                    bottom = parse_css_length(values[2], 0);
                } else if (val_count == 4) {
                    top = parse_css_length(values[0], 0);
                    right = parse_css_length(values[1], 0);
                    bottom = parse_css_length(values[2], 0);
                    left = parse_css_length(values[3], 0);
                } else {
                    top = right = bottom = left = 0;
                }
                
                set_json_number(elem_json, "margin_top", top);
                set_json_number(elem_json, "margin_right", right);
                set_json_number(elem_json, "margin_bottom", bottom);
                set_json_number(elem_json, "margin_left", left);
            }
            else if (strcmp(rule->property, "padding") == 0) {
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = 0;
                
                char *values[4] = {0};
                int val_count = 0;
                char *token = strtok(tmp, " ");
                while (token && val_count < 4) {
                    values[val_count++] = token;
                    token = strtok(NULL, " ");
                }
                
                int top, right, bottom, left;
                
                if (val_count == 1) {
                    top = right = bottom = left = atoi(values[0]);
                } else if (val_count == 2) {
                    top = bottom = atoi(values[0]);
                    right = left = atoi(values[1]);
                } else if (val_count == 3) {
                    top = atoi(values[0]);
                    right = left = atoi(values[1]);
                    bottom = atoi(values[2]);
                } else if (val_count == 4) {
                    top = atoi(values[0]);
                    right = atoi(values[1]);
                    bottom = atoi(values[2]);
                    left = atoi(values[3]);
                } else {
                    top = right = bottom = left = 0;
                }
                
                cJSON_ReplaceItemInObject(elem_json, "padding_top", cJSON_CreateNumber(top));
                cJSON_ReplaceItemInObject(elem_json, "padding_right", cJSON_CreateNumber(right));
                cJSON_ReplaceItemInObject(elem_json, "padding_bottom", cJSON_CreateNumber(bottom));
                cJSON_ReplaceItemInObject(elem_json, "padding_left", cJSON_CreateNumber(left));
            }

            else if (rule->property && strcmp(rule->property, "opacity") == 0) {
 
                // Do nothing else - just log and continue
            }
            else if (rule->property && strcmp(rule->property, "filter") == 0) {
                if (rule->value && rule->value[0] != '\0') {
                 //   printf("🎨 Filter found: '%s' (skipping for now)\n", rule->value);
                    // Don't process further - just prevent crashes
                }
            }
            else if (strcmp(rule->property, "border") == 0) {
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = 0;
                
                char *bw = strtok(tmp, " ");
                char *bs = strtok(NULL, " ");
                char *bc = strtok(NULL, " ");
                
                if (bw) cJSON_ReplaceItemInObject(elem_json, "border_width", 
                                                 cJSON_CreateString(bw));
                if (bs) cJSON_ReplaceItemInObject(elem_json, "border_style", 
                                                 cJSON_CreateString(bs));
                if (bc) cJSON_ReplaceItemInObject(elem_json, "border_color", 
                                                 cJSON_CreateString(bc));
            }
            else if (strcmp(rule->property, "border-width") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "border_width",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "border-style") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "border_style",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "border-color") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "border_color",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "border-radius") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "border_radius",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "border-bottom") == 0) {
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = 0;
                
                char *bw = strtok(tmp, " ");
                char *bs = strtok(NULL, " ");
                char *bc = strtok(NULL, " ");
                
                if (bw) {
                    cJSON *existing = cJSON_GetObjectItem(elem_json, "border_bottom_width");
                    if (existing) {
                        cJSON_ReplaceItemInObject(elem_json, "border_bottom_width", 
                                                  cJSON_CreateString(bw));
                    } else {
                        cJSON_AddStringToObject(elem_json, "border_bottom_width", bw);
                    }
                }
                if (bs) {
                    cJSON *existing = cJSON_GetObjectItem(elem_json, "border_bottom_style");
                    if (existing) {
                        cJSON_ReplaceItemInObject(elem_json, "border_bottom_style", 
                                                  cJSON_CreateString(bs));
                    } else {
                        cJSON_AddStringToObject(elem_json, "border_bottom_style", bs);
                    }
                }
                if (bc) {
                    cJSON *existing = cJSON_GetObjectItem(elem_json, "border_bottom_color");
                    if (existing) {
                        cJSON_ReplaceItemInObject(elem_json, "border_bottom_color", 
                                                  cJSON_CreateString(bc));
                    } else {
                        cJSON_AddStringToObject(elem_json, "border_bottom_color", bc);
                    }
                }
            }
            // Layout properties
            else if (strcmp(rule->property, "display") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "display",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "position") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "position",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "float") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "float",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "clear") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "clear",
                    cJSON_CreateString(rule->value));
            }
            
            // List properties
            else if (strcmp(rule->property, "list-style-type") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "list_style_type",
                    cJSON_CreateString(rule->value));
            }

            // da maknemo underline is menija
            else if (is_menu ) {
                // Override underline for menu links
                cJSON_ReplaceItemInObject(elem_json, "text_decoration", cJSON_CreateString("none"));
            }

            else if (strcmp(rule->property, "padding") == 0) {
                char tmp[128];
                strncpy(tmp, rule->value, sizeof(tmp)-1);
                tmp[sizeof(tmp)-1] = 0;
                
                char *values[4] = {0};
                int val_count = 0;
                char *token = strtok(tmp, " ");
                while (token && val_count < 4) {
                    values[val_count++] = token;
                    token = strtok(NULL, " ");
                }
                
                int top, right, bottom, left;
                
                // Use parse_css_length instead of atoi
                if (val_count == 1) {
                    top = right = bottom = left = parse_css_length(values[0], 0);
                } else if (val_count == 2) {
                    top = bottom = parse_css_length(values[0], 0);
                    right = left = parse_css_length(values[1], 0);
                } else if (val_count == 3) {
                    top = parse_css_length(values[0], 0);
                    right = left = parse_css_length(values[1], 0);
                    bottom = parse_css_length(values[2], 0);
                } else if (val_count == 4) {
                    top = parse_css_length(values[0], 0);
                    right = parse_css_length(values[1], 0);
                    bottom = parse_css_length(values[2], 0);
                    left = parse_css_length(values[3], 0);
                } else {
                    top = right = bottom = left = 0;
                }
                
                set_json_number(elem_json, "padding_top", top);
                set_json_number(elem_json, "padding_right", right);
                set_json_number(elem_json, "padding_bottom", bottom);
                set_json_number(elem_json, "padding_left", left);
            }
                      else if (strcmp(rule->property, "word-wrap") == 0 ||
                     strcmp(rule->property, "overflow-wrap") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "word_wrap",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "white-space") == 0) {
                cJSON_ReplaceItemInObject(elem_json, "white_space",
                    cJSON_CreateString(rule->value));
            }
            else if (strcmp(rule->property, "max-width") == 0) {
                int max_width = parse_css_length(rule->value, 800);
                cJSON_ReplaceItemInObject(elem_json, "max_width",
                    cJSON_CreateNumber(max_width));
            }

              // Text wrapping properties
              else if (strcmp(rule->property, "word-wrap") == 0 ||
              strcmp(rule->property, "overflow-wrap") == 0) {
         cJSON_ReplaceItemInObject(elem_json, "word_wrap",
             cJSON_CreateString(rule->value));
     }
     else if (strcmp(rule->property, "white-space") == 0) {
         cJSON_ReplaceItemInObject(elem_json, "white_space",
             cJSON_CreateString(rule->value));
     }
     else if (strcmp(rule->property, "max-width") == 0) {
         int max_w = parse_css_length(rule->value, 800);
         cJSON_ReplaceItemInObject(elem_json, "max_width",
             cJSON_CreateNumber(max_w));
     }
     else if (strcmp(rule->property, "overflow-x") == 0) {
         cJSON_ReplaceItemInObject(elem_json, "overflow_x",
             cJSON_CreateString(rule->value));
     }
            // ... rest of property handlers
            
        }
    }
    
      // =========================================================================
    // 🖥️ NEPROBOJNI NEXT.JS SEMANTIČKI PC FILTER (POPRAVLJENO)
    // Uspešno hvata klase sa sufiksima i rešava preklapanje jednom zauvek!
    // =========================================================================
    if (element_classes && element_classes[0] != '\0') {
        
        // ❌ 1. Gasimo mobilne elemente (Hvata podstringove sa tvog najnovijeg Gista!)
        if (strstr(element_classes, "WrapMobile") != NULL || 
            strstr(element_classes, "headerWrapMobile") != NULL || 
            strstr(element_classes, "minimalHomepage") != NULL ||
            strstr(element_classes, "minimal-homepage") != NULL ||
            strstr(element_classes, "skeleton") != NULL ||
            strstr(element_classes, "mobileSearchbox") != NULL) {
            
            // Koristimo tvoje bezbedne funkcije za upis
            set_json_string(elem_json, "display", "none");
            set_json_bool(elem_json, "is_visible", 0);
            
            if(INFO_MESSAGES) printf("📱 [PC Filter] Uspešno ugašen mobilni/tablet element (ID: %s)\n", element_id);
        }
        
        // 🖥️ 2. Eksplicitno palimo i forsiramo puni PC raspored sa ikonama
        else if (strstr(element_classes, "WrapDesktop") != NULL || 
                 strstr(element_classes, "headerWrapDesktop") != NULL ||
                 strstr(element_classes, "promoHomepage") != NULL) {
            
            set_json_string(elem_json, "display", "block");
            set_json_bool(elem_json, "is_visible", 1);
            
            if(INFO_MESSAGES) printf("🖥️ [PC Filter] Uspešno forsiran DESKTOP element sa ikonama (ID: %s)\n", element_id);
        }
    }
    
    // Recurse into children (this was inside the loop before!)
    cJSON *children = cJSON_GetObjectItem(elem_json, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            apply_css_to_element(child);
        }
    }
}


void cleanup_previous_page(pauk_ui_t *pauk_ui) {
    if (!pauk_ui) return;
    printf("🧹 Cleaning up previous page...\n");

    // ===== CLEAN UP JAVASCRIPT STATE =====
    js_reset_all_state();
    // ====================================

    // 1. Stop timers
    hover_timer_running = 0;
    if (pauk_ui->hover_timer) {
        fibril_timer_clear(pauk_ui->hover_timer);
        fibril_timer_destroy(pauk_ui->hover_timer);
        pauk_ui->hover_timer = NULL;
    }
    if (pauk_ui->status_timer) {
        fibril_timer_clear(pauk_ui->status_timer);
        fibril_timer_destroy(pauk_ui->status_timer);
        pauk_ui->status_timer = NULL;
    }

    // 2. Reset focus/hover states
    pauk_ui->focused_element = NULL;
    pauk_ui->current_hover = NULL;
    pauk_ui->cursor_position = 0;
    pauk_ui->scroll_y = 0;

    // 3. Clear JSON memory - avoid double-free
    cJSON *old_rendering = pauk_ui->rendering_json;
    cJSON *old_layout = global_computed_layout;

    pauk_ui->rendering_json = NULL;
    pauk_ui->current_json = NULL;
    global_computed_layout = NULL;

    if (old_rendering && old_layout && old_rendering == old_layout) {
        // Same object - delete once
        cJSON_Delete(old_rendering);
    } else {
        // Different objects - delete both
        if (old_rendering) cJSON_Delete(old_rendering);
        if (old_layout) cJSON_Delete(old_layout);
    }

    // 4. Clear CSS rules
    if (css_rules) {
        for (int i = 0; i < css_rule_count; i++) {
            free(css_rules[i].selector);
            free(css_rules[i].property);
            free(css_rules[i].value);
        }
        free(css_rules);
        css_rules = NULL;
    }
    css_rule_count = 0;

    // 5. Clear image cache
    clear_image_cache();

    // 6. Reset element ID counter
    g_next_element_id = 1;

    // 7. Destroy old Lexbor document
    if (current_doc) {
        lxb_html_document_clean(current_doc);
        lxb_html_document_destroy(current_doc);
        current_doc = NULL;
    }

    // 8. Reset scrollbar
    if (pauk_ui->vscrollbar) {
        ui_scrollbar_set_pos(pauk_ui->vscrollbar, 0);
    }

    // 9. Clear status bar
    if (pauk_ui->status_label) {
        ui_label_set_text(pauk_ui->status_label, "");
        ui_label_paint(pauk_ui->status_label);
    }

    printf("✅ Previous page cleanup complete\n");
}

void apply_js_modifications_to_dom(cJSON *root, cJSON *modifications) {
    if (!root || !modifications) {
        return;
    }
    
    cJSON *mod = NULL;
    cJSON_ArrayForEach(mod, modifications) {
        const char *element_id = mod->string;
        cJSON *properties = mod;
        
        // Find element by string ID (from HTML id attribute)
        cJSON *target = find_element_by_string_id(root, element_id);
        if (!target) {
            continue;
        }
        
        // Apply style modifications
        cJSON *style = cJSON_GetObjectItem(properties, "style");
        if (style) {
            cJSON *prop = NULL;
            cJSON_ArrayForEach(prop, style) {
                const char *prop_name = prop->string;
                const char *prop_value = prop->valuestring;
                
                if (strcmp(prop_name, "color") == 0) {
                    set_json_string(target, "color", prop_value);
                } else if (strcmp(prop_name, "background-color") == 0) {
                    set_json_string(target, "bg_color", prop_value);
                } else if (strcmp(prop_name, "font-size") == 0) {
                    int fs = parse_css_length(prop_value, 16);
                    set_json_number(target, "font_size", fs);
                } else if (strcmp(prop_name, "width") == 0) {
                    int w = parse_css_length(prop_value, 0);
                    set_json_number(target, "width", w);
                } else if (strcmp(prop_name, "height") == 0) {
                    int h = parse_css_length(prop_value, 0);
                    set_json_number(target, "height", h);
                } else {
                    set_json_string(target, prop_name, prop_value);
                }
            }
        }
        
        // Apply innerHTML/content modifications
        cJSON *inner_html = cJSON_GetObjectItem(properties, "innerHTML");
        if (inner_html && inner_html->valuestring) {
            set_json_string(target, "text", inner_html->valuestring);
            
            // Update child text node if exists
            cJSON *children = cJSON_GetObjectItem(target, "children");
            if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
                cJSON *text_node = cJSON_GetArrayItem(children, 0);
                if (text_node && strcmp(get_json_string(text_node, "tag", ""), "text") == 0) {
                    set_json_string(text_node, "text", inner_html->valuestring);
                    set_json_string(text_node, "content", inner_html->valuestring);
                }
            }
        }
    }
}

void refresh_page_after_js(pauk_ui_t *pauk_ui) {
    if (!pauk_ui || !pauk_ui->rendering_json) return;
    
    // Save scrollbar position BEFORE refresh
    gfx_coord_t saved_scrollbar_pos = ui_scrollbar_get_pos(pauk_ui->vscrollbar);
   // printf("🔄 Saved scrollbar position: %d\n", saved_scrollbar_pos);
    
    // ===== STEP 1: Render from top =====
    pauk_ui->scroll_y = 0;
    if (pauk_ui->html_renderer) {
        pauk_ui->html_renderer->scroll_y = 0;
    }
    
    // Redraw
    test_text_rendering(pauk_ui);
    
    // ===== STEP 2: Restore scroll position =====
    ui_scrollbar_set_pos(pauk_ui->vscrollbar, saved_scrollbar_pos);
    
    // Update scroll_y from scrollbar position
    gfx_coord_t move_length = ui_scrollbar_move_length(pauk_ui->vscrollbar);
    int content_height = find_document_bottom(pauk_ui->rendering_json);
    int view_height = pauk_ui->tab_rect_base.p1.y - pauk_ui->tab_rect_base.p0.y;
    
    if (content_height > view_height && move_length > 0) {
        pauk_ui->scroll_y = (int)((float)saved_scrollbar_pos / move_length * (content_height - view_height));
      //  printf("   Restored scroll_y: %d\n", pauk_ui->scroll_y);
    }
    
    // Sync renderer
    if (pauk_ui->html_renderer) {
        pauk_ui->html_renderer->scroll_y = pauk_ui->scroll_y;
    }
    
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                      &pauk_ui->list_rect, NULL);
    gfx_update(pauk_ui->gc);
}



char* get_directory_path(const char *filepath) {
    if (!filepath) return NULL;
    
    char *copy = strdup(filepath);
    if (!copy) return NULL;
    
    // Find last '/'
    char *last_slash = strrchr(copy, '/');
    if (last_slash) {
        *(last_slash + 1) = '\0';  // Keep the slash
    } else {
        // No slash, empty string (current directory)
        copy[0] = '\0';
    }
    
    return copy;
}

// =========================================================================
// 🧹 REKURZIVNI RESET LAYOUT MARKERA (ŠAH-MAT ZA Y=0)
// Postavlja sve elemente u stanje pripravnosti tako da Layout Engine
// mora ponovo da im izračuna x i y pozicije, bez preskakanja grana!
// =========================================================================
static void prisilno_resetuj_layout_markere(cJSON *element) {
    if (!element) return;
    
    if (cJSON_IsArray(element)) {
        cJSON *child;
        cJSON_ArrayForEach(child, element) {
            prisilno_resetuj_layout_markere(child);
        }
        return;
    }
    
    // 🚀 DIREKTAN REZ: Menjamo vrednosti direktno preko C pokazivača!
    cJSON *calc = cJSON_GetObjectItem(element, "layout_calculated");
    if (calc) {
        calc->valueint = 0;
        calc->valuedouble = 0.0;
    } else {
        cJSON_AddNumberToObject(element, "layout_calculated", 0);
    }
    
    cJSON *needs = cJSON_GetObjectItem(element, "needs_layout");
    if (needs) {
        needs->type = cJSON_True; // Prisno palimo na True
        needs->valueint = 1;
    } else {
        cJSON_AddBoolToObject(element, "needs_layout", true);
    }
    
    cJSON *has = cJSON_GetObjectItem(element, "has_layout");
    if (has) {
        has->type = cJSON_False; // Poništavamo tekstualne čvorove na False
        has->valueint = 0;
    }

    // Spuštamo se duboko rekurzivno kroz decu elementa
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            prisilno_resetuj_layout_markere(child);
        }
    }
}

void load_and_render_page(const char *filename, const char *base_url) {
    if (!filename) return;
    
    // ========== START PERFORMANCE MONITORING ==========
    perf_timing_init(&page_timing);
    perf_timing_navigation_start(&page_timing);
    printf("🚀 Page load started at %" PRIu64 " ms\n", get_uptime_ms());
    
    stop_hover_timer(global_pauk_ui);
    stop_status_timer(global_pauk_ui);
    clear_all_callbacks();
    
    char constructed_base_url[512];

    init_image_catalog(global_pauk_ui);

    // If base_url is NULL or not provided, construct it from filename
    if (base_url == NULL || base_url[0] == '\0') {
        // Check if it's a local file (no :// protocol)
        if (strstr(filename, "://") == NULL) {
            // ===== FIX: Extract directory from filename =====
            char *dir_path = get_directory_path(filename);
            if (dir_path && dir_path[0] != '\0') {
                snprintf(constructed_base_url, sizeof(constructed_base_url), "file://%s", dir_path);
                free(dir_path);
            } else {
                // No directory, just use current directory
                snprintf(constructed_base_url, sizeof(constructed_base_url), "file:///");
            }
            base_url = constructed_base_url;
        } else {
            // Already has protocol, use as-is
            base_url = filename;
        }
    }
    
    set_current_base_url(base_url);
    
    hover_timer_running = 0;
    
    if (base_url) {
       // printf("📌 Base URL: %s\n", base_url);
    } else {
      // printf("📌 Local file (no base URL)\n");
    }
    int viewport_width = global_pauk_ui->tab_rect_base.p1.x - global_pauk_ui->tab_rect_base.p0.x;
    int skip_css_parsing = 0;
    int skip_css_application = 0;
  
  
    // ===== ONLY CLEANUP IF WE HAVE A PREVIOUS PAGE =====
    if (global_pauk_ui->rendering_json != NULL || current_doc != NULL) {
        printf("Previous page detected, cleaning up...\n");
        cleanup_previous_page(global_pauk_ui);
    } else {
        printf("First page load, skipping cleanup\n");
    }

    stopwatch_t step_timer;
        // ========== STEP 2: Initialize CSS Parser ==========
        stopwatch_start(&step_timer);
        if(INFO_MESSAGES) printf("\n=== STEP 2: Initialize CSS Parser ===\n");
        if (!css_parser_init()) {
            printf("WARNING: CSS parser initialization failed (continuing without CSS)\n");
        } else {
            if(INFO_MESSAGES) printf("CSS parser initialized\n");
        }
        
        add_default_css_rules();
        if(INFO_MESSAGES) printf("Default CSS rules added for text wrapping\n");
        
        if(INFO_MESSAGES) {
            printf("CSS initialization completed in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
        }
    
        
    // ========== STEP 1: Parse HTML ==========
  //  stopwatch_t step_timer;
    stopwatch_start(&step_timer);
    
    if(INFO_MESSAGES) printf("\n=== STEP 1: Parse HTML Document ===\n");
    
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open HTML file: %s\n", filename);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *html = malloc(size + 1);
    if (!html) {
        fclose(f);
        fprintf(stderr, "ERROR: Failed to allocate memory for HTML\n");
        return;
    }
    
    size_t read_size = fread(html, 1, size, f);
    html[read_size] = '\0';
    fclose(f);

    // Clean up previous document if exists
    if (current_doc) {
        lxb_html_document_destroy(current_doc);
        current_doc = NULL;
    }
    
    current_doc = lxb_html_document_create();
    if (!current_doc) {
        free(html);
        fprintf(stderr, "ERROR: Failed to create Lexbor document\n");
        return;
    }
    
    lexbor_status_t status = lxb_html_document_parse(current_doc, (lxb_char_t*)html, read_size);
    free(html);
    
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "ERROR: Lexbor parse failed with status: %d\n", status);
        lxb_html_document_destroy(current_doc);
        return;
    }
    
    if(INFO_MESSAGES) {
        printf("HTML parsed successfully in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
    }
    
    // ========== STEP 1.5: Find and load external CSS ==========
    stopwatch_start(&step_timer);
    if(INFO_MESSAGES) printf("\n=== Loading External CSS ===\n");
    
    lxb_dom_document_t *css_dom_doc = lxb_dom_interface_document(current_doc);
    lxb_dom_element_t *css_doc_element = lxb_dom_document_element(css_dom_doc);
    
    char *css_base_url = NULL;
    if (g_current_base_url) {
        css_base_url = strdup(g_current_base_url);
    } else {
        css_base_url = strdup(filename);
        char *last_slash = strrchr(css_base_url, '/');
        if (last_slash) {
            *(last_slash + 1) = '\0';
        }
    }
    
    if (INFO_MESSAGES) printf("Base URL for CSS: %s\n", css_base_url);
    
    lxb_dom_collection_t *links = lxb_dom_collection_make(css_dom_doc, 50);
    if (links) {
        lxb_dom_elements_by_tag_name(css_doc_element, links, (lxb_char_t*)"link", 4);
        
        for (size_t i = 0; i < lxb_dom_collection_length(links); i++) {
            lxb_dom_element_t *link = lxb_dom_collection_element(links, i);
            
            size_t rel_len = 0;
            const lxb_char_t *rel_value = lxb_dom_element_get_attribute(link, (lxb_char_t*)"rel", 3, &rel_len);
            
            if (rel_value && rel_len > 0) {
                if (strncmp((char*)rel_value, "stylesheet", rel_len) == 0) {
                    size_t href_len = 0;
                    const lxb_char_t *href_value = lxb_dom_element_get_attribute(link, (lxb_char_t*)"href", 4, &href_len);
                    
                    if (href_value && href_len > 0) {
                        char *css_url = (char*)href_value;
                        printf("🔗 Found external stylesheet: %s\n", css_url);
                        fetch_and_parse_external_css(css_url, css_base_url);
                    }
                }
            }
        }
        lxb_dom_collection_destroy(links, true);
    }
    
    free(css_base_url);
    
    if(INFO_MESSAGES) {
        printf("External CSS loading completed in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
    }


//============================================
// ========== STEP 7: BUILD RENDERING OUTPUT (IN MEMORY) ==========
stopwatch_start(&step_timer);
if(INFO_MESSAGES) printf("\n=== STEP 7: Build Rendering Output (In-Memory) ===\n");

cJSON *rendering_output = cJSON_CreateArray();

// ===== STEP 7.1: PARSE CSS FIRST =====
if(INFO_MESSAGES) printf("\n=== STEP 7.1: Parse CSS from <style> tags ===\n");

if (!skip_css_parsing) {
    extract_and_parse_css_styles(current_doc, NULL);
    if(INFO_MESSAGES) printf("DEBUG: CSS rules parsed: %d\n", css_rule_count);
    for (int i = 0; i < css_rule_count && i < 5; i++) { }
    if (css_rule_count > 5) printf("  ... and %d more rules\n", css_rule_count - 5);
} else {
    printf("⏭️ Skipping CSS parsing due to site complexity\n");
}
 // ====================

// Get DOM document
lxb_dom_document_t *dom_doc = lxb_dom_interface_document(current_doc);
lxb_dom_element_t *doc_element = lxb_dom_document_element(dom_doc);

// Find body element
lxb_dom_element_t *body = NULL;
lxb_dom_collection_t *body_coll = lxb_dom_collection_make(dom_doc, 1);
if (body_coll) {
    lxb_dom_elements_by_tag_name(doc_element, body_coll, (lxb_char_t*)"body", 4);
    size_t body_count = lxb_dom_collection_length(body_coll);
    
    if (body_count > 0) {
        body = lxb_dom_collection_element(body_coll, 0);
        if(INFO_MESSAGES) printf("Found body element\n");
    }
    
    lxb_dom_collection_destroy(body_coll, true);
}

if (body) {
    cJSON *body_json = cJSON_CreateObject();
    if(INFO_MESSAGES) printf("Dodaje defaultse u body\n");
    cJSON_AddStringToObject(body_json, "type", "block");
    cJSON_AddStringToObject(body_json, "tag", "body");
    cJSON_AddStringToObject(body_json, "bg_color", "#ffffff");
    cJSON_AddNumberToObject(body_json, "x", 0);
    cJSON_AddNumberToObject(body_json, "y", 0);
    cJSON_AddStringToObject(body_json, "width", "800");
    cJSON_AddStringToObject(body_json, "height", "600");
    
    int body_id = g_next_element_id++;
    cJSON_AddNumberToObject(body_json, "id", body_id);
    cJSON_AddNumberToObject(body_json, "element_id", body_id);
    cJSON_AddNumberToObject(body_json, "parent_id", -1);
    if(INFO_MESSAGES) printf("zavrsio body defaultse.\n");
    
    lxb_dom_node_t *body_node = lxb_dom_interface_node(body);
    cJSON *children_array = cJSON_CreateArray();
    int child_count = 0;
    if(INFO_MESSAGES) printf("Ulazi u petlju\n");
    
    // ===== ADD COUNTER AND DETAILED LOGGING =====
    int processed_count = 0;
    int max_elements = 1000;  // Safe limit
    int problematic_found = 0;
    
    lxb_dom_node_t *child = lxb_dom_node_first_child(body_node);
    
    while (child && processed_count < max_elements && !problematic_found) {
        processed_count++;
        
        // ===== GET TAG NAME FOR DEBUGGING (every 50th element) =====
        if (INFO_MESSAGES && processed_count % 50 == 0) {
            const char *tag_name = "unknown";
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *elem = lxb_dom_interface_element(child);
                size_t tag_len;
                const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);
                if (tag && tag_len > 0) {
                    char tag_buf[64];
                    size_t copy_len = tag_len < 63 ? tag_len : 63;
                    memcpy(tag_buf, tag, copy_len);
                    tag_buf[copy_len] = '\0';
                    tag_name = tag_buf;
                }
            } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                tag_name = "#text";
            }
          printf("🔍 Processing child %d: <%s>\n", processed_count, tag_name);
        }
        
        // ===== CHECK FOR PROBLEMATIC ELEMENTS =====
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);
            
            if (tag && tag_len > 0) {
                char tag_lower[64];
                size_t copy_len = tag_len < 63 ? tag_len : 63;
                memcpy(tag_lower, tag, copy_len);
                tag_lower[copy_len] = '\0';
                
                // Convert to lowercase
                for (int i = 0; tag_lower[i]; i++) {
                    tag_lower[i] = tolower(tag_lower[i]);
                }
                
                // Skip known problematic elements
                if (strcmp(tag_lower, "svg") == 0) {
                    printf("⚠️ Found SVG at element %d - skipping (not supported)\n", processed_count);
                    child = lxb_dom_node_next(child);
                    continue;
                }
                if (strcmp(tag_lower, "iframe") == 0) {
                    printf("⚠️ Found IFRAME at element %d - skipping (not supported)\n", processed_count);
                    child = lxb_dom_node_next(child);
                    continue;
                }
                if (strcmp(tag_lower, "noscript") == 0) {
                    printf("⚠️ Found NOSCRIPT at element %d - skipping\n", processed_count);
                    child = lxb_dom_node_next(child);
                    continue;
                }
            }
        }
        
        // ===== PROCESS ELEMENT =====
        cJSON *child_json = procesuiraj_elemente(child, 0, body_json);
        
        if (child_json) {
            cJSON_AddItemToArray(children_array, child_json);
            child_count++;
        }
        
        child = lxb_dom_node_next(child);
    }
    
    if (processed_count >= max_elements) {
        printf("⚠️ Reached limit of %d elements, stopping to prevent hang\n", max_elements);
    }
    
    if(INFO_MESSAGES) printf("Processed %d body children for rendering (total processed: %d)\n", 
                              child_count, processed_count);
    
    if (child_count > 0) {
        cJSON_AddItemToObject(body_json, "children", children_array);
    } else {
        cJSON_Delete(children_array);
    }
    
    cJSON_AddItemToArray(rendering_output, body_json);
    global_computed_layout = rendering_output;
    
    add_attribute_to_children(rendering_output, "nav", "is_menu", "true", 2);
    global_document_json = rendering_output;
    // ===== Only apply CSS if not skipped =====
    if (!skip_css_application) {
        dodaj_css(rendering_output);
        if(INFO_MESSAGES) printf("CSS applied to rendering tree\n");
    } else {
        printf("⏭️ Skipping CSS application due to site complexity\n");
    }
    

} else {
    if(INFO_MESSAGES) printf("ERROR: No body element found\n");
    
    cJSON *empty_body = cJSON_CreateObject();
    cJSON_AddStringToObject(empty_body, "type", "block");
    cJSON_AddStringToObject(empty_body, "tag", "body");
    cJSON_AddStringToObject(empty_body, "bg_color", "#ffffff");
    cJSON_AddNumberToObject(empty_body, "x", 0);
    cJSON_AddNumberToObject(empty_body, "y", 0);
    cJSON_AddStringToObject(empty_body, "width", "800px");
    cJSON_AddStringToObject(empty_body, "height", "600px");
    cJSON_AddItemToArray(rendering_output, empty_body);
    
    int body_id = g_next_element_id++;
    cJSON_AddNumberToObject(empty_body, "id", body_id);
    cJSON_AddNumberToObject(empty_body, "element_id", body_id);
    cJSON_AddNumberToObject(empty_body, "parent_id", -1);
}

if(INFO_MESSAGES) {
    printf("In-memory rendering output built in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
}


    // ========== STEP 2.5: Initialize Document Outline ==========
    if(INFO_MESSAGES) printf("\n=== STEP 2.5: Initialize Document Outline ===\n");
    init_document_outline(&global_document_outline);
    if(INFO_MESSAGES) printf("Document outline tracker initialized\n");
    
    // ========== STEP 3: Initialize Event Handler ==========
    if(INFO_MESSAGES) printf("\n=== STEP 3: Initialize Event Handler ===\n");
    event_handler_init();
    if(INFO_MESSAGES) printf("Event handler initialized\n");
    
    // ========== STEP 4: Initialize JavaScript (Optional) ==========
    stopwatch_start(&step_timer);
    if(INFO_MESSAGES_JS) printf("\n=== STEP 4: Initialize JavaScript ===\n");
    
    cJSON *js_modifications = NULL;
    
    if (enable_js) {
        js_ctx = js_engine_init();
        if (js_ctx) {
            if(INFO_MESSAGES_JS) printf("JavaScript engine initialized\n");
            
            SecurityPolicy policy = js_default_security_policy();
            policy.max_memory_bytes = 8 * 1024 * 1024;
            policy.allow_dom_apis = 1;
            policy.enable_console = 1;
            js_set_security_policy(js_ctx, policy);
            
            js_execute_script_elements(js_ctx, current_doc);
        
                        // =========================================================================
            // 🔄 FIBRIL EVENT PUMP SHIELD (REŠENJE ZA ASINHRONE TAJMERE)
            // Prisiljava HelenOS da izvrši sve pokrenute Google tajmere i skripte
            // PRE nego što uzmemo modifikacije i završimo učitavanje stranice!
            // =========================================================================
            if(INFO_MESSAGES_JS) printf("⏳ [Event Pump] Pokrećem fibril pumpu za Google tajmere...\n");
            
            // Puštamo fibril procesor da se odmori i izvrši pozadinske niti.
            // Google tajmeri obično traže minimalno kašnjenje, pa je 50ms-100ms savršeno u QEMU.
            fibril_usleep(80000); // 80 milisekundi drži petlju otvorenom za tajmere
            
            // Ako tvoj QuickJS drajver ima funkciju za izvršavanje zaostalih JS taskova,
            // pozivamo je ovde (ekvivalent JS_ExecutePendingJob):
            JSContext *ctx_pump;
            while (JS_ExecutePendingJob(JS_GetRuntime(js_ctx), &ctx_pump) > 0) {
                // Pumpamo QuickJS poslove dokle god ih ima na steku
            }
            if(INFO_MESSAGES_JS) printf("✅ [Event Pump] Fibril tajmeri uspešno propumpani kroz QuickJS!\n");
            // =====
            
            js_modifications = get_js_modifications();
            if (js_modifications) {
                int mod_count = 0;
                cJSON *element;
                
                if(INFO_MESSAGES_JS) printf("JavaScript modifications:\n");
                cJSON_ArrayForEach(element, js_modifications) {
                    mod_count++;
                    if(INFO_MESSAGES_JS) printf("  - Element '%s' modified by JS\n", element->string);
                }
                
                if(INFO_MESSAGES_JS) printf("Total: %d modifications\n", mod_count);
            } else {
                if(INFO_MESSAGES_JS) printf("No JavaScript modifications detected\n");
            }
    // =========================================================================

            // =========================================================================
          
            if(INFO_MESSAGES_JS) printf("JavaScript execution completed\n");
        } else {
            printf("WARNING: JavaScript engine initialization failed (continuing without JS)\n");
        }
    } else {
        printf("JavaScript execution disabled\n");
    }
    
    if(INFO_MESSAGES_JS) {
        printf("JavaScript processing completed in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
    }
    
    // Mark DOM as loaded (content parsed, but not yet rendered)
    perf_timing_dom_loaded(&page_timing);
    printf("📄 DOM loaded at %" PRIu64 " ms (%" PRIu64 " ms elapsed)\n", 
           get_uptime_ms(), perf_timing_elapsed_ms(&page_timing, page_timing.dom_loaded));
  // Premesten obrisani deo ==================
      // ===== JavaScript modifications (only if not skipped) =====
      if (!skip_css_application) {
        if (js_modifications) {
            printf("🎨 Applying JavaScript modifications to DOM...\n");
            apply_js_modifications_to_dom(rendering_output, js_modifications);
            
            // 🚀 GEOMETRIJSKI FIX: Pronalazimo sivu kutiju ("result13")
            // i resetujemo joj fiksnu visinu pre STEP 8 layout kalkulacije
            cJSON *test13_box = find_element_by_string_id(rendering_output, "result13");
            if (test13_box) {
                cJSON_DeleteItemFromObject(test13_box, "height"); // Uklanjamo staru visinu
                set_json_bool(test13_box, "needs_layout", true);   // Forsiramo ponovni layout
            }
            
            dodaj_css(rendering_output);
            cJSON_Delete(js_modifications);
            js_modifications = NULL;
        }
    }  
    
// ========== STEP 6: Process Events ==========

if(INFO_MESSAGES) printf("\n=== STEP 6: Process Events ===\n");
event_handler_reset_limits();
lxb_dom_node_t *root = lxb_dom_interface_node(current_doc);
process_events_recursive(root);
int event_count = get_event_handler_count();
if(INFO_MESSAGES) printf("Found %d event handler(s)\n", event_count);

// ===== ADD THIS: Calculate site complexity =====
event_handler_calculate_complexity();

// ===== ADD THIS: Decide what to skip =====


if (g_site_complexity_score >= 8000) {
    printf("⚠️ High complexity site detected, skipping CSS entirely\n");
    skip_css_parsing = 1;
    skip_css_application = 1;
} else if (g_site_complexity_score >= 50) {
    printf("⚠️ Medium complexity site, skipping CSS parsing\n");
    skip_css_parsing = 1;
    skip_css_application = 0;
} else {
    printf("✅ Low complexity site, normal CSS processing\n");
}



// ========== STEP 8: CALCULATE LAYOUT ==========
    stopwatch_start(&step_timer);
    if(INFO_MESSAGES) printf("\n=== STEP 8: Calculate Layout ===\n");
    
    if (cJSON_GetArraySize(rendering_output) > 0) {
        cJSON *body_element = cJSON_GetArrayItem(rendering_output, 0);
        
        if (body_element) {
            cJSON *width_item = cJSON_GetObjectItem(body_element, "width");
            if (!width_item || (cJSON_IsString(width_item) && strcmp(width_item->valuestring, "auto") == 0)) {
                // 🚀 USE IT DIRECTLY (Do not put 'int' here!)
                cJSON_ReplaceItemInObject(body_element, "width", cJSON_CreateNumber(viewport_width));
                if(INFO_MESSAGES) printf("📐 Body width set to viewport: %d\n", viewport_width);
            } else if (cJSON_IsNumber(width_item)) {
                if(INFO_MESSAGES) printf("📐 Body width from CSS: %d\n", width_item->valueint);
            }
            
            prisilno_resetuj_layout_markere(body_element);
            layout_document(body_element, global_pauk_ui, &global_pauk_ui->font_manager);
        }
    } else {
        printf("ERROR: rendering_output is empty!\n");
    }
    
    if(INFO_MESSAGES) {
        printf("Layout calculation completed in %" PRIu64 " ms\n", stopwatch_elapsed_ms(&step_timer));
    }
    
    // Mark first paint (initial layout done)
    perf_timing_first_paint(&page_timing);
    printf("🎨 First paint at %" PRIu64 " ms (%" PRIu64 " ms elapsed)\n", 
           get_uptime_ms(), perf_timing_elapsed_ms(&page_timing, page_timing.first_paint));
    
    // STEP 8.3: Post-processing fixes
    if(rendering_output){
        fix_media_link_alignment(rendering_output);
    }
    
    // STEP 8.4: Calculate document dimensions for scrolling
    if (rendering_output && cJSON_IsArray(rendering_output)) {
        for (int i = 0; i < cJSON_GetArraySize(rendering_output); i++) {
            cJSON *elem = cJSON_GetArrayItem(rendering_output, i);
            if (strcmp(get_json_string(elem, "tag", ""), "body") == 0) {
                int body_width = get_json_number(elem, "width", 800);
                int content_width = find_document_right(rendering_output, viewport_width);
                int doc_height = find_document_bottom(rendering_output) + 50;
                
                cJSON_ReplaceItemInObject(elem, "height", cJSON_CreateNumber(doc_height));
                
                int scroll_width = (content_width > body_width) ? content_width : body_width;
                global_pauk_ui->content_width = scroll_width;
                
                if(INFO_MESSAGES) {
                    printf("📐 Document dimensions: body %d x %d, content width: %d\n", 
                           body_width, doc_height, content_width);
                }
                break;
            }
        }
    }
    
    global_pauk_ui->rendering_json = rendering_output;
    global_pauk_ui->current_json = rendering_output;
    
    test_text_rendering(global_pauk_ui);
    update_scrollbar_ratio(global_pauk_ui);
    
    pixelmap_to_bitmap_copy(global_pauk_ui);
    gfx_bitmap_render(global_pauk_ui->html_renderer->content_bitmap, 
                      &global_pauk_ui->list_rect, NULL);
    gfx_update(global_pauk_ui->gc);
    
    global_pauk_ui->current_hover = NULL;
    //close_keepalive_connection();
/// ===== LOAD ALL IMAGES INTO QUEUE =====
// This just collects ALL images from the DOM (with duplicates)
if (get_queue_count() > 0) {
  //  printf("📸 Found %d raw images on the page\n", get_queue_count());
    
    // Build ImageList with ALL images (including duplicates)
    ImageList all_images;
    all_images.count = 0;
    all_images.urls = malloc(get_queue_count() * sizeof(char*));
    all_images.elements = malloc(get_queue_count() * sizeof(cJSON*));
    
    if (!all_images.urls || !all_images.elements) {
        printf("❌ Failed to allocate memory for image list\n");
        free(all_images.urls);
        free(all_images.elements);
        return;
    }
    
    // Copy ALL images from queue to ImageList
    for (int i = 0; i < get_queue_count(); i++) {
        if (queue[i].status == 0) {
            all_images.urls[all_images.count] = strdup(queue[i].original_url);
            all_images.elements[all_images.count] = queue[i].element;
            all_images.count++;
        }
    }
    
    //printf("📸 Loaded %d images into catalog\n", all_images.count);
  
   // printf("🔌 Main connection closed before image downloads\n");
    // ===== NOW DEDUPLICATE THE CATALOG =====
    // This happens AFTER all images are loaded
    ImageList unique_images;
    unique_images.count = 0;
    unique_images.urls = malloc(all_images.count * sizeof(char*));
    unique_images.elements = malloc(all_images.count * sizeof(cJSON*));
    
    if (!unique_images.urls || !unique_images.elements) {
        printf("❌ Failed to allocate memory for deduplication\n");
        for (int i = 0; i < all_images.count; i++) {
            free(all_images.urls[i]);
        }
        free(all_images.urls);
        free(all_images.elements);
        return;
    }
    
    // Deduplicate the catalog
    for (int i = 0; i < all_images.count; i++) {
        int is_duplicate = 0;
        for (int j = 0; j < unique_images.count; j++) {
            if (strcmp(all_images.urls[i], unique_images.urls[j]) == 0) {
                is_duplicate = 1;
             //   printf("⏭️ Skipping duplicate: %s\n", all_images.urls[i]);
                break;
            }
        }
        if (!is_duplicate) {
            unique_images.urls[unique_images.count] = strdup(all_images.urls[i]);
            unique_images.elements[unique_images.count] = all_images.elements[i];
            unique_images.count++;
        }
    }
    
   // printf("📸 After deduplication: %d unique images\n", unique_images.count);
    
    // Free all_images (no longer needed)
    for (int i = 0; i < all_images.count; i++) {
        free(all_images.urls[i]);
    }
    free(all_images.urls);
    free(all_images.elements);
    
     // ===== PROCESS UNIQUE IMAGES =====
     if (unique_images.count > 0) {
        // Clean up any previous queue
        cleanup_download_queue();
        
        // Initialize the queue
        init_download_queue();
        
        // Add unique images to queue
        add_images_to_queue(&unique_images);
        
        // Process the queue (manages MAX_PARALLEL)
        process_download_queue(global_pauk_ui);  // Changed: was process_download_queue_ipc
    } else {
        printf("📥 No unique images to download\n");
    }
    
    // Clean up unique_images
    for (int i = 0; i < unique_images.count; i++) {
        free(unique_images.urls[i]);
    }
    free(unique_images.urls);
    free(unique_images.elements);
    
    // Clear the original queue
    for (int i = 0; i < queue_count; i++) {
        free(queue[i].url);
        free(queue[i].original_url);
    }
    queue_count = 0;


}
// ================================

    // Mark page fully loaded
    perf_timing_page_loaded(&page_timing);
    printf("✅ Page fully loaded at %" PRIu64 " ms (%" PRIu64 " ms total)\n", 
           get_uptime_ms(), perf_timing_elapsed_ms(&page_timing, page_timing.page_loaded));
    
    // Print complete performance summary
    perf_timing_print(&page_timing);
    
    // Optional: Update status bar with load time
    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg), "Loaded in %" PRIu64 " ms", 
             perf_timing_elapsed_ms(&page_timing, page_timing.page_loaded));
             show_status_message(global_pauk_ui, status_msg, 3000);
    
    hover_timer_running = 1;
    if(hover_timer_running) {
        start_hover_timer(global_pauk_ui, global_pauk_ui->hover_interval);
    }
    global_pauk_ui->navigating = 0;
    void close_keepalive_connection(void);
  //  
}


// Recursively adjust positions of all children and grandchildren
void adjust_child_positions(cJSON *children, int y_offset) {
    if (!children || !cJSON_IsArray(children)) return;
    
    cJSON *child;
    cJSON_ArrayForEach(child, children) {
        // Update this child's Y position
        int current_y = get_json_number(child, "y", 0);
        cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(current_y + y_offset));
        
        // Recurse into this child's children
        cJSON *grandchildren = cJSON_GetObjectItem(child, "children");
        if (grandchildren && cJSON_IsArray(grandchildren)) {
            adjust_child_positions(grandchildren, y_offset);
        }
    }
}

void fix_media_link_alignment(cJSON *root) {
    if (!root) return;
    
    // Handle arrays
    if (cJSON_IsArray(root)) {
        for (int i = 0; i < cJSON_GetArraySize(root); i++) {
            cJSON *elem = cJSON_GetArrayItem(root, i);
            fix_media_link_alignment(elem);  // Recurse into each element
        }
        return;
    }
    
    // Handle objects
    if (cJSON_IsObject(root)) {
        // Check if this element has children
        cJSON *children = cJSON_GetObjectItem(root, "children");
        if (children && cJSON_IsArray(children)) {
            // First, look for media and link in this level's children
            cJSON *media = NULL;
            cJSON *link = NULL;
            int media_y = 0, media_h = 0, link_y = 0;
            
            for (int i = 0; i < cJSON_GetArraySize(children); i++) {
                cJSON *child = cJSON_GetArrayItem(children, i);
                
                int is_media = get_json_bool(child, "is_image", 0) ||
                               get_json_bool(child, "is_video", 0) ||
                               get_json_bool(child, "is_table", 0) ||
                               get_json_bool(child, "is_canvas", 0) ||
                               get_json_bool(child, "is_iframe", 0) ||
                               get_json_bool(child, "is_svg", 0);
                
                int is_link = get_json_bool(child, "is_link", 0);
                
                if (is_media) {
                    media = child;
                    media_y = get_json_number(child, "y", 0);
                    media_h = get_json_number(child, "height", 0);
                }
                if (is_link) {
                    link = child;
                    link_y = get_json_number(child, "y", 0);
                }
            }
            
            // If found media and link at same Y, fix them
            if (media && link && media_y == link_y) {
                int media_bottom = media_y + media_h;
                
                cJSON *link_children = cJSON_GetObjectItem(link, "children");
                int font_size = 16;
                int text_offset = 0;
                
                if (link_children && cJSON_IsArray(link_children) && cJSON_GetArraySize(link_children) > 0) {
                    cJSON *text_node = cJSON_GetArrayItem(link_children, 0);
                    font_size = get_json_number(text_node, "font_size", 16);
                    int text_y = get_json_number(text_node, "y", 0);
                    text_offset = text_y - link_y;
                }
                
                int new_text_y = media_bottom - font_size;
                int new_link_y = new_text_y - text_offset;
                
                set_json_number(link, "y", new_link_y);
                
                if (link_children && cJSON_IsArray(link_children)) {
                    int offset = new_link_y - link_y;
                    adjust_child_positions(link_children, offset);
                    
                    if (cJSON_GetArraySize(link_children) > 0) {
                        cJSON *text_node = cJSON_GetArrayItem(link_children, 0);
                        int current_text_y = get_json_number(text_node, "y", 0);
                        if (current_text_y != new_text_y) {
                            set_json_number(text_node, "y", new_text_y);
                        }
                    }
                }

            }
            
            // Recurse into children for deeper nesting
            for (int i = 0; i < cJSON_GetArraySize(children); i++) {
                cJSON *child = cJSON_GetArrayItem(children, i);
                fix_media_link_alignment(child);
            }
        }
    }
}



int main(int argc, char *argv[]) {
    if(INFO_MESSAGES) printf("=== SRBINOS BROWSER ===\n");
    
    // Initialize UI
    pauk_ui_t pauk_ui;
    
    pauk_ui.mouse_pos.x = 1;
    pauk_ui.mouse_pos.y = 2;
    pauk_ui.prev_pos.x = -1; 
    pauk_ui.prev_pos.y = -1;
    pauk_ui.cursor_move = 0;
    pauk_ui.hover = false;
    pauk_ui.current_json = NULL; 
    pauk_ui.current_hover = NULL;  
    pauk_ui.hover_interval = 500;
    pauk_ui.scroll_step = 40;
    pauk_ui.page_step = 200;
    pauk_ui.content_height = 0;
    pauk_ui.hover_timer = NULL;  // Timer not started yet

    cJSON *js_modifications = NULL;

    pauk_ui.rendering_json = NULL;
    pauk_ui.focused_element = NULL;

    // HTML INIT START
font_manager_init(&pauk_ui.font_manager);
font_manager_load_fonts(&pauk_ui.font_manager, "/data/font/");

font_manager_init_substitutions(&pauk_ui.font_manager);
init_image_cache(); 
init_image_downloader();

errno_t rc = init_ui(&pauk_ui, UI_ANY_DEFAULT);
    if (rc != EOK) {
        fprintf(stderr, "Failed to initialize UI: %s\n", str_error(rc));
        return 1;
    }
    global_pauk_ui = &pauk_ui;
    if (argc >= 2) {
        const char *input = argv[1];
        size_t len = strlen(input);
        
        // Check if it's an HTML file
        if (len > 5 && (strcmp(input + len - 5, ".html") == 0 ||
                        strcmp(input + len - 4, ".htm") == 0)) {
            // Valid HTML file - show in address bar
            ui_entry_set_text(pauk_ui.address_entry, input);
        } else {
            // Not HTML - show error or just display as is
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Not HTML: %s", input);
            ui_entry_set_text(pauk_ui.address_entry, error_msg);
        }
    } else {
        // Empty address bar
        ui_entry_set_text(pauk_ui.address_entry, "");
    }

                // Initialize bookmark file FIRST
                 rc = init_bookmark_file();
                if (rc != EOK) {
                    fprintf(stderr, "Warning: Failed to initialize bookmark file: %s\n", str_error(rc));
                    // Continue anyway
                }

ui_run(pauk_ui.ui);


// ==========  CLEANUP ==========
if(INFO_MESSAGES) printf("\n=== Cleanup ===\n");

// Clean up rendering output
if(rendering_output){
cJSON_Delete(rendering_output);
}

// Clean up JavaScript modifications if they exist
if (js_modifications) {
    cJSON_Delete(js_modifications);
    js_modifications = NULL;
}

// Clean up JavaScript context if it was created
if (js_ctx) {
    js_engine_cleanup(js_ctx);
    js_ctx = NULL;
}

// Clean up CSS parser
css_parser_cleanup();

// Clean up event handler
event_handler_cleanup();

//ocisti stgatusbar tajmer 
if (pauk_ui.status_timer) {
    fibril_timer_clear(pauk_ui.status_timer);
}

// Clean up computed layout (if you still have this variable somewhere)
if (global_computed_layout) {
    cJSON_Delete(global_computed_layout);
    global_computed_layout = NULL;
}

// Destroy Lexbor document
if(current_doc){
lxb_html_document_destroy(current_doc);
}
if(INFO_MESSAGES) printf("Cleanup completed successfully.\n");
if(INFO_MESSAGES) printf("\n=== HTML RENDERER FINISHED ===\n");
// ========== END STEP 10 ==========


    
    return 0;
}

