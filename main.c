#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include "cjson.h"
#include "css_parser.h"
#include "js_executor_quickjs.h"
#include "layout_engine.h"
#include "forms_parser.h"
#include "tables_parser.h"
#include "lists_parser.h"
#include "menus_parser.h"
#include "img_parser.h"
#include "media_parser.h"
#include "link_parsing.h"
#include "headings_parser.h"
#include "iframe_parsing.h"
#include "layout_calculator.h"
#include "render_output.h"
#include "lua_position.h"


static cJSON *global_computed_layout = NULL;
static DocumentOutline global_document_outline;

// Simplified element structure for rendering
typedef struct {
    const char *tag;
    const char *type;      // "block", "inline", "image", "table_ref", "form_ref", "menu_ref"
    const char *text;
    const char *id;
    const char *bg_color;
    const char *color;
    const char *font_style;
    const char *style;
    const char *href;
    const char *src;
    int font_size;
    int x;
    int y;
    const char *width;
    const char *height;
} RenderingElement;

//kopiraj fajl
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

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
    
    printf("Copying %s -> %s\n", src_file, dst_path);
    
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
//ZA ATRIBUTE
static void convert_dash_to_camel(const char *dash_str, char *camel_str, size_t max_len) {
    if (!dash_str || !camel_str || max_len == 0) {
        if (camel_str && max_len > 0) camel_str[0] = '\0';
        return;
    }
    
    size_t i = 0, j = 0;
    int next_upper = 0;
    
    while (dash_str[i] != '\0' && j < max_len - 1) {
        if (dash_str[i] == '-') {
            next_upper = 1;
        } else {
            if (next_upper) {
                camel_str[j++] = toupper(dash_str[i]);
                next_upper = 0;
            } else {
                camel_str[j++] = dash_str[i];
            }
        }
        i++;
    }
    
    camel_str[j] = '\0';
}

// Convert Lexbor element to simple rendering JSON
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
            "span", "a", "strong", "em", "i", "b", "u", "code",
            "mark", "small", "sub", "sup", "abbr", "cite",
            "label", "q", "s", NULL
        };
        
        for (int i = 0; inline_tags[i] != NULL; i++) {
            if (strcasecmp(tag, inline_tags[i]) == 0) {
                type = "inline";
                break;
            }
        }
        
        // Special element types
        if (strcasecmp(tag, "img") == 0) type = "image";
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
        while (*start && isspace(*start)) start++;
        
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) {
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
    
    // Check if this is an inline container (don't get text from children)
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
    char *tag = NULL;
    if (tag_name && tag_len > 0) {
        tag = malloc(tag_len + 1);
        memcpy(tag, tag_name, tag_len);
        tag[tag_len] = '\0';
        
        // Skip elements that contain other elements
        const char *container_tags[] = {
            "div", "p", "h1", "h2", "h3", "h4", "h5", "h6",
            "ul", "ol", /* "li", */ "table", "form", "section", // REMOVED "li" from here
            "article", "aside", "nav", "header", "footer",
            "main", "figure", "blockquote", "pre", NULL
        };
        
        for (int i = 0; container_tags[i] != NULL; i++) {
            if (strcasecmp(tag, container_tags[i]) == 0) {
                free(tag);
                return strdup(""); // Container elements have text in children
            }
        }
        free(tag);
    }
    
    // Get direct text content
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
        if (isspace(*src)) {
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
    while (*start && isspace(*start)) start++;
    
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

// Process node recursively for rendering
cJSON* process_node_for_rendering(lxb_dom_node_t *node, int depth) {
    if (!node || depth > 20) return NULL; // Safety limit
    
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        
        // Skip non-rendering elements
        size_t tag_len;
        const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
        char *tag = NULL;
        if (tag_name && tag_len > 0) {
            tag = malloc(tag_len + 1);
            memcpy(tag, tag_name, tag_len);
            tag[tag_len] = '\0';
            
            // Skip these elements entirely
            const char *skip_tags[] = {
                "script", "style", "meta", "link", "title",
                "head", "html", "!doctype", NULL
            };
            
            for (int i = 0; skip_tags[i] != NULL; i++) {
                if (strcasecmp(tag, skip_tags[i]) == 0) {
                    free(tag);
                    return NULL;
                }
            }
            
            free(tag);
        }
        
        // Convert element to rendering JSON
        cJSON *elem_json = element_to_rendering_json(elem, 0);
        if (!elem_json) return NULL;
        
        // Process children
        cJSON *children_array = cJSON_CreateArray();
        int child_count = 0;
        
        lxb_dom_node_t *child = lxb_dom_node_first_child(node);
        while (child) {
            cJSON *child_json = process_node_for_rendering(child, depth + 1);
            if (child_json) {
                cJSON_AddItemToArray(children_array, child_json);
                child_count++;
            }
            child = lxb_dom_node_next(child);
        }
        
        // Only add children array if there are children
        if (child_count > 0) {
            cJSON_AddItemToObject(elem_json, "children", children_array);
        } else {
            cJSON_Delete(children_array);
        }
        
        return elem_json;
    }
    else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        const lxb_char_t *text = lxb_dom_node_text_content(node, NULL);
        if (text) {
            // Check if text has actual content (not just whitespace)
            const lxb_char_t *p = text;
            int has_content = 0;
            while (*p != '\0') {
                if (!isspace(*p)) {
                    has_content = 1;
                    break;
                }
                p++;
            }
            
            if (has_content) {
                cJSON *text_json = cJSON_CreateObject();
                cJSON_AddStringToObject(text_json, "type", "text");
                
                char *text_str = lexbor_to_cstr(text, strlen((char*)text));
                if (text_str) {
                    // Trim
                    char *start = text_str;
                    while (*start && isspace(*start)) start++;
                    
                    char *end = start + strlen(start) - 1;
                    while (end > start && isspace(*end)) {
                        *end = '\0';
                        end--;
                    }
                    
                    if (strlen(start) > 0) {
                        cJSON_AddStringToObject(text_json, "content", start);
                        cJSON_AddNumberToObject(text_json, "length", strlen(start));
                    }
                    
                    free(text_str);
                    
                    if (cJSON_HasObjectItem(text_json, "content")) {
                        return text_json;
                    }
                }
                
                cJSON_Delete(text_json);
            }
        }
    }
    
    return NULL;
}

// Main function to generate rendering output
int generate_rendering_output(const char *html_file, const char *output_file) {
    printf("Generating rendering output: %s -> %s\n", html_file, output_file);
    
    // Parse HTML
    FILE *f = fopen(html_file, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open HTML file: %s\n", html_file);
        return 0;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *html = malloc(size + 1);
    if (!html) {
        fclose(f);
        fprintf(stderr, "ERROR: Failed to allocate memory for HTML\n");
        return 0;
    }
    
    size_t read_size = fread(html, 1, size, f);
    html[read_size] = '\0';
    fclose(f);
    
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) {
        free(html);
        fprintf(stderr, "ERROR: Failed to create Lexbor document\n");
        return 0;
    }
    
    lxb_status_t status = lxb_html_document_parse(doc, (lxb_char_t*)html, read_size);
    free(html);
    
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "ERROR: Lexbor parse failed\n");
        lxb_html_document_destroy(doc);
        return 0;
    }
    
    // Create root rendering array
    cJSON *rendering_root = cJSON_CreateArray();
    
    // Find body element
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc);
    lxb_dom_element_t *doc_element = lxb_dom_document_element(dom_doc);
    
    lxb_dom_collection_t *body_coll = lxb_dom_collection_make(dom_doc, 1);
    if (body_coll) {
        lxb_dom_elements_by_tag_name(doc_element, body_coll, (lxb_char_t*)"body", 4);
        size_t body_count = lxb_dom_collection_length(body_coll);
        
        if (body_count > 0) {
            lxb_dom_element_t *body = lxb_dom_collection_element(body_coll, 0);
            
            // Create body wrapper
            cJSON *body_wrapper = cJSON_CreateObject();
            cJSON_AddStringToObject(body_wrapper, "type", "block");
            cJSON_AddStringToObject(body_wrapper, "tag", "body");
            cJSON_AddStringToObject(body_wrapper, "bg_color", "#ffffff");
            cJSON_AddNumberToObject(body_wrapper, "x", 0);
            cJSON_AddNumberToObject(body_wrapper, "y", 0);
            cJSON_AddStringToObject(body_wrapper, "width", "100%");
            cJSON_AddStringToObject(body_wrapper, "height", "auto");
            
            // Process body children
            cJSON *body_children = cJSON_CreateArray();
            lxb_dom_node_t *body_node = lxb_dom_interface_node(body);
            lxb_dom_node_t *child = lxb_dom_node_first_child(body_node);
            
            while (child) {
                cJSON *child_json = process_node_for_rendering(child, 0);
                if (child_json) {
                    cJSON_AddItemToArray(body_children, child_json);
                }
                child = lxb_dom_node_next(child);
            }
            
            cJSON_AddItemToObject(body_wrapper, "children", body_children);
            cJSON_AddItemToArray(rendering_root, body_wrapper);
        }
        
        lxb_dom_collection_destroy(body_coll, true);
    }
    
    // Write output
    FILE *out_file = fopen(output_file, "wb");
    if (out_file) {
        char *json_str = cJSON_Print(rendering_root);
        if (json_str) {
            fwrite(json_str, 1, strlen(json_str), out_file);
            free(json_str);
        }
        fclose(out_file);
        printf("Rendering output written to: %s\n", output_file);
        kopiraj_fajl(output_file);
    } else {
        printf("ERROR: Could not open output file\n");
    }
    
    // Cleanup
    cJSON_Delete(rendering_root);
    lxb_html_document_destroy(doc);
    
    return 1;
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


cJSON* process_element_for_rendering(lxb_dom_node_t *node, int depth) {
    if (!node || depth > 20) return NULL;
    
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        
        // Skip non-rendering elements
        size_t tag_len;
        const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
        if (!tag_name || tag_len == 0) return NULL;
        
        char *tag = malloc(tag_len + 1);
        memcpy(tag, tag_name, tag_len);
        tag[tag_len] = '\0';
        
        // Skip these elements entirely
        const char *skip_tags[] = {
            "script", "style", "meta", "link", "title",
            "head", "html", "!doctype", "noscript", "template",
            NULL
        };
        
        for (int i = 0; skip_tags[i] != NULL; i++) {
            if (strcasecmp(tag, skip_tags[i]) == 0) {
                free(tag);
                return NULL;
            }
        }
        
        // Create element JSON with ALL properties (default values first)
        cJSON *elem_json = cJSON_CreateObject();
        
        // ========== ALWAYS SET THESE PROPERTIES (with defaults) ==========
        // DEFAULT ZA ATRIBUTE
        cJSON_AddNumberToObject(elem_json, "data_attribute_count", 0);

        // 1. Basic properties
        cJSON_AddStringToObject(elem_json, "tag", tag);
        cJSON_AddStringToObject(elem_json, "type", "block"); // default
        
        // 2. Text properties
        cJSON_AddStringToObject(elem_json, "text", "");
        cJSON_AddStringToObject(elem_json, "font_family", "Arial");
        cJSON_AddNumberToObject(elem_json, "font_size", 16);
        cJSON_AddStringToObject(elem_json, "font_style", "normal"); // normal, italic, oblique
        cJSON_AddStringToObject(elem_json, "font_weight", "normal"); // normal, bold, 100-900
        cJSON_AddStringToObject(elem_json, "text_align", "left"); // left, center, right, justify
        cJSON_AddStringToObject(elem_json, "text_decoration", "none"); // none, underline, line-through
        cJSON_AddStringToObject(elem_json, "text_transform", "none"); // none, uppercase, lowercase, capitalize
        
        // 3. Color properties
        cJSON_AddStringToObject(elem_json, "color", "#000000"); // black default
        cJSON_AddStringToObject(elem_json, "bg_color", "#FFFFFF"); // white default
        cJSON_AddStringToObject(elem_json, "border_color", "#000000");
        
        // 4. Layout properties
        cJSON_AddNumberToObject(elem_json, "x", 0);
        cJSON_AddNumberToObject(elem_json, "y", 0);
        cJSON_AddStringToObject(elem_json, "width", "auto");
        cJSON_AddStringToObject(elem_json, "height", "auto");
        cJSON_AddNumberToObject(elem_json, "z_index", 0);
        cJSON_AddStringToObject(elem_json, "position", "static"); // static, relative, absolute, fixed
        cJSON_AddStringToObject(elem_json, "display", "block"); // block, inline, inline-block, none
        cJSON_AddStringToObject(elem_json, "visibility", "visible"); // visible, hidden
        cJSON_AddBoolToObject(elem_json, "visible", true);
        
        // 5. Margin and padding (all defaults to 0)
        cJSON_AddNumberToObject(elem_json, "margin_top", 0);
        cJSON_AddNumberToObject(elem_json, "margin_right", 0);
        cJSON_AddNumberToObject(elem_json, "margin_bottom", 0);
        cJSON_AddNumberToObject(elem_json, "margin_left", 0);
        cJSON_AddNumberToObject(elem_json, "padding_top", 0);
        cJSON_AddNumberToObject(elem_json, "padding_right", 0);
        cJSON_AddNumberToObject(elem_json, "padding_bottom", 0);
        cJSON_AddNumberToObject(elem_json, "padding_left", 0);
        
        // 6. Border properties
        cJSON_AddNumberToObject(elem_json, "border_width", 0);
        cJSON_AddStringToObject(elem_json, "border_style", "none"); // none, solid, dashed, dotted
        cJSON_AddNumberToObject(elem_json, "border_radius", 0);
        
        // 7. Content properties
        cJSON_AddStringToObject(elem_json, "src", ""); // for images
        cJSON_AddStringToObject(elem_json, "href", ""); // for links
        cJSON_AddStringToObject(elem_json, "alt", ""); // alt text
        cJSON_AddStringToObject(elem_json, "title", ""); // tooltip
        cJSON_AddStringToObject(elem_json, "list_file", ""); //for list references


        // 8. Boolean flags (all false by default)
        cJSON_AddBoolToObject(elem_json, "is_table", false);
        cJSON_AddBoolToObject(elem_json, "is_form", false);
        cJSON_AddBoolToObject(elem_json, "is_menu", false);
        cJSON_AddBoolToObject(elem_json, "is_image", false);
        cJSON_AddBoolToObject(elem_json, "is_link", false);
        cJSON_AddBoolToObject(elem_json, "is_button", false);
        cJSON_AddBoolToObject(elem_json, "is_input", false);
        cJSON_AddBoolToObject(elem_json, "is_list", false);
        cJSON_AddBoolToObject(elem_json, "is_list_item", false);
        cJSON_AddBoolToObject(elem_json, "is_heading", false);
        cJSON_AddBoolToObject(elem_json, "is_paragraph", false);
        cJSON_AddBoolToObject(elem_json, "is_inline", false);
        cJSON_AddBoolToObject(elem_json, "is_block", false);
        cJSON_AddBoolToObject(elem_json, "has_layout", false);
        cJSON_AddBoolToObject(elem_json, "has_focus", false);
        cJSON_AddBoolToObject(elem_json, "is_clickable", false);
        cJSON_AddBoolToObject(elem_json, "is_editable", false);
        cJSON_AddBoolToObject(elem_json, "is_selectable", false);

        // ⬇️⬇️⬇️  SEMANTIC FLAGS  ⬇️⬇️⬇️
cJSON_AddBoolToObject(elem_json, "is_semantic", false);
cJSON_AddBoolToObject(elem_json, "is_header", false);
cJSON_AddBoolToObject(elem_json, "is_footer", false);
cJSON_AddBoolToObject(elem_json, "is_section", false);
cJSON_AddBoolToObject(elem_json, "is_article", false);
cJSON_AddBoolToObject(elem_json, "is_aside", false);
cJSON_AddBoolToObject(elem_json, "is_main", false);
cJSON_AddBoolToObject(elem_json, "is_nav", false);
cJSON_AddBoolToObject(elem_json, "is_figure", false);
cJSON_AddBoolToObject(elem_json, "is_figcaption", false);
cJSON_AddBoolToObject(elem_json, "is_time", false);
cJSON_AddBoolToObject(elem_json, "is_mark", false);
cJSON_AddBoolToObject(elem_json, "is_summary", false);
cJSON_AddBoolToObject(elem_json, "is_details", false);
cJSON_AddBoolToObject(elem_json, "is_dialog", false);
cJSON_AddBoolToObject(elem_json, "is_meter", false);
cJSON_AddBoolToObject(elem_json, "is_progress", false);
cJSON_AddBoolToObject(elem_json, "is_output", false);
cJSON_AddBoolToObject(elem_json, "is_data", false);
// ⬆️⬆️⬆️ END ADDED FLAGS ⬆️⬆️⬆️

// ⬆️⬆️⬆️ AUDIO/VIDEO/CANVAS ADDED FLAGS ⬆️⬆️⬆️
cJSON_AddBoolToObject(elem_json, "is_media", false);
cJSON_AddStringToObject(elem_json, "media_type", "");
cJSON_AddBoolToObject(elem_json, "is_audio", false);
cJSON_AddBoolToObject(elem_json, "is_video", false);
cJSON_AddBoolToObject(elem_json, "is_canvas", false);
cJSON_AddBoolToObject(elem_json, "is_iframe", false);
// ⬆️⬆️⬆️ END AUDIO/VIDEO/CANVAS ADDED FLAGS ⬆️⬆️⬆️

// ⬆️⬆️⬆️ IFRAME FLAGS ⬆️⬆️⬆️
//cJSON_AddBoolToObject(elem_json, "is_iframe", false);  // ← ADD THIS
cJSON_AddBoolToObject(elem_json, "fully_sandboxed", false);
cJSON_AddBoolToObject(elem_json, "allowfullscreen", false);
cJSON_AddBoolToObject(elem_json, "allowpaymentrequest", false);
// ⬆️⬆️⬆️ KRAJ IFRAME FLAGS ⬆️⬆️⬆️

// ⬆️⬆️⬆️ DETAILS FLAGS ⬆️⬆️⬆️
cJSON_AddBoolToObject(elem_json, "is_details", false);  // ← ADD
cJSON_AddBoolToObject(elem_json, "is_summary", false);  // ← ADD
cJSON_AddBoolToObject(elem_json, "is_details_summary", false);  // ← ADD
cJSON_AddBoolToObject(elem_json, "details_open", false);  // ← ADD
cJSON_AddBoolToObject(elem_json, "is_expanded", false);  // ← ADD
cJSON_AddBoolToObject(elem_json, "has_default_summary", false);  // ← ADD
   // ⬆️⬆️⬆️ KRAJ DETAILS FLAGS ⬆️⬆️⬆️     



        // 9. Element ID and classes
        cJSON_AddStringToObject(elem_json, "id", "");
        cJSON *classes_array = cJSON_CreateArray();
        cJSON_AddItemToObject(elem_json, "classes", classes_array);
        cJSON_AddStringToObject(elem_json, "class_string", "");
        
        // ========== NOW OVERRIDE DEFAULTS WITH ACTUAL VALUES ==========
        
        // Determine element type based on tag
        const char *elem_type = "block";
        
        // Check for specific element types and set flags
        if (strcasecmp(tag, "img") == 0 || strcasecmp(tag, "image") == 0) {
            elem_type = "image";
            cJSON_ReplaceItemInObject(elem_json, "is_image", cJSON_CreateBool(true));
            cJSON_ReplaceItemInObject(elem_json, "display", cJSON_CreateString("inline-block"));
            
            // ========== ENHANCED IMAGE ATTRIBUTE EXTRACTION ==========
            
            // Get all important image attributes
            size_t attr_len;
            
            // 1. src (already have, but ensure it's properly extracted)
            const lxb_char_t *src = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"src", 3, &attr_len);
            if (src && attr_len > 0) {
                char *src_str = malloc(attr_len + 1);
                memcpy(src_str, src, attr_len);
                src_str[attr_len] = '\0';
                cJSON_ReplaceItemInObject(elem_json, "src", cJSON_CreateString(src_str));
                free(src_str);
            }
            
            // 2. alt (already have)
            const lxb_char_t *alt = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"alt", 3, &attr_len);
            if (alt && attr_len > 0) {
                char *alt_str = malloc(attr_len + 1);
                memcpy(alt_str, alt, attr_len);
                alt_str[attr_len] = '\0';
                cJSON_ReplaceItemInObject(elem_json, "alt", cJSON_CreateString(alt_str));
                free(alt_str);
            }
            
            // 3. width attribute (NOT CSS width)
            const lxb_char_t *width_attr = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"width", 5, &attr_len);
            if (width_attr && attr_len > 0) {
                char *width_str = malloc(attr_len + 1);
                memcpy(width_str, width_attr, attr_len);
                width_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "attr_width", width_str);  // Different key from CSS width
                free(width_str);
            }
            
            // 4. height attribute (NOT CSS height)
            const lxb_char_t *height_attr = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"height", 6, &attr_len);
            if (height_attr && attr_len > 0) {
                char *height_str = malloc(attr_len + 1);
                memcpy(height_str, height_attr, attr_len);
                height_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "attr_height", height_str);
                free(height_str);
            }
            
            // 5. loading attribute (lazy, eager, auto)
            const lxb_char_t *loading = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"loading", 7, &attr_len);
            if (loading && attr_len > 0) {
                char *loading_str = malloc(attr_len + 1);
                memcpy(loading_str, loading, attr_len);
                loading_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "loading", loading_str);
                free(loading_str);
            } else {
                cJSON_AddStringToObject(elem_json, "loading", "eager"); // default
            }
            
            // 6. decoding attribute (async, sync, auto)
            const lxb_char_t *decoding = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"decoding", 8, &attr_len);
            if (decoding && attr_len > 0) {
                char *decoding_str = malloc(attr_len + 1);
                memcpy(decoding_str, decoding, attr_len);
                decoding_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "decoding", decoding_str);
                free(decoding_str);
            } else {
                cJSON_AddStringToObject(elem_json, "decoding", "auto"); // default
            }
            
            // 7. srcset attribute (responsive images)
            const lxb_char_t *srcset = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"srcset", 6, &attr_len);
            if (srcset && attr_len > 0) {
                char *srcset_str = malloc(attr_len + 1);
                memcpy(srcset_str, srcset, attr_len);
                srcset_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "srcset", srcset_str);
                
                // Parse srcset into array for easier handling
                cJSON *srcset_array = parse_srcset(srcset_str);
                if (srcset_array) {
                    cJSON_AddItemToObject(elem_json, "srcset_parsed", srcset_array);
                }
                
                free(srcset_str);
            }
            
            // 8. sizes attribute (for srcset)
            const lxb_char_t *sizes = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"sizes", 5, &attr_len);
            if (sizes && attr_len > 0) {
                char *sizes_str = malloc(attr_len + 1);
                memcpy(sizes_str, sizes, attr_len);
                sizes_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "sizes", sizes_str);
                free(sizes_str);
            }
            
            // 9. crossorigin attribute
            const lxb_char_t *crossorigin = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"crossorigin", 11, &attr_len);
            if (crossorigin && attr_len > 0) {
                char *crossorigin_str = malloc(attr_len + 1);
                memcpy(crossorigin_str, crossorigin, attr_len);
                crossorigin_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "crossorigin", crossorigin_str);
                free(crossorigin_str);
            }
            
            // 10. referrerpolicy attribute
            const lxb_char_t *referrerpolicy = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"referrerpolicy", 13, &attr_len);
            if (referrerpolicy && attr_len > 0) {
                char *referrerpolicy_str = malloc(attr_len + 1);
                memcpy(referrerpolicy_str, referrerpolicy, attr_len);
                referrerpolicy_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "referrerpolicy", referrerpolicy_str);
                free(referrerpolicy_str);
            }
            
            // 11. ismap attribute (server-side image map)
            lxb_dom_attr_t *ismap_attr = lxb_dom_element_attr_by_name(
                elem, (lxb_char_t*)"ismap", 5);
            if (ismap_attr) {
                cJSON_AddBoolToObject(elem_json, "ismap", true);
            }
            
            // 12. usemap attribute (client-side image map)
            const lxb_char_t *usemap = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"usemap", 6, &attr_len);
            if (usemap && attr_len > 0) {
                char *usemap_str = malloc(attr_len + 1);
                memcpy(usemap_str, usemap, attr_len);
                usemap_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "usemap", usemap_str);
                free(usemap_str);
            }
            
            // 13. title attribute (tooltip)
            const lxb_char_t *title = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"title", 5, &attr_len);
            if (title && attr_len > 0) {
                char *title_str = malloc(attr_len + 1);
                memcpy(title_str, title, attr_len);
                title_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "title", title_str);
                free(title_str);
            }
            
            // 14. longdesc attribute (long description URL)
            const lxb_char_t *longdesc = lxb_dom_element_get_attribute(
                elem, (lxb_char_t*)"longdesc", 8, &attr_len);
            if (longdesc && attr_len > 0) {
                char *longdesc_str = malloc(attr_len + 1);
                memcpy(longdesc_str, longdesc, attr_len);
                longdesc_str[attr_len] = '\0';
                cJSON_AddStringToObject(elem_json, "longdesc", longdesc_str);
                free(longdesc_str);
            }
            
            // 15. Figure out image dimensions for layout
            calculate_image_dimensions(elem_json);
        }
        else if (strcasecmp(tag, "form") == 0) {
            elem_type = "form_ref";
            cJSON_ReplaceItemInObject(elem_json, "is_form", cJSON_CreateBool(true));
            
            // Generate a unique filename for this form
            static int form_counter = 0;
            char form_filename[256];
            form_counter++;
            
            const lxb_char_t *form_id = lxb_dom_element_id(elem, &tag_len);
            if (form_id && tag_len > 0) {
                char *id_str = malloc(tag_len + 1);
                if (id_str) {
                    memcpy(id_str, form_id, tag_len);
                    id_str[tag_len] = '\0';
                    snprintf(form_filename, sizeof(form_filename), "form_%s_%d.txt", id_str, form_counter);
                    free(id_str);
                }
            } else {
                snprintf(form_filename, sizeof(form_filename), "form_%d.txt", form_counter);
            }
            
            // Store the filename reference
            cJSON_ReplaceItemInObject(elem_json, "form_file", cJSON_CreateString(form_filename));
            
            // Store the element for extraction
            store_form_for_extraction(elem, form_filename);
            
            // Process form children normally
            // (Forms can contain labels, inputs, etc. that should appear in main rendering)
            cJSON *children_array = cJSON_CreateArray();
            int child_count = 0;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            while (child) {
                cJSON *child_json = process_element_for_rendering(child, depth + 1);
                if (child_json) {
                    cJSON_AddItemToArray(children_array, child_json);
                    child_count++;
                }
                child = lxb_dom_node_next(child);
            }
            
            if (child_count > 0) {
                cJSON_AddItemToObject(elem_json, "children", children_array);
            } else {
                cJSON_Delete(children_array);
            }
        }
        else if (strcasecmp(tag, "table") == 0) {
            elem_type = "table_ref";
            cJSON_ReplaceItemInObject(elem_json, "is_table", cJSON_CreateBool(true));
            
            // Generate a unique filename for this table
            static int table_counter = 0;
            char table_filename[256];
            table_counter++;
            
            const lxb_char_t *table_id = lxb_dom_element_id(elem, &tag_len);
            if (table_id && tag_len > 0) {
                char *id_str = malloc(tag_len + 1);
                if (id_str) {
                    memcpy(id_str, table_id, tag_len);
                    id_str[tag_len] = '\0';
                    snprintf(table_filename, sizeof(table_filename), "table_%s_%d.txt", id_str, table_counter);
                    free(id_str);
                }
            } else {
                snprintf(table_filename, sizeof(table_filename), "table_%d.txt", table_counter);
            }
            
            // Store the filename reference
            cJSON_ReplaceItemInObject(elem_json, "table_file", cJSON_CreateString(table_filename));
            
            // Store the element for extraction
            store_table_for_extraction(elem, table_filename);
            
            // STILL process children so they appear in main rendering
            // (even though full table goes to separate file)
            
            // Process children like normal
            cJSON *children_array = cJSON_CreateArray();
            int child_count = 0;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            while (child) {
                cJSON *child_json = process_element_for_rendering(child, depth + 1);
                if (child_json) {
                    cJSON_AddItemToArray(children_array, child_json);
                    child_count++;
                }
                child = lxb_dom_node_next(child);
            }
            
            if (child_count > 0) {
                cJSON_AddItemToObject(elem_json, "children", children_array);
            } else {
                cJSON_Delete(children_array);
            }
            
            free(tag);
            return elem_json;
        }
        else if (strcasecmp(tag, "form") == 0) {
            elem_type = "form_ref";
            cJSON_ReplaceItemInObject(elem_json, "is_form", cJSON_CreateBool(true));
            
            // Generate a unique filename for this form
            static int form_counter = 0;
            char form_filename[256];
            form_counter++;
            
            const lxb_char_t *form_id = lxb_dom_element_id(elem, &tag_len);
            if (form_id && tag_len > 0) {
                char *id_str = malloc(tag_len + 1);
                if (id_str) {
                    memcpy(id_str, form_id, tag_len);
                    id_str[tag_len] = '\0';
                    snprintf(form_filename, sizeof(form_filename), "form_%s_%d.txt", id_str, form_counter);
                    free(id_str);
                }
            } else {
                snprintf(form_filename, sizeof(form_filename), "form_%d.txt", form_counter);
            }
            
            // Store the filename reference
            cJSON_ReplaceItemInObject(elem_json, "form_file", cJSON_CreateString(form_filename));
            
            // Store the element for extraction
            store_form_for_extraction(elem, form_filename);
        }
        // FIXED: Separated nav/menu from ul/ol
        else if (strcasecmp(tag, "nav") == 0 || strcasecmp(tag, "menu") == 0) {
            elem_type = "menu_ref";
            cJSON_ReplaceItemInObject(elem_json, "is_menu", cJSON_CreateBool(true));
            
            // Generate a unique filename for this menu
            static int menu_counter = 0;
            char menu_filename[256];
            menu_counter++;
            
            const lxb_char_t *menu_id = lxb_dom_element_id(elem, &tag_len);
            if (menu_id && tag_len > 0) {
                char *id_str = malloc(tag_len + 1);
                if (id_str) {
                    memcpy(id_str, menu_id, tag_len);
                    id_str[tag_len] = '\0';
                    snprintf(menu_filename, sizeof(menu_filename), "menu_%s_%d.txt", id_str, menu_counter);
                    free(id_str);
                }
            } else {
                snprintf(menu_filename, sizeof(menu_filename), "menu_%d.txt", menu_counter);
            }
            
            // Store the filename reference
            cJSON_ReplaceItemInObject(elem_json, "menu_file", cJSON_CreateString(menu_filename));
            
            // Store the element for extraction
            store_menu_for_extraction(elem, menu_filename, tag);
            
            // Process menu children normally
            cJSON *children_array = cJSON_CreateArray();
            int child_count = 0;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            while (child) {
                cJSON *child_json = process_element_for_rendering(child, depth + 1);
                if (child_json) {
                    cJSON_AddItemToArray(children_array, child_json);
                    child_count++;
                }
                child = lxb_dom_node_next(child);
            }
            
            if (child_count > 0) {
                cJSON_AddItemToObject(elem_json, "children", children_array);
            } else {
                cJSON_Delete(children_array);
            }
        }
        else if (strcasecmp(tag, "a") == 0) {
            elem_type = "inline";
            cJSON_ReplaceItemInObject(elem_json, "is_link", cJSON_CreateBool(true));
            cJSON_ReplaceItemInObject(elem_json, "is_clickable", cJSON_CreateBool(true));
            
            // Default styles
            cJSON_ReplaceItemInObject(elem_json, "color", cJSON_CreateString("#0000FF"));
            cJSON_ReplaceItemInObject(elem_json, "text_decoration", cJSON_CreateString("underline"));
            
            // Parse all link attributes
            parse_link_element_complete(elem, elem_json);
        }
        else if (strcasecmp(tag, "button") == 0 || strcasecmp(tag, "input") == 0) {
            elem_type = "inline";
            cJSON_ReplaceItemInObject(elem_json, "is_button", cJSON_CreateBool(true));
            cJSON_ReplaceItemInObject(elem_json, "is_clickable", cJSON_CreateBool(true));
        }
        else if (strcasecmp(tag, "li") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "is_list_item", cJSON_CreateBool(true));
        }
        // FIXED: This is the MAIN list handling section
        else if (strcasecmp(tag, "ul") == 0 || strcasecmp(tag, "ol") == 0) {
            elem_type = "list_ref";
            cJSON_ReplaceItemInObject(elem_json, "is_list", cJSON_CreateBool(true));
            
            // Generate a unique filename for this list
            static int list_counter = 0;
            char list_filename[256];
            list_counter++;
            
            const lxb_char_t *list_id = lxb_dom_element_id(elem, &tag_len);
            if (list_id && tag_len > 0) {
                char *id_str = malloc(tag_len + 1);
                if (id_str) {
                    memcpy(id_str, list_id, tag_len);
                    id_str[tag_len] = '\0';
                    snprintf(list_filename, sizeof(list_filename), "list_%s_%d.txt", id_str, list_counter);
                    free(id_str);
                }
            } else {
                snprintf(list_filename, sizeof(list_filename), "list_%d.txt", list_counter);
            }
            
            // Store the filename reference
            cJSON_ReplaceItemInObject(elem_json, "list_file", cJSON_CreateString(list_filename));
            
            // Store the element for extraction
            store_list_for_extraction(elem, list_filename, tag);
            
            // Process list children normally (they appear in main rendering)
            cJSON *children_array = cJSON_CreateArray();
            int child_count = 0;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            while (child) {
                cJSON *child_json = process_element_for_rendering(child, depth + 1);
                if (child_json) {
                    cJSON_AddItemToArray(children_array, child_json);
                    child_count++;
                }
                child = lxb_dom_node_next(child);
            }
            
            if (child_count > 0) {
                cJSON_AddItemToObject(elem_json, "children", children_array);
            } else {
                cJSON_Delete(children_array);
            }
        }

        else if (strcasecmp(tag, "audio") == 0 ||
        strcasecmp(tag, "video") == 0 ||
        strcasecmp(tag, "canvas") == 0) {
   elem_type = "media_ref";
   cJSON_ReplaceItemInObject(elem_json, "is_media", cJSON_CreateBool(true));
   
   // Set specific media type
   if (strcasecmp(tag, "audio") == 0) {
       cJSON_ReplaceItemInObject(elem_json, "media_type", cJSON_CreateString("audio"));
       cJSON_ReplaceItemInObject(elem_json, "is_audio", cJSON_CreateBool(true));
   } else if (strcasecmp(tag, "video") == 0) {
       cJSON_ReplaceItemInObject(elem_json, "media_type", cJSON_CreateString("video"));
       cJSON_ReplaceItemInObject(elem_json, "is_video", cJSON_CreateBool(true));
   } else if (strcasecmp(tag, "canvas") == 0) {
       cJSON_ReplaceItemInObject(elem_json, "media_type", cJSON_CreateString("canvas"));
       cJSON_ReplaceItemInObject(elem_json, "is_canvas", cJSON_CreateBool(true));
   }
   
   // ========== ENHANCED MEDIA ATTRIBUTE EXTRACTION ==========
   size_t attr_len;
   
   // Common attributes for audio/video
   if (strcasecmp(tag, "audio") == 0 || strcasecmp(tag, "video") == 0) {
       // 1. src attribute
       const lxb_char_t *src = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"src", 3, &attr_len);
       if (src && attr_len > 0) {
           char *src_str = malloc(attr_len + 1);
           memcpy(src_str, src, attr_len);
           src_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "src", src_str);
           free(src_str);
       }
       
       // 2. Controls attribute
       lxb_dom_attr_t *controls_attr = lxb_dom_element_attr_by_name(
           elem, (lxb_char_t*)"controls", 8);
       if (controls_attr) {
           cJSON_AddBoolToObject(elem_json, "controls", true);
       }
       
       // 3. Autoplay attribute
       lxb_dom_attr_t *autoplay_attr = lxb_dom_element_attr_by_name(
           elem, (lxb_char_t*)"autoplay", 8);
       if (autoplay_attr) {
           cJSON_AddBoolToObject(elem_json, "autoplay", true);
       }
       
       // 4. Loop attribute
       lxb_dom_attr_t *loop_attr = lxb_dom_element_attr_by_name(
           elem, (lxb_char_t*)"loop", 4);
       if (loop_attr) {
           cJSON_AddBoolToObject(elem_json, "loop", true);
       }
       
       // 5. Muted attribute
       lxb_dom_attr_t *muted_attr = lxb_dom_element_attr_by_name(
           elem, (lxb_char_t*)"muted", 5);
       if (muted_attr) {
           cJSON_AddBoolToObject(elem_json, "muted", true);
       }
       
       // 6. Preload attribute (none, metadata, auto)
       const lxb_char_t *preload = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"preload", 7, &attr_len);
       if (preload && attr_len > 0) {
           char *preload_str = malloc(attr_len + 1);
           memcpy(preload_str, preload, attr_len);
           preload_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "preload", preload_str);
           free(preload_str);
       }
       
       // 7. Poster attribute (video only)
       if (strcasecmp(tag, "video") == 0) {
           const lxb_char_t *poster = lxb_dom_element_get_attribute(
               elem, (lxb_char_t*)"poster", 6, &attr_len);
           if (poster && attr_len > 0) {
               char *poster_str = malloc(attr_len + 1);
               memcpy(poster_str, poster, attr_len);
               poster_str[attr_len] = '\0';
               cJSON_AddStringToObject(elem_json, "poster", poster_str);
               free(poster_str);
           }
       }
       calculate_media_dimensions(elem_json); 
   }
   
   // Video-specific attributes
   if (strcasecmp(tag, "video") == 0) {
       // Width/height attributes
       const lxb_char_t *width_attr = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"width", 5, &attr_len);
       if (width_attr && attr_len > 0) {
           char *width_str = malloc(attr_len + 1);
           memcpy(width_str, width_attr, attr_len);
           width_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "attr_width", width_str);
           free(width_str);
       }
       
       const lxb_char_t *height_attr = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"height", 6, &attr_len);
       if (height_attr && attr_len > 0) {
           char *height_str = malloc(attr_len + 1);
           memcpy(height_str, height_attr, attr_len);
           height_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "attr_height", height_str);
           free(height_str);
       }
       
       // Playsinline attribute (for mobile)
       lxb_dom_attr_t *playsinline_attr = lxb_dom_element_attr_by_name(
           elem, (lxb_char_t*)"playsinline", 10);
       if (playsinline_attr) {
           cJSON_AddBoolToObject(elem_json, "playsinline", true);
       }
   }
   
   // Canvas-specific attributes
   if (strcasecmp(tag, "canvas") == 0) {
       // Width/height are REQUIRED for canvas
       const lxb_char_t *width_attr = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"width", 5, &attr_len);
       if (width_attr && attr_len > 0) {
           char *width_str = malloc(attr_len + 1);
           memcpy(width_str, width_attr, attr_len);
           width_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "attr_width", width_str);
           free(width_str);
       } else {
           cJSON_AddNumberToObject(elem_json, "attr_width", 300); // Default
       }
       
       const lxb_char_t *height_attr = lxb_dom_element_get_attribute(
           elem, (lxb_char_t*)"height", 6, &attr_len);
       if (height_attr && attr_len > 0) {
           char *height_str = malloc(attr_len + 1);
           memcpy(height_str, height_attr, attr_len);
           height_str[attr_len] = '\0';
           cJSON_AddStringToObject(elem_json, "attr_height", height_str);
           free(height_str);
       } else {
           cJSON_AddNumberToObject(elem_json, "attr_height", 150); // Default
       }
   }
   
   // ========== SOURCE ELEMENTS (<source> tags) ==========
   // Audio/Video can have multiple <source> children
   if (strcasecmp(tag, "audio") == 0 || strcasecmp(tag, "video") == 0) {
       cJSON *sources_array = cJSON_CreateArray();
       int source_count = 0;
       
       lxb_dom_node_t *child = lxb_dom_node_first_child(node);
       while (child) {
           if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
               lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
               size_t child_tag_len;
               const lxb_char_t *child_tag = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
               
               if (child_tag && child_tag_len > 0) {
                   char *child_tag_str = malloc(child_tag_len + 1);
                   memcpy(child_tag_str, child_tag, child_tag_len);
                   child_tag_str[child_tag_len] = '\0';
                   
                   if (strcasecmp(child_tag_str, "source") == 0) {
                       cJSON *source_json = cJSON_CreateObject();
                       
                       // Get source attributes
                       const lxb_char_t *src = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"src", 3, &child_tag_len);
                       if (src && child_tag_len > 0) {
                           char *src_str = malloc(child_tag_len + 1);
                           memcpy(src_str, src, child_tag_len);
                           src_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(source_json, "src", src_str);
                           free(src_str);
                       }
                       
                       const lxb_char_t *type = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"type", 4, &child_tag_len);
                       if (type && child_tag_len > 0) {
                           char *type_str = malloc(child_tag_len + 1);
                           memcpy(type_str, type, child_tag_len);
                           type_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(source_json, "type", type_str);
                           free(type_str);
                       }
                       
                       const lxb_char_t *media = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"media", 5, &child_tag_len);
                       if (media && child_tag_len > 0) {
                           char *media_str = malloc(child_tag_len + 1);
                           memcpy(media_str, media, child_tag_len);
                           media_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(source_json, "media", media_str);
                           free(media_str);
                       }
                       
                       const lxb_char_t *sizes = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"sizes", 5, &child_tag_len);
                       if (sizes && child_tag_len > 0) {
                           char *sizes_str = malloc(child_tag_len + 1);
                           memcpy(sizes_str, sizes, child_tag_len);
                           sizes_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(source_json, "sizes", sizes_str);
                           free(sizes_str);
                       }
                       
                       const lxb_char_t *srcset = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"srcset", 6, &child_tag_len);
                       if (srcset && child_tag_len > 0) {
                           char *srcset_str = malloc(child_tag_len + 1);
                           memcpy(srcset_str, srcset, child_tag_len);
                           srcset_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(source_json, "srcset", srcset_str);
                           
                           // Parse srcset like we did for images
                           cJSON *srcset_parsed = parse_srcset(srcset_str);
                           if (srcset_parsed) {
                               cJSON_AddItemToObject(source_json, "srcset_parsed", srcset_parsed);
                           }
                           free(srcset_str);
                       }
                       
                       cJSON_AddItemToArray(sources_array, source_json);
                       source_count++;
                   }
                   
                   free(child_tag_str);
               }
           }
           child = lxb_dom_node_next(child);
       }
       
       if (source_count > 0) {
           cJSON_AddItemToObject(elem_json, "sources", sources_array);
           cJSON_AddNumberToObject(elem_json, "source_count", source_count);
       } else {
           cJSON_Delete(sources_array);
       }
   }
   
   // ========== TRACK ELEMENTS (<track> tags) ==========
   // For subtitles/captions
   if (strcasecmp(tag, "video") == 0) {
       cJSON *tracks_array = cJSON_CreateArray();
       int track_count = 0;
       
       lxb_dom_node_t *child = lxb_dom_node_first_child(node);
       while (child) {
           if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
               lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
               size_t child_tag_len;
               const lxb_char_t *child_tag = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
               
               if (child_tag && child_tag_len > 0) {
                   char *child_tag_str = malloc(child_tag_len + 1);
                   memcpy(child_tag_str, child_tag, child_tag_len);
                   child_tag_str[child_tag_len] = '\0';
                   
                   if (strcasecmp(child_tag_str, "track") == 0) {
                       cJSON *track_json = cJSON_CreateObject();
                       
                       // Get track attributes
                       const lxb_char_t *src = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"src", 3, &child_tag_len);
                       if (src && child_tag_len > 0) {
                           char *src_str = malloc(child_tag_len + 1);
                           memcpy(src_str, src, child_tag_len);
                           src_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(track_json, "src", src_str);
                           free(src_str);
                       }
                       
                       const lxb_char_t *kind = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"kind", 4, &child_tag_len);
                       if (kind && child_tag_len > 0) {
                           char *kind_str = malloc(child_tag_len + 1);
                           memcpy(kind_str, kind, child_tag_len);
                           kind_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(track_json, "kind", kind_str);
                           free(kind_str);
                       } else {
                           cJSON_AddStringToObject(track_json, "kind", "subtitles"); // Default
                       }
                       
                       const lxb_char_t *srclang = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"srclang", 7, &child_tag_len);
                       if (srclang && child_tag_len > 0) {
                           char *srclang_str = malloc(child_tag_len + 1);
                           memcpy(srclang_str, srclang, child_tag_len);
                           srclang_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(track_json, "srclang", srclang_str);
                           free(srclang_str);
                       }
                       
                       const lxb_char_t *label = lxb_dom_element_get_attribute(
                           child_elem, (lxb_char_t*)"label", 5, &child_tag_len);
                       if (label && child_tag_len > 0) {
                           char *label_str = malloc(child_tag_len + 1);
                           memcpy(label_str, label, child_tag_len);
                           label_str[child_tag_len] = '\0';
                           cJSON_AddStringToObject(track_json, "label", label_str);
                           free(label_str);
                       }
                       
                       lxb_dom_attr_t *default_attr = lxb_dom_element_attr_by_name(
                           child_elem, (lxb_char_t*)"default", 7);
                       if (default_attr) {
                           cJSON_AddBoolToObject(track_json, "default", true);
                       }
                       
                       cJSON_AddItemToArray(tracks_array, track_json);
                       track_count++;
                   }
                   
                   free(child_tag_str);
               }
           }
           child = lxb_dom_node_next(child);
       }
       
       if (track_count > 0) {
           cJSON_AddItemToObject(elem_json, "tracks", tracks_array);
           cJSON_AddNumberToObject(elem_json, "track_count", track_count);
       } else {
           cJSON_Delete(tracks_array);
       }
   }
   
   // Calculate media dimensions for layout
   calculate_media_dimensions(elem_json);
}


//    IFRAME PARSING
else if (strcasecmp(tag, "iframe") == 0) {
    elem_type = "iframe_ref";
    cJSON_ReplaceItemInObject(elem_json, "is_iframe", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObject(elem_json, "display", cJSON_CreateString("inline-block"));
    
    // ========== COMPLETE IFRAME ATTRIBUTE PARSING ==========
    size_t attr_len;
    
    // 1. src attribute (most important)
    const lxb_char_t *src = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"src", 3, &attr_len);
    if (src && attr_len > 0) {
        char *src_str = malloc(attr_len + 1);
        memcpy(src_str, src, attr_len);
        src_str[attr_len] = '\0';
        cJSON_ReplaceItemInObject(elem_json, "src", cJSON_CreateString(src_str));
        cJSON_AddStringToObject(elem_json, "iframe_src", src_str); // Also store as iframe_src
        free(src_str);
    }
    
    // 2. title attribute (required for accessibility)
    const lxb_char_t *title = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"title", 5, &attr_len);
    if (title && attr_len > 0) {
        char *title_str = malloc(attr_len + 1);
        memcpy(title_str, title, attr_len);
        title_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_title", title_str);
        free(title_str);
    }
    
    // 3. name attribute
    const lxb_char_t *name = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"name", 4, &attr_len);
    if (name && attr_len > 0) {
        char *name_str = malloc(attr_len + 1);
        memcpy(name_str, name, attr_len);
        name_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_name", name_str);
        free(name_str);
    }
    
    // 4. width attribute (can be pixels or percentage)
    const lxb_char_t *width = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"width", 5, &attr_len);
    if (width && attr_len > 0) {
        char *width_str = malloc(attr_len + 1);
        memcpy(width_str, width, attr_len);
        width_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_width", width_str);
        
        // Parse for numeric value if possible
        char *endptr;
        double width_val = strtod(width_str, &endptr);
        if (endptr != width_str) {
            cJSON_AddNumberToObject(elem_json, "iframe_width_px", width_val);
        }
        free(width_str);
    } else {
        cJSON_AddStringToObject(elem_json, "iframe_width", "300"); // Default
    }
    
    // 5. height attribute (can be pixels or percentage)
    const lxb_char_t *height = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"height", 6, &attr_len);
    if (height && attr_len > 0) {
        char *height_str = malloc(attr_len + 1);
        memcpy(height_str, height, attr_len);
        height_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_height", height_str);
        
        // Parse for numeric value if possible
        char *endptr;
        double height_val = strtod(height_str, &endptr);
        if (endptr != height_str) {
            cJSON_AddNumberToObject(elem_json, "iframe_height_px", height_val);
        }
        free(height_str);
    } else {
        cJSON_AddStringToObject(elem_json, "iframe_height", "150"); // Default
    }
    
    // 6. sandbox attribute (space-separated tokens)
    const lxb_char_t *sandbox = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"sandbox", 7, &attr_len);
    if (sandbox && attr_len > 0) {
        char *sandbox_str = malloc(attr_len + 1);
        memcpy(sandbox_str, sandbox, attr_len);
        sandbox_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "sandbox", sandbox_str);
        
        // Parse sandbox tokens into array
        cJSON *sandbox_array = cJSON_CreateArray();
        char *saveptr;
        char *token = strtok_r(sandbox_str, " \t\n\r", &saveptr);
        int sandbox_count = 0;
        
        while (token) {
            if (strlen(token) > 0) {
                cJSON_AddItemToArray(sandbox_array, cJSON_CreateString(token));
                sandbox_count++;
                
                // Set individual flags for common sandbox values
                if (strcasecmp(token, "allow-forms") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_forms", true);
                } else if (strcasecmp(token, "allow-scripts") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_scripts", true);
                } else if (strcasecmp(token, "allow-same-origin") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_same_origin", true);
                } else if (strcasecmp(token, "allow-popups") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_popups", true);
                } else if (strcasecmp(token, "allow-top-navigation") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_top_navigation", true);
                } else if (strcasecmp(token, "allow-popups-to-escape-sandbox") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_popups_to_escape", true);
                } else if (strcasecmp(token, "allow-modals") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_modals", true);
                } else if (strcasecmp(token, "allow-orientation-lock") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_orientation_lock", true);
                } else if (strcasecmp(token, "allow-pointer-lock") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_pointer_lock", true);
                } else if (strcasecmp(token, "allow-presentation") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_presentation", true);
                } else if (strcasecmp(token, "allow-storage-access") == 0) {
                    cJSON_AddBoolToObject(elem_json, "sandbox_allow_storage_access", true);
                }
            }
            token = strtok_r(NULL, " \t\n\r", &saveptr);
        }
        
        if (sandbox_count > 0) {
            cJSON_AddItemToObject(elem_json, "sandbox_tokens", sandbox_array);
            cJSON_AddNumberToObject(elem_json, "sandbox_token_count", sandbox_count);
        } else {
            cJSON_Delete(sandbox_array);
        }
        
        free(sandbox_str);
    } else {
        cJSON_AddStringToObject(elem_json, "sandbox", ""); // Empty means full restrictions
    }
    
    // 7. allow attribute (space-separated permissions)
    const lxb_char_t *allow = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"allow", 5, &attr_len);
    if (allow && attr_len > 0) {
        char *allow_str = malloc(attr_len + 1);
        memcpy(allow_str, allow, attr_len);
        allow_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "allow", allow_str);
        
        // Parse allow permissions into array
        cJSON *allow_array = cJSON_CreateArray();
        char *saveptr;
        char *token = strtok_r(allow_str, " \t\n\r;", &saveptr); // Also split on semicolons
        int allow_count = 0;
        
        while (token) {
            if (strlen(token) > 0) {
                cJSON_AddItemToArray(allow_array, cJSON_CreateString(token));
                allow_count++;
                
                // Common permission patterns
                if (strstr(token, "camera") != NULL) {
                    cJSON_AddBoolToObject(elem_json, "allow_camera", true);
                }
                if (strstr(token, "microphone") != NULL) {
                    cJSON_AddBoolToObject(elem_json, "allow_microphone", true);
                }
                if (strstr(token, "geolocation") != NULL) {
                    cJSON_AddBoolToObject(elem_json, "allow_geolocation", true);
                }
                if (strstr(token, "fullscreen") != NULL) {
                    cJSON_AddBoolToObject(elem_json, "allow_fullscreen", true);
                }
                if (strstr(token, "payment") != NULL) {
                    cJSON_AddBoolToObject(elem_json, "allow_payment", true);
                }
            }
            token = strtok_r(NULL, " \t\n\r;", &saveptr);
        }
        
        if (allow_count > 0) {
            cJSON_AddItemToObject(elem_json, "allow_permissions", allow_array);
            cJSON_AddNumberToObject(elem_json, "allow_count", allow_count);
        } else {
            cJSON_Delete(allow_array);
        }
        
        free(allow_str);
    }
    
    // 8. srcdoc attribute (inline HTML content)
    const lxb_char_t *srcdoc = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"srcdoc", 6, &attr_len);
    if (srcdoc && attr_len > 0) {
        // Note: srcdoc contains HTML, not just text
        char *srcdoc_str = malloc(attr_len + 1);
        memcpy(srcdoc_str, srcdoc, attr_len);
        srcdoc_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "srcdoc", srcdoc_str);
        cJSON_AddBoolToObject(elem_json, "has_inline_content", true);
        
        // Store length for reference
        cJSON_AddNumberToObject(elem_json, "srcdoc_length", attr_len);
        free(srcdoc_str);
    }
    
    // 9. loading attribute (lazy, eager)
    const lxb_char_t *loading = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"loading", 7, &attr_len);
    if (loading && attr_len > 0) {
        char *loading_str = malloc(attr_len + 1);
        memcpy(loading_str, loading, attr_len);
        loading_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_loading", loading_str);
        free(loading_str);
    } else {
        cJSON_AddStringToObject(elem_json, "iframe_loading", "eager"); // Default
    }
    
    // 10. referrerpolicy attribute
    const lxb_char_t *referrerpolicy = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"referrerpolicy", 13, &attr_len);
    if (referrerpolicy && attr_len > 0) {
        char *policy_str = malloc(attr_len + 1);
        memcpy(policy_str, referrerpolicy, attr_len);
        policy_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "iframe_referrerpolicy", policy_str);
        free(policy_str);
    }
    
    // 11. allowfullscreen attribute (boolean)
    lxb_dom_attr_t *allowfullscreen_attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)"allowfullscreen", 15);
    if (allowfullscreen_attr) {
        cJSON_AddBoolToObject(elem_json, "allowfullscreen", true);
    }
    
    // 12. allowpaymentrequest attribute (boolean)
    lxb_dom_attr_t *allowpaymentrequest_attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)"allowpaymentrequest", 19);
    if (allowpaymentrequest_attr) {
        cJSON_AddBoolToObject(elem_json, "allowpaymentrequest", true);
    }
    
    // 13. csp attribute (Content Security Policy)
    const lxb_char_t *csp = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"csp", 3, &attr_len);
    if (csp && attr_len > 0) {
        char *csp_str = malloc(attr_len + 1);
        memcpy(csp_str, csp, attr_len);
        csp_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "csp", csp_str);
        free(csp_str);
    }
    
    // 14. Security analysis
    // Check if iframe is potentially dangerous
    const char *src_value = cJSON_GetObjectItem(elem_json, "src") ? 
                           cJSON_GetObjectItem(elem_json, "src")->valuestring : NULL;
    
    if (src_value && strlen(src_value) > 0) {
        // Check for data: URLs (can be malicious)
        if (strncasecmp(src_value, "data:", 5) == 0) {
            cJSON_AddBoolToObject(elem_json, "security_warning", true);
            cJSON_AddStringToObject(elem_json, "security_issue", 
                                  "iframe with data: URL (potentially unsafe)");
        }
        
        // Check for javascript: URLs
        if (strncasecmp(src_value, "javascript:", 11) == 0) {
            cJSON_AddBoolToObject(elem_json, "security_warning", true);
            cJSON_AddStringToObject(elem_json, "security_issue", 
                                  "iframe with javascript: URL (unsafe)");
        }
        
        // Check if sandbox is empty (full restrictions) - actually safer!
        cJSON *sandbox_item = cJSON_GetObjectItem(elem_json, "sandbox");
        if (sandbox_item && strlen(sandbox_item->valuestring) == 0) {
            cJSON_AddBoolToObject(elem_json, "fully_sandboxed", true);
        }
    }
    
    // 15. Add iframe type classification
    if (cJSON_GetObjectItem(elem_json, "srcdoc")) {
        cJSON_AddStringToObject(elem_json, "iframe_type", "inline_html");
    } else if (src_value && strncasecmp(src_value, "about:", 6) == 0) {
        cJSON_AddStringToObject(elem_json, "iframe_type", "about_page");
    } else if (src_value && strncasecmp(src_value, "blob:", 5) == 0) {
        cJSON_AddStringToObject(elem_json, "iframe_type", "blob_url");
    } else if (src_value && strstr(src_value, "youtube.com") != NULL) {
        cJSON_AddStringToObject(elem_json, "iframe_type", "youtube_embed");
    } else if (src_value && strlen(src_value) > 0) {
        cJSON_AddStringToObject(elem_json, "iframe_type", "external_content");
    } else {
        cJSON_AddStringToObject(elem_json, "iframe_type", "empty");
    }
    
    // ========== DEFAULT DIMENSIONS FOR LAYOUT ==========
    // Calculate approximate dimensions for rendering
    calculate_iframe_dimensions(elem_json);
}
// Kraj IFRAME ***********

// ⬇️⬇️⬇️ SEMANTIC ELEMENTS  ⬇️⬇️⬇️
else if (strcasecmp(tag, "header") == 0 ||
         strcasecmp(tag, "footer") == 0 ||
         strcasecmp(tag, "section") == 0 ||
         strcasecmp(tag, "article") == 0 ||
         strcasecmp(tag, "aside") == 0 ||
         strcasecmp(tag, "main") == 0 ||
         strcasecmp(tag, "nav") == 0 ||           // Already handled above, but include for completeness
         strcasecmp(tag, "figure") == 0 ||
         strcasecmp(tag, "figcaption") == 0 ||
         strcasecmp(tag, "time") == 0 ||
         strcasecmp(tag, "mark") == 0 ||
         strcasecmp(tag, "summary") == 0 ||
         strcasecmp(tag, "details") == 0 ||
         strcasecmp(tag, "dialog") == 0 ||
         strcasecmp(tag, "meter") == 0 ||
         strcasecmp(tag, "progress") == 0 ||
         strcasecmp(tag, "output") == 0 ||
         strcasecmp(tag, "data") == 0) {
    
    elem_type = "semantic_block";
    cJSON_ReplaceItemInObject(elem_json, "is_semantic", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObject(elem_json, "semantic_type", cJSON_CreateString(tag));
    
    // Add specific semantic element flags
    if (strcasecmp(tag, "header") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_header", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "footer") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_footer", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "section") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_section", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "article") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_article", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "aside") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_aside", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "main") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_main", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "nav") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_nav", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "figure") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_figure", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "figcaption") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_figcaption", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "time") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_time", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "mark") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_mark", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "summary") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_summary", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "details") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_details", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "dialog") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_dialog", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "meter") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_meter", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "progress") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_progress", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "output") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_output", cJSON_CreateBool(true));
    } else if (strcasecmp(tag, "data") == 0) {
        cJSON_ReplaceItemInObject(elem_json, "is_data", cJSON_CreateBool(true));
    }
    
    // Default styling for semantic blocks
    cJSON_ReplaceItemInObject(elem_json, "display", cJSON_CreateString("block"));
    
    // For section/article elements, they can affect heading hierarchy
    if (strcasecmp(tag, "section") == 0 || 
        strcasecmp(tag, "article") == 0 ||
        strcasecmp(tag, "aside") == 0 ||
        strcasecmp(tag, "nav") == 0) {
        // These will be tracked in outline system
        cJSON_AddNumberToObject(elem_json, "affects_outline", 1);
    }
}
// ⬆️⬆️⬆️ END SEMANTIC ELEMENTS ⬆️⬆️⬆️


else if (strcasecmp(tag, "details") == 0) {
    elem_type = "details_ref";
    cJSON_ReplaceItemInObject(elem_json, "is_details", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObject(elem_json, "display", cJSON_CreateString("block"));
    
    // ========== DETAILS ATTRIBUTE PARSING ==========
    size_t attr_len;
    
    // 1. open attribute (boolean - whether details is expanded)
    lxb_dom_attr_t *open_attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)"open", 4);
    if (open_attr) {
        cJSON_ReplaceItemInObject(elem_json, "details_open", cJSON_CreateBool(true));
        cJSON_AddBoolToObject(elem_json, "is_expanded", true);
    } else {
        cJSON_ReplaceItemInObject(elem_json, "details_open", cJSON_CreateBool(false));
        cJSON_AddBoolToObject(elem_json, "is_expanded", false);
    }
    
    // 2. name attribute (for scripting)
    const lxb_char_t *name = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"name", 4, &attr_len);
    if (name && attr_len > 0) {
        char *name_str = malloc(attr_len + 1);
        memcpy(name_str, name, attr_len);
        name_str[attr_len] = '\0';
        cJSON_AddStringToObject(elem_json, "details_name", name_str);
        free(name_str);
    }
    
    // ========== FIND SUMMARY ELEMENT ==========
    // Look for direct child <summary> element
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    cJSON *summary_json = NULL;
    int summary_found = 0;
    
    while (child && !summary_found) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t child_tag_len;
            const lxb_char_t *child_tag = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
            
            if (child_tag && child_tag_len > 0) {
                char *child_tag_str = malloc(child_tag_len + 1);
                memcpy(child_tag_str, child_tag, child_tag_len);
                child_tag_str[child_tag_len] = '\0';
                
                if (strcasecmp(child_tag_str, "summary") == 0) {
                    // Found the summary!
                    summary_found = 1;
                    
                    // Process the summary element
                    summary_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(summary_json, "tag", "summary");
                    cJSON_AddStringToObject(summary_json, "type", "inline");
                    cJSON_AddBoolToObject(summary_json, "is_summary", true);
                    
                    // Get summary text
                    char *summary_text = get_element_text(child_elem);
                    if (summary_text && strlen(summary_text) > 0) {
                        // Trim
                        char *start = summary_text;
                        while (*start && isspace(*start)) start++;
                        char *end = start + strlen(start) - 1;
                        while (end > start && isspace(*end)) {
                            *end = '\0';
                            end--;
                        }
                        if (strlen(start) > 0) {
                            cJSON_AddStringToObject(summary_json, "text", start);
                            cJSON_AddStringToObject(elem_json, "summary_text", start);
                        }
                    }
                    if (summary_text) free(summary_text);
                    
                    // Get summary attributes
                    const lxb_char_t *summary_id = lxb_dom_element_id(child_elem, &child_tag_len);
                    if (summary_id && child_tag_len > 0) {
                        char *id_str = malloc(child_tag_len + 1);
                        memcpy(id_str, summary_id, child_tag_len);
                        id_str[child_tag_len] = '\0';
                        cJSON_AddStringToObject(summary_json, "id", id_str);
                        free(id_str);
                    }
                    
                    // Add to details element
                    cJSON_AddItemToObject(elem_json, "summary_element", summary_json);
                }
                
                free(child_tag_str);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    // If no summary found, create a default one
    if (!summary_found) {
        cJSON_AddStringToObject(elem_json, "summary_text", "Details");
        cJSON_AddBoolToObject(elem_json, "has_default_summary", true);
    }
    
    cJSON_AddNumberToObject(elem_json, "summary_found", summary_found);
    
    // ========== ESTIMATE CONTENT SIZE ==========
    // Count non-summary children for content size estimation
    int content_child_count = 0;
    child = lxb_dom_node_first_child(node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t child_tag_len;
            const lxb_char_t *child_tag = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
            
            if (child_tag && child_tag_len > 0) {
                char *child_tag_str = malloc(child_tag_len + 1);
                memcpy(child_tag_str, child_tag, child_tag_len);
                child_tag_str[child_tag_len] = '\0';
                
                // Only count if not a summary element
                if (strcasecmp(child_tag_str, "summary") != 0) {
                    content_child_count++;
                }
                
                free(child_tag_str);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            // Count text nodes that have actual content
            const lxb_char_t *text = lxb_dom_node_text_content(child, NULL);
            if (text) {
                const lxb_char_t *p = text;
                int has_content = 0;
                while (*p != '\0') {
                    if (!isspace(*p)) {
                        has_content = 1;
                        break;
                    }
                    p++;
                }
                if (has_content) {
                    content_child_count++;
                }
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    cJSON_AddNumberToObject(elem_json, "content_child_count", content_child_count);
    
    // ========== DEFAULT STYLING FOR DETAILS ==========
    // Add some default styles if not present
    if (!cJSON_GetObjectItem(elem_json, "border_width")) {
        cJSON_AddNumberToObject(elem_json, "border_width", 1);
    }
    if (!cJSON_GetObjectItem(elem_json, "border_style")) {
        cJSON_AddStringToObject(elem_json, "border_style", "solid");
    }
    if (!cJSON_GetObjectItem(elem_json, "border_color")) {
        cJSON_AddStringToObject(elem_json, "border_color", "#cccccc");
    }
    if (!cJSON_GetObjectItem(elem_json, "padding")) {
        cJSON_AddStringToObject(elem_json, "padding", "8px");
    }
    if (!cJSON_GetObjectItem(elem_json, "margin")) {
        cJSON_AddStringToObject(elem_json, "margin", "10px 0");
    }
    if (!cJSON_GetObjectItem(elem_json, "border_radius")) {
        cJSON_AddStringToObject(elem_json, "border_radius", "4px");
    }
    
    // Summary-specific defaults
    cJSON_AddStringToObject(elem_json, "summary_font_weight", "bold");
    cJSON_AddStringToObject(elem_json, "summary_cursor", "pointer");
    cJSON_AddStringToObject(elem_json, "summary_padding", "8px");
    cJSON_AddStringToObject(elem_json, "summary_bg_color", "#f0f0f0");
    
    // ========== CREATE TOGGLE INDICATOR ==========
    // Add visual indicator for expand/collapse
    if (cJSON_GetObjectItem(elem_json, "details_open") && 
        cJSON_IsTrue(cJSON_GetObjectItem(elem_json, "details_open"))) {
        cJSON_AddStringToObject(elem_json, "toggle_indicator", "▼"); // Down arrow for open
    } else {
        cJSON_AddStringToObject(elem_json, "toggle_indicator", "▶"); // Right arrow for closed
    }
    
    // ========== ESTIMATE DIMENSIONS ==========
    // Closed state: just summary height + borders
    int closed_height = 40; // Default summary height
    // Open state: estimate based on content
    int open_height = closed_height + (content_child_count * 25); // Rough estimate
    
    cJSON_AddNumberToObject(elem_json, "estimated_closed_height", closed_height);
    cJSON_AddNumberToObject(elem_json, "estimated_open_height", open_height);
    
    // Current height based on open state
    if (cJSON_GetObjectItem(elem_json, "details_open") && 
        cJSON_IsTrue(cJSON_GetObjectItem(elem_json, "details_open"))) {
        cJSON_AddNumberToObject(elem_json, "current_height", open_height);
    } else {
        cJSON_AddNumberToObject(elem_json, "current_height", closed_height);
    }
    
    // Default width
    cJSON_AddNumberToObject(elem_json, "estimated_width", 400);
}

else if (strcasecmp(tag, "summary") == 0) {
    elem_type = "inline";
    cJSON_ReplaceItemInObject(elem_json, "is_summary", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObject(elem_json, "is_clickable", cJSON_CreateBool(true));
    
    // Summary elements are usually bold and have pointer cursor
    cJSON_ReplaceItemInObject(elem_json, "font_weight", cJSON_CreateString("bold"));
    cJSON_AddStringToObject(elem_json, "cursor", "pointer");
    
    // Check if parent is <details>
    lxb_dom_node_t *parent = lxb_dom_node_parent(node);
    if (parent && parent->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *parent_elem = lxb_dom_interface_element(parent);
        size_t parent_tag_len;
        const lxb_char_t *parent_tag = lxb_dom_element_qualified_name(parent_elem, &parent_tag_len);
        
        if (parent_tag && parent_tag_len > 0) {
            char *parent_tag_str = malloc(parent_tag_len + 1);
            memcpy(parent_tag_str, parent_tag, parent_tag_len);
            parent_tag_str[parent_tag_len] = '\0';
            
            if (strcasecmp(parent_tag_str, "details") == 0) {
                cJSON_AddBoolToObject(elem_json, "is_details_summary", true);
                
                // Check if parent details is open
                lxb_dom_attr_t *open_attr = lxb_dom_element_attr_by_name(
                    parent_elem, (lxb_char_t*)"open", 4);
                if (open_attr) {
                    cJSON_AddBoolToObject(elem_json, "parent_details_open", true);
                } else {
                    cJSON_AddBoolToObject(elem_json, "parent_details_open", false);
                }
            }
            
            free(parent_tag_str);
        }
    }
}

else if (strcasecmp(tag, "h1") == 0 || strcasecmp(tag, "h2") == 0 || 
strcasecmp(tag, "h3") == 0 || strcasecmp(tag, "h4") == 0 ||
strcasecmp(tag, "h5") == 0 || strcasecmp(tag, "h6") == 0) {
cJSON_ReplaceItemInObject(elem_json, "is_heading", cJSON_CreateBool(true));
cJSON_ReplaceItemInObject(elem_json, "font_weight", cJSON_CreateString("bold"));

// ⬇️ ADD HEADING LEVEL DETECTION HERE ⬇️
int heading_level = 0;
if (strcasecmp(tag, "h1") == 0) heading_level = 1;
else if (strcasecmp(tag, "h2") == 0) heading_level = 2;
else if (strcasecmp(tag, "h3") == 0) heading_level = 3;
else if (strcasecmp(tag, "h4") == 0) heading_level = 4;
else if (strcasecmp(tag, "h5") == 0) heading_level = 5;
else if (strcasecmp(tag, "h6") == 0) heading_level = 6;

cJSON_ReplaceItemInObject(elem_json, "heading_level", cJSON_CreateNumber(heading_level));

// Default heading sizes
if (heading_level == 1) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(32));
} else if (heading_level == 2) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(24));
} else if (heading_level == 3) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(19));
} else if (heading_level == 4) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(16));
} else if (heading_level == 5) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(13));
} else if (heading_level == 6) {
cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(11));
}
}
// ⬆️⬆️⬆️ END YOUR EXISTING HEADING CODE ⬆️⬆️⬆️
        else if (strcasecmp(tag, "p") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "is_paragraph", cJSON_CreateBool(true));
        }
        
        // Check for inline elements
        const char *inline_tags[] = {
            "span", "a", "strong", "em", "i", "b", "u", "code",
            "mark", "small", "sub", "sup", "abbr", "cite", "label",
            "q", "s", "strike", "tt", "var", "bdi", "bdo", "br",
            "wbr", "img", "input", "button", "select", "textarea",
            "output", "progress", "meter", "time", "data", NULL
        };
        
        for (int i = 0; inline_tags[i] != NULL; i++) {
            if (strcasecmp(tag, inline_tags[i]) == 0) {
                elem_type = "inline";
                cJSON_ReplaceItemInObject(elem_json, "is_inline", cJSON_CreateBool(true));
                cJSON_ReplaceItemInObject(elem_json, "is_block", cJSON_CreateBool(false));
                cJSON_ReplaceItemInObject(elem_json, "display", cJSON_CreateString("inline"));
                break;
            }
        }
        
        // Set the type
        cJSON_ReplaceItemInObject(elem_json, "type", cJSON_CreateString(elem_type));
        
        // Get actual text content
        char *text = get_element_text(elem);
        if (text && strlen(text) > 0) {
            char *start = text;
            while (*start && isspace(*start)) start++;
            
            char *end = start + strlen(start) - 1;
            while (end > start && isspace(*end)) {
                *end = '\0';
                end--;
            }
            
            if (strlen(start) > 0) {
                cJSON_ReplaceItemInObject(elem_json, "text", cJSON_CreateString(start));
            }
        }
        if (text) free(text);
        
        // Get actual ID
        const lxb_char_t *id = lxb_dom_element_id(elem, &tag_len);
        if (id && tag_len > 0) {
            char *id_str = malloc(tag_len + 1);
            memcpy(id_str, id, tag_len);
            id_str[tag_len] = '\0';
            if (strlen(id_str) > 0) {
                cJSON_ReplaceItemInObject(elem_json, "id", cJSON_CreateString(id_str));
            }
            free(id_str);
        }
        
        // Get actual classes
        const lxb_char_t *cls = lxb_dom_element_class(elem, &tag_len);
        if (cls && tag_len > 0) {
            char *cls_str = malloc(tag_len + 1);
            memcpy(cls_str, cls, tag_len);
            cls_str[tag_len] = '\0';
            cJSON_ReplaceItemInObject(elem_json, "class_string", cJSON_CreateString(cls_str));
            
            // Parse into array
            char *saveptr;
            char *token = strtok_r(cls_str, " \t\n\r", &saveptr);
            while (token) {
                if (strlen(token) > 0) {
                    cJSON_AddItemToArray(classes_array, cJSON_CreateString(token));
                }
                token = strtok_r(NULL, " \t\n\r", &saveptr);
            }
            
            free(cls_str);
        }
        
        // Get actual attributes and override defaults
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
                    // Clean value
                    char *val_start = value_str;
                    while (*val_start == ' ') val_start++;
                    char *val_end = val_start + strlen(val_start) - 1;
                    while (val_end > val_start && (*val_end == ' ' || *val_end == '\t' || *val_end == '\n')) {
                        *val_end = '\0';
                        val_end--;
                    }
                    
                    if (strlen(name_str) > 0 && strlen(val_start) > 0) {
                        // Remove quotes from URLs
                        if (val_start[0] == '"' || val_start[0] == '\'') {
                            memmove(val_start, val_start + 1, strlen(val_start));
                            val_end = val_start + strlen(val_start) - 1;
                            if (val_end >= val_start && (*val_end == '"' || *val_end == '\'')) {
                                *val_end = '\0';
                            }
                        }
                        
               // ⬇️⬇️⬇️ ADD DATA ATTRIBUTE HANDLING HERE ⬇️⬇️⬇️
                // Check if it's a data-* attribute
                if (strncmp(name_str, "data-", 5) == 0) {
                    // Create data_attributes object if it doesn't exist
                    cJSON *data_attrs = cJSON_GetObjectItem(elem_json, "data_attributes");
                    if (!data_attrs) {
                        data_attrs = cJSON_CreateObject();
                        cJSON_AddItemToObject(elem_json, "data_attributes", data_attrs);
                    }
                    
                    // Convert data-attr-name to camelCase or keep as is
                    char *data_key = name_str + 5; // Skip "data-"
                    
                    // Option 1: Keep as is (with dash)
                    // cJSON_AddStringToObject(data_attrs, data_key, val_start);
                    
                    // Option 2: Convert to camelCase (data-foo-bar -> fooBar)
                    char camel_key[256];
                    convert_dash_to_camel(data_key, camel_key, sizeof(camel_key));
                    cJSON_AddStringToObject(data_attrs, camel_key, val_start);
                    
                    // Also store original dashed name
                    cJSON_AddStringToObject(data_attrs, name_str, val_start);
                    
                    // Count data attributes
                    cJSON *data_count = cJSON_GetObjectItem(elem_json, "data_attribute_count");
                    if (data_count) {
                        cJSON_SetNumberValue(data_count, data_count->valueint + 1);
                    } else {
                        cJSON_AddNumberToObject(elem_json, "data_attribute_count", 1);
                    }
                }
                // ⬆️⬆️⬆️ END DATA ATTRIBUTE HANDLING ⬆️⬆️⬆️

                        // Override defaults with actual values
                        if (strcmp(name_str, "href") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "href", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "src") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "src", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "alt") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "alt", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "title") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "title", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "width") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "width", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "height") == 0) {
                            cJSON_ReplaceItemInObject(elem_json, "height", cJSON_CreateString(val_start));
                        } else if (strcmp(name_str, "type") == 0) {
                            // For input elements
                            cJSON_ReplaceItemInObject(elem_json, "input_type", cJSON_CreateString(val_start));
                        }
                    }
                    
                    free(name_str);
                    free(value_str);
                }
            }
            
            attr = lxb_dom_element_next_attribute(attr);
        }
        
        // Get inline styles and override defaults
        lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(elem, LXB_DOM_ATTR_STYLE);
        if (style_attr) {
            cJSON *styles = parse_inline_styles_simple(style_attr);
            if (styles) {
                // Override with actual style values
                cJSON *bg_color = cJSON_GetObjectItem(styles, "background-color");
                if (bg_color && cJSON_IsString(bg_color)) {
                    cJSON_ReplaceItemInObject(elem_json, "bg_color", cJSON_CreateString(bg_color->valuestring));
                }
                
                cJSON *color = cJSON_GetObjectItem(styles, "color");
                if (color && cJSON_IsString(color)) {
                    cJSON_ReplaceItemInObject(elem_json, "color", cJSON_CreateString(color->valuestring));
                }
                
                cJSON *font_size = cJSON_GetObjectItem(styles, "font-size");
                if (font_size && cJSON_IsString(font_size)) {
                    // Convert to integer if it's in px
                    char *value = font_size->valuestring;
                    char *endptr;
                    int size = strtol(value, &endptr, 10);
                    if (endptr != value && strstr(value, "px")) {
                        cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(size));
                    }
                }
                
                cJSON *font_family = cJSON_GetObjectItem(styles, "font-family");
                if (font_family && cJSON_IsString(font_family)) {
                    cJSON_ReplaceItemInObject(elem_json, "font_family", cJSON_CreateString(font_family->valuestring));
                }
                
                cJSON *font_weight = cJSON_GetObjectItem(styles, "font-weight");
                if (font_weight && cJSON_IsString(font_weight)) {
                    cJSON_ReplaceItemInObject(elem_json, "font_weight", cJSON_CreateString(font_weight->valuestring));
                }
                
                cJSON *font_style = cJSON_GetObjectItem(styles, "font-style");
                if (font_style && cJSON_IsString(font_style)) {
                    cJSON_ReplaceItemInObject(elem_json, "font_style", cJSON_CreateString(font_style->valuestring));
                }
                
                cJSON *text_align = cJSON_GetObjectItem(styles, "text-align");
                if (text_align && cJSON_IsString(text_align)) {
                    cJSON_ReplaceItemInObject(elem_json, "text_align", cJSON_CreateString(text_align->valuestring));
                }
                
                cJSON_Delete(styles);
            }
        }
        
        // Override font sizes for headings
        if (strcasecmp(tag, "h1") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(32));
        } else if (strcasecmp(tag, "h2") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(24));
        } else if (strcasecmp(tag, "h3") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(19));
        } else if (strcasecmp(tag, "h4") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(16));
        } else if (strcasecmp(tag, "h5") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(13));
        } else if (strcasecmp(tag, "h6") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_size", cJSON_CreateNumber(11));
        }
        
        // Override font styles for specific tags
        if (strcasecmp(tag, "b") == 0 || strcasecmp(tag, "strong") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_weight", cJSON_CreateString("bold"));
        } else if (strcasecmp(tag, "i") == 0 || strcasecmp(tag, "em") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "font_style", cJSON_CreateString("italic"));
        } else if (strcasecmp(tag, "u") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "text_decoration", cJSON_CreateString("underline"));
        } else if (strcasecmp(tag, "s") == 0 || strcasecmp(tag, "strike") == 0) {
            cJSON_ReplaceItemInObject(elem_json, "text_decoration", cJSON_CreateString("line-through"));
        }
        
        // Add layout information if available
        if (global_computed_layout) {
            merge_layout_with_element(elem_json, global_computed_layout);
        }
        
        // Process children (except for special types)
        if (strcasecmp(elem_type, "image") != 0 && 
            strcasecmp(elem_type, "table_ref") != 0 &&
            strcasecmp(elem_type, "form_ref") != 0 &&
            strcasecmp(elem_type, "menu_ref") != 0) {
            
            cJSON *children_array = cJSON_CreateArray();
            int child_count = 0;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            while (child) {
                cJSON *child_json = process_element_for_rendering(child, depth + 1);
                if (child_json) {
                    cJSON_AddItemToArray(children_array, child_json);
                    child_count++;
                }
                child = lxb_dom_node_next(child);
            }
            
            if (child_count > 0) {
                cJSON_AddItemToObject(elem_json, "children", children_array);
            } else {
                cJSON_Delete(children_array);
            }
        }
        
        free(tag);
        return elem_json;
    }
    
    return NULL;
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
    while (*start && isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
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
    
    return result;
}


cJSON* get_global_computed_layout(void) {
    return global_computed_layout;
}

void set_global_computed_layout(cJSON *layout) {
    if (global_computed_layout) {
        cJSON_Delete(global_computed_layout);
    }
    global_computed_layout = layout;
}

void clear_global_computed_layout(void) {
    if (global_computed_layout) {
        cJSON_Delete(global_computed_layout);
        global_computed_layout = NULL;
    }
}


void merge_layout_with_element(cJSON *elem_json, cJSON *layout_data) {
    if (!elem_json || !layout_data) return;
    
    // ===== SKIP ENTIRELY FOR IFRAMES =====
    cJSON *is_iframe = cJSON_GetObjectItem(elem_json, "is_iframe");
    if (is_iframe && cJSON_IsTrue(is_iframe)) {
        // Don't override iframe dimensions with layout engine values
        cJSON_AddBoolToObject(elem_json, "layout_skipped_for_iframe", true);
        cJSON_AddBoolToObject(elem_json, "has_layout", false);
        return; // DON'T process iframes at all - keep their calculated dimensions
    }
    // ===== END IFRAME SKIP =====
    
    // Original code continues here for non-iframes...
    cJSON *tag_item = cJSON_GetObjectItem(elem_json, "tag");
    cJSON *id_item = cJSON_GetObjectItem(elem_json, "id");
    cJSON *class_item = cJSON_GetObjectItem(elem_json, "class");
    
    if (!tag_item || !cJSON_IsString(tag_item)) return;
    
    const char *tag = tag_item->valuestring;
    const char *element_id = id_item && cJSON_IsString(id_item) ? id_item->valuestring : "";
    const char *element_class = class_item && cJSON_IsString(class_item) ? class_item->valuestring : "";
    
    // Get the elements object from layout data
    cJSON *elements_obj = cJSON_GetObjectItem(layout_data, "elements");
    if (!elements_obj) return;
    
    // Try to find matching element in layout data
    cJSON *matched_element = NULL;
    
    // First try by ID
    if (strlen(element_id) > 0) {
        char id_key[256];
        snprintf(id_key, sizeof(id_key), "id_%s", element_id);
        
        cJSON *element_item = cJSON_GetObjectItem(elements_obj, id_key);
        if (element_item && cJSON_IsObject(element_item)) {
            matched_element = element_item;
        }
    }
    
    // If not found by ID, try to find by tag and approximate position
    if (!matched_element) {
        cJSON *element = NULL;
        cJSON_ArrayForEach(element, elements_obj) {
            if (!cJSON_IsObject(element)) continue;
            
            cJSON *layout_tag = cJSON_GetObjectItem(element, "tag");
            cJSON *layout_id = cJSON_GetObjectItem(element, "id");
            
            // Check if tag matches
            if (layout_tag && cJSON_IsString(layout_tag) && 
                strcasecmp(layout_tag->valuestring, tag) == 0) {
                
                // For body element, we have a specific key
                if (strcasecmp(tag, "body") == 0) {
                    matched_element = element;
                    break;
                }
                
                // For other elements, check if ID is empty (meaning it wasn't matched by ID)
                if (!layout_id || !cJSON_IsString(layout_id) || strlen(layout_id->valuestring) == 0) {
                    // Check class if available
                    cJSON *layout_class = cJSON_GetObjectItem(element, "className");
                    if (element_class && layout_class && cJSON_IsString(layout_class)) {
                        if (strstr(element_class, layout_class->valuestring) != NULL ||
                            strstr(layout_class->valuestring, element_class) != NULL) {
                            matched_element = element;
                            break;
                        }
                    } else {
                        // No class info, take the first matching tag
                        if (!matched_element) {
                            matched_element = element;
                        }
                    }
                }
            }
        }
    }
    
    // If we found a match, add the layout data
    if (matched_element) {
        cJSON *x = cJSON_GetObjectItem(matched_element, "x");
        cJSON *y = cJSON_GetObjectItem(matched_element, "y");
        cJSON *width = cJSON_GetObjectItem(matched_element, "width");
        cJSON *height = cJSON_GetObjectItem(matched_element, "height");
        cJSON *absoluteX = cJSON_GetObjectItem(matched_element, "absoluteX");
        cJSON *absoluteY = cJSON_GetObjectItem(matched_element, "absoluteY");
        cJSON *visible = cJSON_GetObjectItem(matched_element, "visible");
        cJSON *display = cJSON_GetObjectItem(matched_element, "display");
        
        if (x && cJSON_IsNumber(x)) {
            cJSON_AddNumberToObject(elem_json, "x", x->valuedouble);
        }
        if (y && cJSON_IsNumber(y)) {
            cJSON_AddNumberToObject(elem_json, "y", y->valuedouble);
        }
        if (width && cJSON_IsNumber(width)) {
            cJSON_AddNumberToObject(elem_json, "layout_width", width->valuedouble);
        }
        if (height && cJSON_IsNumber(height)) {
            cJSON_AddNumberToObject(elem_json, "layout_height", height->valuedouble);
        }
        if (absoluteX && cJSON_IsNumber(absoluteX)) {
            cJSON_AddNumberToObject(elem_json, "absolute_x", absoluteX->valuedouble);
        }
        if (absoluteY && cJSON_IsNumber(absoluteY)) {
            cJSON_AddNumberToObject(elem_json, "absolute_y", absoluteY->valuedouble);
        }
        if (visible && cJSON_IsBool(visible)) {
            cJSON_AddBoolToObject(elem_json, "visible", cJSON_IsTrue(visible));
        }
        if (display && cJSON_IsString(display)) {
            cJSON_AddStringToObject(elem_json, "computed_display", display->valuestring);
        }
        
        // Add a flag to indicate layout was applied
        cJSON_AddBoolToObject(elem_json, "has_layout", true);
    } else {
        // Mark that no layout was found for this element
        cJSON_AddBoolToObject(elem_json, "has_layout", false);
    }
}



int main(int argc, char *argv[]) {
    printf("=== HTML RENDERER OUTPUT GENERATOR ===\n");
    
    if (argc < 2) {
        printf("Usage: %s <input.html> [output.txt]\n", argv[0]);
        return 1;
    }
    
    const char *html_file = argv[1];
    char output_file[256];
    
    if (argc >= 3) {
        strncpy(output_file, argv[2], sizeof(output_file) - 1);
        output_file[sizeof(output_file) - 1] = '\0';
    } else {
        snprintf(output_file, sizeof(output_file), "%s.txt", html_file);
    }
    
    printf("Input: %s\n", html_file);
    printf("Output: %s\n", output_file);
    
    // ========== STEP 1: Parse HTML ==========
    printf("\n=== STEP 1: Parse HTML Document ===\n");
    
    FILE *f = fopen(html_file, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open HTML file: %s\n", html_file);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *html = malloc(size + 1);
    if (!html) {
        fclose(f);
        fprintf(stderr, "ERROR: Failed to allocate memory for HTML\n");
        return 1;
    }
    
    size_t read_size = fread(html, 1, size, f);
    html[read_size] = '\0';
    fclose(f);
    
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) {
        free(html);
        fprintf(stderr, "ERROR: Failed to create Lexbor document\n");
        return 1;
    }
    
    lexbor_status_t status = lxb_html_document_parse(doc, (lxb_char_t*)html, read_size);
    free(html);
    
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "ERROR: Lexbor parse failed with status: %d\n", status);
        lxb_html_document_destroy(doc);
        return 1;
    }
    
    printf("HTML parsed successfully\n");
    
    // ========== STEP 2: Initialize CSS Parser ==========
    printf("\n=== STEP 2: Initialize CSS Parser ===\n");
    if (!css_parser_init()) {
        printf("WARNING: CSS parser initialization failed (continuing without CSS)\n");
    } else {
        printf("CSS parser initialized\n");
    }
    
    // ========== STEP 2.5: Initialize Document Outline ==========
printf("\n=== STEP 2.5: Initialize Document Outline ===\n");
init_document_outline(&global_document_outline);
printf("Document outline tracker initialized\n");

    // ========== STEP 3: Initialize Event Handler ==========
    printf("\n=== STEP 3: Initialize Event Handler ===\n");
    event_handler_init();
    printf("Event handler initialized\n");
    
// ========== STEP 4: Initialize JavaScript (Optional) ==========
printf("\n=== STEP 4: Initialize JavaScript ===\n");
int enable_js = 1; // Set to 0 to disable JavaScript
JSContext *js_ctx = NULL;
cJSON *js_modifications = NULL;  // Add this variable

if (enable_js) {
    js_ctx = js_engine_init();
    if (js_ctx) {
        printf("JavaScript engine initialized\n");
        
        // Set security policy
        SecurityPolicy policy = js_default_security_policy();
        policy.max_memory_bytes = 32 * 1024 * 1024;
        policy.allow_dom_apis = 1;
        policy.enable_console = 1;
        js_set_security_policy(js_ctx, policy);
        
        // Execute scripts and calculate layout
        js_execute_script_elements(js_ctx, doc);

        // ========== GET JAVASCRIPT MODIFICATIONS ==========
        js_modifications = get_js_modifications();
        if (js_modifications) {
            // Calculate modification count and print in one pass
            int mod_count = 0;
            cJSON *element;
            
            printf("JavaScript modifications:\n");
            cJSON_ArrayForEach(element, js_modifications) {
                mod_count++;
                printf("  - Element '%s' modified by JS\n", element->string);
            }
            
            printf("Total: %d modifications\n", mod_count);
            
            // We'll store this in rendering_output LATER, after it's created
            // Just keep js_modifications for now
        } else {
            printf("No JavaScript modifications detected\n");
        }
        // ==================================================
        
        printf("JavaScript execution completed\n");
    } else {
        printf("WARNING: JavaScript engine initialization failed (continuing without JS)\n");
    }
} else {
    printf("JavaScript execution disabled\n");
}
    
    // ========== STEP 5: Calculate Layout ==========
    printf("\n=== STEP 5: Calculate Layout ===\n");
    cJSON *computed_layout = calculate_document_layout(doc);
    
    if (computed_layout) {
        printf("Layout calculated successfully\n");
        
        // Store globally for element processing
        if (global_computed_layout) {
            cJSON_Delete(global_computed_layout);
        }
        global_computed_layout = cJSON_Duplicate(computed_layout, 1);
        
        // Show layout summary
        cJSON *elements = cJSON_GetObjectItem(computed_layout, "elements");
        if (elements) {
            int count = cJSON_GetArraySize(elements);
            printf("Layout contains %d positioned elements\n", count);
        }
    } else {
        printf("WARNING: Layout calculation failed (using default positions)\n");
    }
    
    // ========== STEP 6: Process Events ==========
    printf("\n=== STEP 6: Process Events ===\n");
    lxb_dom_node_t *root = lxb_dom_interface_node(doc);
    process_events_recursive(root);
    int event_count = get_event_handler_count();
    printf("Found %d event handler(s)\n", event_count);
    
    // ========== STEP 7: Build Clean Rendering Output ==========
    printf("\n=== STEP 7: Build Rendering Output ===\n");
    
    // Create root array for rendering output
    cJSON *rendering_output = cJSON_CreateArray();
    
    // ===== JS MODIFICATIONS HERE (after creating rendering_output) =====
if (js_modifications) {
    cJSON_AddItemToObject(rendering_output, "js_modifications", 
                         cJSON_Duplicate(js_modifications, 1));
}
// ======================================================================

    // Get DOM document
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc);
    lxb_dom_element_t *doc_element = lxb_dom_document_element(dom_doc);
    
    // Find body element
    lxb_dom_element_t *body = NULL;
    lxb_dom_collection_t *body_coll = lxb_dom_collection_make(dom_doc, 1);
    if (body_coll) {
        lxb_dom_elements_by_tag_name(doc_element, body_coll, (lxb_char_t*)"body", 4);
        size_t body_count = lxb_dom_collection_length(body_coll);
        
        if (body_count > 0) {
            body = lxb_dom_collection_element(body_coll, 0);
            printf("Found body element\n");
        }
        
        lxb_dom_collection_destroy(body_coll, true);
    }
    
    if (body) {
        // Create body wrapper for rendering
        cJSON *body_json = cJSON_CreateObject();
        
        // Basic body properties
        cJSON_AddStringToObject(body_json, "type", "block");
        cJSON_AddStringToObject(body_json, "tag", "body");
        cJSON_AddStringToObject(body_json, "bg_color", "#ffffff");
        cJSON_AddNumberToObject(body_json, "x", 0);
        cJSON_AddNumberToObject(body_json, "y", 0);
        cJSON_AddStringToObject(body_json, "width", "100%");
        cJSON_AddStringToObject(body_json, "height", "auto");
        
        // Get body layout if available
        if (global_computed_layout) {
            cJSON *elements = cJSON_GetObjectItem(global_computed_layout, "elements");
            if (elements) {
                cJSON *body_layout = cJSON_GetObjectItem(elements, "body");
                if (body_layout) {
                    cJSON *width = cJSON_GetObjectItem(body_layout, "width");
                    cJSON *height = cJSON_GetObjectItem(body_layout, "height");
                    
                    if (width && cJSON_IsNumber(width)) {
                        char width_str[32];
                        snprintf(width_str, sizeof(width_str), "%dpx", (int)width->valuedouble);
                        if (cJSON_HasObjectItem(body_json, "width")) {
                            cJSON_ReplaceItemInObject(body_json, "width", cJSON_CreateString(width_str));
                        } else {
                            cJSON_AddStringToObject(body_json, "width", width_str);
                        }
                    }
                    
                    if (height && cJSON_IsNumber(height)) {
                        char height_str[32];
                        snprintf(height_str, sizeof(height_str), "%dpx", (int)height->valuedouble);
                        if (cJSON_HasObjectItem(body_json, "height")) {
                            cJSON_ReplaceItemInObject(body_json, "height", cJSON_CreateString(height_str));
                        } else {
                            cJSON_AddStringToObject(body_json, "height", height_str);
                        }
                    }
                }
            }
        }
        
        // Process body children
        lxb_dom_node_t *body_node = lxb_dom_interface_node(body);
        cJSON *children_array = cJSON_CreateArray();
        int child_count = 0;
        
        // Process immediate children of body
        lxb_dom_node_t *child = lxb_dom_node_first_child(body_node);
        while (child) {
            cJSON *child_json = process_element_for_rendering(child, 0);
            if (child_json) {
                cJSON_AddItemToArray(children_array, child_json);
                child_count++;
            }
            child = lxb_dom_node_next(child);
        }
        
        printf("Processed %d body children for rendering\n", child_count);
        
        if (child_count > 0) {
            cJSON_AddItemToObject(body_json, "children", children_array);
        } else {
            cJSON_Delete(children_array);
        }
        
        cJSON_AddItemToArray(rendering_output, body_json);
    } else {
        printf("ERROR: No body element found\n");
        
        // Create empty body as fallback
        cJSON *empty_body = cJSON_CreateObject();
        cJSON_AddStringToObject(empty_body, "type", "block");
        cJSON_AddStringToObject(empty_body, "tag", "body");
        cJSON_AddStringToObject(empty_body, "bg_color", "#ffffff");
        cJSON_AddNumberToObject(empty_body, "x", 0);
        cJSON_AddNumberToObject(empty_body, "y", 0);
        cJSON_AddStringToObject(empty_body, "width", "800px");
        cJSON_AddStringToObject(empty_body, "height", "600px");
        cJSON_AddItemToArray(rendering_output, empty_body);
    }
    // ========== EXTRACT TABLES TO SEPARATE FILES ==========
    printf("\n=== EXTRACTING TABLES TO SEPARATE FILES ===\n");

    int table_count = get_table_count();  // Use getter
    
    if (table_count > 0) {
        printf("Found %d table(s) to extract\n", table_count);
        
        // Create array for table references in the main output
        cJSON *table_references = cJSON_CreateArray();
        
        for (int i = 0; i < table_count; i++) {  // Use getter here
            const char *filename = get_table_filename(i);  // Get filename using getter
            lxb_dom_element_t *table_elem = get_table_element(i);  // Get element using getter
            
            if (!filename || !table_elem) continue;  // Safety check
            
            printf("Processing table %d: %s\n", i + 1, filename);  // Use filename variable
            
            // Extract table structure
            cJSON *table_json = extract_table_structure(table_elem);  // Use table_elem variable
            if (table_json) {
                cJSON_AddNumberToObject(table_json, "table_index", i + 1);
                cJSON_AddStringToObject(table_json, "source_file", filename);

// Write table to separate file
char table_filepath[512];
// Get directory from output_file
char *last_slash = strrchr(output_file, '/');
if (last_slash) {
    // Extract directory
    size_t dir_len = last_slash - output_file + 1;
    char dir[256];
    strncpy(dir, output_file, dir_len);
    dir[dir_len] = '\0';
    snprintf(table_filepath, sizeof(table_filepath), 
            "%s%s", dir, filename);  // <-- USE filename variable
} else {
    // No directory, just filename
    snprintf(table_filepath, sizeof(table_filepath), 
            "%s", filename);  // <-- USE filename variable
}

FILE *table_file = fopen(table_filepath, "wb");
if (table_file) {
    char *table_json_str = cJSON_Print(table_json);
    if (table_json_str) {
        fwrite(table_json_str, 1, strlen(table_json_str), table_file);
        free(table_json_str);
        printf("  Written to: %s\n", table_filepath);
    }
    fclose(table_file);
    kopiraj_fajl(table_filepath);
}
            
            // Create reference for main output
            cJSON *table_ref = cJSON_CreateObject();
            cJSON_AddStringToObject(table_ref, "type", "table_ref");
            cJSON_AddStringToObject(table_ref, "table_file", filename);
            cJSON_AddNumberToObject(table_ref, "table_index", i + 1);
            
            // Add estimated dimensions if available
            cJSON *est_width = cJSON_GetObjectItem(table_json, "estimated_width");
            cJSON *est_height = cJSON_GetObjectItem(table_json, "estimated_height");
            if (est_width && cJSON_IsNumber(est_width)) {
                cJSON_AddNumberToObject(table_ref, "estimated_width", est_width->valueint);
            }
            if (est_height && cJSON_IsNumber(est_height)) {
                cJSON_AddNumberToObject(table_ref, "estimated_height", est_height->valueint);
            }
            
            cJSON_AddItemToArray(table_references, table_ref);
            
            cJSON_Delete(table_json);
        }
    }
    
    // Add table references to the main rendering output
    if (cJSON_GetArraySize(table_references) > 0) {
        // Create a wrapper object for table references
        cJSON *tables_wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(tables_wrapper, "type", "tables_container");
        cJSON_AddStringToObject(tables_wrapper, "tag", "div");
        cJSON_AddItemToObject(tables_wrapper, "table_references", table_references);
        cJSON_AddNumberToObject(tables_wrapper, "table_count", cJSON_GetArraySize(table_references));
        
        // Add to rendering output
        cJSON_AddItemToArray(rendering_output, tables_wrapper);
        
        printf("Added %d table references to output\n", cJSON_GetArraySize(table_references));
    } else {
        cJSON_Delete(table_references);
    }
    

    
} else {
    printf("No tables found in document\n");
}
// ========== END TABLE EXTRACTION ==========

// ========== EXTRACT FORMS TO SEPARATE FILES ==========
printf("\n=== EXTRACTING FORMS TO SEPARATE FILES ===\n");

int form_count = get_form_count();  // Use getter

if (form_count > 0) {
    printf("Found %d form(s) to extract\n", form_count);
    
    // Create array for form references in the main output
    cJSON *form_references = cJSON_CreateArray();
    
    for (int i = 0; i < form_count; i++) {
        const char *filename = get_form_filename(i);
        lxb_dom_element_t *form_elem = get_form_element(i);
        
        if (!filename || !form_elem) continue;
        
        printf("Processing form %d: %s\n", i + 1, filename);
        
        // Extract form structure
        cJSON *form_json = extract_form_structure(form_elem);
        if (form_json) {
            cJSON_AddNumberToObject(form_json, "form_index", i + 1);
            cJSON_AddStringToObject(form_json, "source_file", filename);
            
            // Write form to separate file (same pattern as tables)
            char form_filepath[512];
            // Get directory from output_file
            char *last_slash = strrchr(output_file, '/');
            if (last_slash) {
                size_t dir_len = last_slash - output_file + 1;
                char dir[256];
                strncpy(dir, output_file, dir_len);
                dir[dir_len] = '\0';
                snprintf(form_filepath, sizeof(form_filepath), "%s%s", dir, filename);
            } else {
                snprintf(form_filepath, sizeof(form_filepath), "%s", filename);
            }
            
            FILE *form_file = fopen(form_filepath, "wb");
            if (form_file) {
                char *form_json_str = cJSON_Print(form_json);
                if (form_json_str) {
                    fwrite(form_json_str, 1, strlen(form_json_str), form_file);
                    free(form_json_str);
                    printf("  Written to: %s\n", form_filepath);
                }
                fclose(form_file);
                kopiraj_fajl(form_filepath);
            }
            
            // Create reference for main output
            cJSON *form_ref = cJSON_CreateObject();
            cJSON_AddStringToObject(form_ref, "type", "form_ref");
            cJSON_AddStringToObject(form_ref, "form_file", filename);
            cJSON_AddNumberToObject(form_ref, "form_index", i + 1);
            
            // Add placeholder dimensions
            cJSON_AddNumberToObject(form_ref, "estimated_width", 400);
            cJSON_AddNumberToObject(form_ref, "estimated_height", 200);
            
            cJSON_AddItemToArray(form_references, form_ref);
            
            cJSON_Delete(form_json);
        }
    }
    
    // Add form references to rendering output
    if (cJSON_GetArraySize(form_references) > 0) {
        cJSON *forms_wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(forms_wrapper, "type", "forms_container");
        cJSON_AddStringToObject(forms_wrapper, "tag", "div");
        cJSON_AddItemToObject(forms_wrapper, "form_references", form_references);
        cJSON_AddNumberToObject(forms_wrapper, "form_count", cJSON_GetArraySize(form_references));
        
        cJSON_AddItemToArray(rendering_output, forms_wrapper);
        printf("Added %d form references to output\n", cJSON_GetArraySize(form_references));
    } else {
        cJSON_Delete(form_references);
    }
} else {
    printf("No forms found in document\n");
}
// ========== END FORM EXTRACTION ==========


// ========== EXTRACT LISTS TO SEPARATE FILES ==========
printf("\n=== EXTRACTING LISTS TO SEPARATE FILES ===\n");

int list_count = get_list_count();

if (list_count > 0) {
    printf("Found %d list(s) to extract\n", list_count);
    
    // Create array for list references in the main output
    cJSON *list_references = cJSON_CreateArray();
    
    for (int i = 0; i < list_count; i++) {
        const char *filename = get_list_filename(i);
        lxb_dom_element_t *list_elem = get_list_element(i);
        const char *list_type = get_list_type(i);
        
        if (!filename || !list_elem || !list_type) continue;
        
        printf("Processing list %d: %s (%s)\n", i + 1, filename, list_type);
        
        // Extract list structure
        cJSON *list_json = extract_list_structure(list_elem);
        if (list_json) {
            cJSON_AddNumberToObject(list_json, "list_index", i + 1);
            cJSON_AddStringToObject(list_json, "source_file", filename);
          //  cJSON_AddStringToObject(list_json, "list_type", list_type);
            
            // Write list to separate file
            char list_filepath[512];
            // Get directory from output_file
            char *last_slash = strrchr(output_file, '/');
            if (last_slash) {
                size_t dir_len = last_slash - output_file + 1;
                char dir[256];
                strncpy(dir, output_file, dir_len);
                dir[dir_len] = '\0';
                snprintf(list_filepath, sizeof(list_filepath), "%s%s", dir, filename);
            } else {
                snprintf(list_filepath, sizeof(list_filepath), "%s", filename);
            }
            
            FILE *list_file = fopen(list_filepath, "wb");
            if (list_file) {
                char *list_json_str = cJSON_Print(list_json);
                if (list_json_str) {
                    fwrite(list_json_str, 1, strlen(list_json_str), list_file);
                    free(list_json_str);
                    printf("  Written to: %s\n", list_filepath);
                }
                fclose(list_file);
                kopiraj_fajl(filename);
            } else {
                printf("  ERROR: Could not create list file: %s\n", list_filepath);
            }
            
            // Create reference for main output
            cJSON *list_ref = cJSON_CreateObject();
            cJSON_AddStringToObject(list_ref, "type", "list_ref");
            cJSON_AddStringToObject(list_ref, "list_file", filename);
            cJSON_AddStringToObject(list_ref, "list_type", list_type);
            cJSON_AddNumberToObject(list_ref, "list_index", i + 1);
            
            // Add estimated dimensions if available
            cJSON *est_width = cJSON_GetObjectItem(list_json, "estimated_width");
            cJSON *est_height = cJSON_GetObjectItem(list_json, "estimated_height");
            if (est_width && cJSON_IsNumber(est_width)) {
                cJSON_AddNumberToObject(list_ref, "estimated_width", est_width->valueint);
            }
            if (est_height && cJSON_IsNumber(est_height)) {
                cJSON_AddNumberToObject(list_ref, "estimated_height", est_height->valueint);
            }
            
            // Add item count
            cJSON *item_count = cJSON_GetObjectItem(list_json, "item_count");
            if (item_count && cJSON_IsNumber(item_count)) {
                cJSON_AddNumberToObject(list_ref, "item_count", item_count->valueint);
            }
            
            cJSON_AddItemToArray(list_references, list_ref);
            
            cJSON_Delete(list_json);
        } else {
            printf("  WARNING: Failed to extract list structure\n");
        }
    }
    
    // Add list references to the main rendering output
    if (cJSON_GetArraySize(list_references) > 0) {
        // Create a wrapper object for list references
        cJSON *lists_wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(lists_wrapper, "type", "lists_container");
        cJSON_AddStringToObject(lists_wrapper, "tag", "div");
        cJSON_AddItemToObject(lists_wrapper, "list_references", list_references);
        cJSON_AddNumberToObject(lists_wrapper, "list_count", cJSON_GetArraySize(list_references));
        
        // Add to rendering output
        cJSON_AddItemToArray(rendering_output, lists_wrapper);
        
        printf("Added %d list references to output\n", cJSON_GetArraySize(list_references));
    } else {
        cJSON_Delete(list_references);
    }
    
} else {
    printf("No lists found in document\n");
}
// ========== END LIST EXTRACTION ==========

// ========== EXTRACT MENUS TO SEPARATE FILES ==========
printf("\n=== EXTRACTING MENUS TO SEPARATE FILES ===\n");

int menu_count = get_menu_count();

if (menu_count > 0) {
    printf("Found %d menu(s) to extract\n", menu_count);
    
    // Create array for menu references in the main output
    cJSON *menu_references = cJSON_CreateArray();
    
    for (int i = 0; i < menu_count; i++) {
        const char *filename = get_menu_filename(i);
        lxb_dom_element_t *menu_elem = get_menu_element(i);
        const char *menu_type = get_menu_type(i);
        
        if (!filename || !menu_elem || !menu_type) continue;
        
        printf("Processing menu %d: %s (%s)\n", i + 1, filename, menu_type);
        
        // Extract menu structure
        cJSON *menu_json = extract_menu_structure(menu_elem);
        if (menu_json) {
            cJSON_AddNumberToObject(menu_json, "menu_index", i + 1);
            cJSON_AddStringToObject(menu_json, "source_file", filename);
          //  cJSON_AddStringToObject(menu_json, "menu_type", menu_type);
            
            // ========== ADD: LINK TO LISTS ==========
            // Create array for linked list files
            cJSON *contains_lists = cJSON_CreateArray();
            int linked_list_count = 0;
            
            // Check all direct children of this menu element
            lxb_dom_node_t *menu_node = lxb_dom_interface_node(menu_elem);
            lxb_dom_node_t *child = lxb_dom_node_first_child(menu_node);
            
            while (child) {
                if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
                    
                    // Search through all extracted lists to see if this is one
                    for (int list_idx = 0; list_idx < get_list_count(); list_idx++) {
                        lxb_dom_element_t *list_elem = get_list_element(list_idx);
                        if (list_elem == child_elem) {
                            // Found a matching list!
                            const char *list_filename = get_list_filename(list_idx);
                            if (list_filename) {
                                cJSON_AddItemToArray(contains_lists, 
                                                   cJSON_CreateString(list_filename));
                                linked_list_count++;
                                printf("  Linked to list: %s\n", list_filename);
                            }
                            break; // Found it, no need to check other lists
                        }
                    }
                }
                child = lxb_dom_node_next(child);
            }
            
            // Add the linked lists array to menu JSON if we found any
            if (linked_list_count > 0) {
                cJSON_AddItemToObject(menu_json, "contains_lists", contains_lists);
                cJSON_AddNumberToObject(menu_json, "linked_list_count", linked_list_count);
            } else {
                cJSON_Delete(contains_lists);
            }
            // ========== END LIST LINKING ==========
            
            // Write menu to separate file
            char menu_filepath[512];
            char *last_slash = strrchr(output_file, '/');
            if (last_slash) {
                size_t dir_len = last_slash - output_file + 1;
                char dir[256];
                strncpy(dir, output_file, dir_len);
                dir[dir_len] = '\0';
                snprintf(menu_filepath, sizeof(menu_filepath), "%s%s", dir, filename);
            } else {
                snprintf(menu_filepath, sizeof(menu_filepath), "%s", filename);
            }
            
            FILE *menu_file = fopen(menu_filepath, "wb");
            if (menu_file) {
                char *menu_json_str = cJSON_Print(menu_json);
                if (menu_json_str) {
                    fwrite(menu_json_str, 1, strlen(menu_json_str), menu_file);
                    free(menu_json_str);
                    printf("  Written to: %s\n", menu_filepath);
                }
                fclose(menu_file);
                 kopiraj_fajl(filename); 
            } else {
                printf("  ERROR: Could not create menu file: %s\n", menu_filepath);
            }
            
            // Create reference for main output
            cJSON *menu_ref = cJSON_CreateObject();
            cJSON_AddStringToObject(menu_ref, "type", "menu_ref");
            cJSON_AddStringToObject(menu_ref, "menu_file", filename);
            cJSON_AddStringToObject(menu_ref, "menu_type", menu_type);
            cJSON_AddNumberToObject(menu_ref, "menu_index", i + 1);
            
            // ========== ALSO ADD TO MENU REFERENCE ==========
            // Add linked lists to the menu reference in main output
            if (linked_list_count > 0) {
                cJSON *ref_lists = cJSON_CreateArray();
                cJSON *list_array = cJSON_GetObjectItem(menu_json, "contains_lists");
                if (list_array && cJSON_IsArray(list_array)) {
                    cJSON *list_item;
                    cJSON_ArrayForEach(list_item, list_array) {
                        if (cJSON_IsString(list_item)) {
                            cJSON_AddItemToArray(ref_lists, 
                                               cJSON_CreateString(list_item->valuestring));
                        }
                    }
                    cJSON_AddItemToObject(menu_ref, "contains_lists", ref_lists);
                } else {
                    cJSON_Delete(ref_lists);
                }
            }
            // ========== END ADD TO REFERENCE ==========
            
            // Add estimated dimensions
            cJSON *est_width = cJSON_GetObjectItem(menu_json, "estimated_width");
            cJSON *est_height = cJSON_GetObjectItem(menu_json, "estimated_height");
            if (est_width && cJSON_IsNumber(est_width)) {
                cJSON_AddNumberToObject(menu_ref, "estimated_width", est_width->valueint);
            }
            if (est_height && cJSON_IsNumber(est_height)) {
                cJSON_AddNumberToObject(menu_ref, "estimated_height", est_height->valueint);
            }
            
            // Add item count
            cJSON *item_count = cJSON_GetObjectItem(menu_json, "item_count");
            if (item_count && cJSON_IsNumber(item_count)) {
                cJSON_AddNumberToObject(menu_ref, "item_count", item_count->valueint);
            }
            
            cJSON_AddItemToArray(menu_references, menu_ref);
            
            cJSON_Delete(menu_json);
        } else {
            printf("  WARNING: Failed to extract menu structure\n");
        }
    }
    
    // Add menu references to the main rendering output
    if (cJSON_GetArraySize(menu_references) > 0) {
        // Create a wrapper object for menu references
        cJSON *menus_wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(menus_wrapper, "type", "menus_container");
        cJSON_AddStringToObject(menus_wrapper, "tag", "div");
        cJSON_AddItemToObject(menus_wrapper, "menu_references", menu_references);
        cJSON_AddNumberToObject(menus_wrapper, "menu_count", cJSON_GetArraySize(menu_references));
        
        // Add to rendering output
        cJSON_AddItemToArray(rendering_output, menus_wrapper);
        
        printf("Added %d menu references to output\n", cJSON_GetArraySize(menu_references));
    } else {
        cJSON_Delete(menu_references);
    }
    
} else {
    printf("No menus found in document\n");
}

    // ========== STEP 8: Write Output ==========
    printf("\n=== STEP 8: Write Rendering Output ===\n");
    FILE *out_file = fopen(output_file, "wb");
    if (out_file) {
        char *json_str = cJSON_Print(rendering_output);
        if (json_str) {
            size_t written = fwrite(json_str, 1, strlen(json_str), out_file);
            printf("Wrote %zu bytes to %s\n", written, output_file);
            free(json_str);
        } else {
            printf("ERROR: Failed to generate JSON string\n");
        }
        fclose(out_file);
        kopiraj_fajl(output_file);
    } else {
        printf("ERROR: Could not open output file %s\n", output_file);
        
        // Print to console as fallback
        char *json_str = cJSON_Print(rendering_output);
        if (json_str) {
            printf("\nRendering Output:\n%s\n", json_str);
            free(json_str);
        }
    }
    
// ========== STEP 8.5: Calculate Positions with Lua ==========
printf("\n=== STEP 8.5: Calculate Element Positions with Lua ===\n");

// Initialize Lua calculator
lua_State* L = init_lua_position_calculator("position_calculator.lua");
if (L) {
    printf("✓ Lua initialized successfully\n");
    
    
    // Create configuration matching your viewport
    LuaLayoutConfig config = {
        .viewport_width = 800,
        .viewport_height = 600,
        .default_font_size = 16,
        .line_height_multiplier = 1.2,
        .margin_between_elements = 15,  // Increased for better spacing
        .padding_inside_elements = 8
    };
    
    printf("Calling Lua to calculate positions...\n");
    printf("Main output has %d top-level elements\n", cJSON_GetArraySize(rendering_output));
    
    // Calculate positions using Lua
    cJSON* lua_output = calculate_positions_lua(L, rendering_output, &config);
    
    if (lua_output) {
        printf("✓ Lua calculation succeeded!\n");
        
        // Debug: Print Lua output summary
        cJSON* success = cJSON_GetObjectItem(lua_output, "success");
        cJSON* message = cJSON_GetObjectItem(lua_output, "message");
        cJSON* element_count = cJSON_GetObjectItem(lua_output, "element_count");
        
        if (success && cJSON_IsTrue(success) && message) {
            printf("Lua: %s\n", message->valuestring);
        }
        
        if (element_count && cJSON_IsNumber(element_count)) {
            printf("Lua positioned %d elements\n", (int)element_count->valuedouble);
        }
        
        cJSON* elements = cJSON_GetObjectItem(lua_output, "elements");
        if (elements && cJSON_IsArray(elements)) {
            int count = cJSON_GetArraySize(elements);
            printf("Lua elements array has %d items\n", count);
            
            // Print first 15 elements for debugging
            int print_limit = count < 15 ? count : 15;
            printf("First %d positioned elements:\n", print_limit);
            
            for (int i = 0; i < print_limit; i++) {
                cJSON* elem = cJSON_GetArrayItem(elements, i);
                if (elem) {
                    cJSON* tag = cJSON_GetObjectItem(elem, "tag");
                    cJSON* id = cJSON_GetObjectItem(elem, "id");
                    cJSON* type = cJSON_GetObjectItem(elem, "type");
                    cJSON* x = cJSON_GetObjectItem(elem, "x");
                    cJSON* y = cJSON_GetObjectItem(elem, "y");
                    cJSON* width = cJSON_GetObjectItem(elem, "calculated_width");
                    cJSON* height = cJSON_GetObjectItem(elem, "calculated_height");
                    
                    if (tag && x && y) {
                        printf("  [%2d] %-8s", i + 1, tag->valuestring);
                        if (id && cJSON_IsString(id) && strlen(id->valuestring) > 0) {
                            printf(" #%s", id->valuestring);
                        }
                        if (type && cJSON_IsString(type)) {
                            printf(" (%s)", type->valuestring);
                        }
                        printf(" at (%4.0f, %4.0f)", x->valuedouble, y->valuedouble);
                        if (width && height) {
                            printf(" %4.0fx%-4.0f", width->valuedouble, height->valuedouble);
                        }
                        printf("\n");
                    }
                }
            }
            
            if (count > print_limit) {
                printf("  ... and %d more elements\n", count - print_limit);
            }
        } else {
            printf("⚠ Warning: No 'elements' array in Lua output\n");
            // Debug what we actually got
            char* debug_str = cJSON_Print(lua_output);
            if (debug_str) {
                printf("Lua output (first 500 chars):\n%.500s%s\n", 
                       debug_str, strlen(debug_str) > 500 ? "..." : "");
                free(debug_str);
            }
        }
        
        // Merge Lua positions into main output
        printf("\nMerging Lua positions into main output...\n");
        merge_lua_positions_fixed(rendering_output, lua_output);
        
        // Count how many elements got positions
        int positioned_count = 0;
        if (cJSON_IsArray(rendering_output)) {
            for (int i = 0; i < cJSON_GetArraySize(rendering_output); i++) {
                cJSON* elem = cJSON_GetArrayItem(rendering_output, i);
                if (elem && cJSON_HasObjectItem(elem, "calculated_width")) {
                    positioned_count++;
                }
            }
        }
        printf("✓ Merged complete. %d elements now have calculated dimensions.\n", positioned_count);
        
        // Save merged output
        char merged_file[512];
        snprintf(merged_file, sizeof(merged_file), "%s_with_positions.json", output_file);
        
        FILE* merged_f = fopen(merged_file, "wb");
        if (merged_f) {
            char* merged_json = cJSON_Print(rendering_output);
            if (merged_json) {
                size_t written = fwrite(merged_json, 1, strlen(merged_json), merged_f);
                printf("✓ Saved merged output to: %s (%zu bytes)\n", merged_file, written);
                free(merged_json);
                kopiraj_fajl(merged_file);
            } else {
                printf("⚠ Failed to generate JSON for merged output\n");
            }
            fclose(merged_f);
        } else {
            printf("⚠ Could not create file: %s\n", merged_file);
        }
        
        cJSON_Delete(lua_output);
    } else {
        printf("✗ Lua calculation failed!\n");
        printf("  Check if position_calculator.lua exists in current directory\n");
        printf("  Or if Lua function 'calculate_positions' is defined in the script\n");
    }
    
    // Cleanup Lua
    cleanup_lua_position_calculator(L);
    printf("✓ Lua cleaned up\n");
} else {
    printf("✗ Lua initialization failed!\n");
    printf("  Possible causes:\n");
    printf("  1. position_calculator.lua file not found\n");
    printf("  2. Lua library not linked correctly\n");
    printf("  3. Script has syntax errors\n");
}

printf("=== STEP 8.5 COMPLETE ===\n");

    // ========== STEP 10: Cleanup ==========
    printf("\n=== STEP 10: Cleanup ===\n");
    
    cleanup_tables_storage();

//ocisti forme
cleanup_forms_storage();

//ocisti liste
cleanup_lists_storage();

//ocisti meni
cleanup_menus_storage();


    // Cleanup JavaScript
    if (js_ctx) {
        js_engine_cleanup(js_ctx);
        printf("JavaScript engine cleaned up\n");
    }
    
    // Cleanup layout data
    if (global_computed_layout) {
        cJSON_Delete(global_computed_layout);
        global_computed_layout = NULL;
    }
    
    if (computed_layout) {
        cJSON_Delete(computed_layout);
    }
    
    // Cleanup JSON
    cJSON_Delete(rendering_output);
    
    // Cleanup systems
    event_handler_cleanup();
    css_parser_cleanup();
    
    // Cleanup document
    lxb_html_document_destroy(doc);
    
    printf("\n=== PROCESSING COMPLETE ===\n");
    printf("Clean rendering output written to: %s\n", output_file);
    
    return 0;
}

