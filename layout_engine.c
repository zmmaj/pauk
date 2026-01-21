#include "layout_engine.h"
#include "css_parser.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

// Default styles for HTML elements
static const char* get_default_display(const char *tag_name) {
    if (!tag_name) return "inline";
    
    // Block elements
    const char *block_elements[] = {
        "div", "p", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "table", "form", "header", "footer",
        "section", "article", "aside", "nav", "main", "figure",
        "figcaption", "blockquote", "hr", "pre", 
        "textarea", "select",
        NULL
    };
    
    for (int i = 0; block_elements[i] != NULL; i++) {
        if (strcasecmp(tag_name, block_elements[i]) == 0) {
            return "block";
        }
    }
    
    // Inline-block elements (form controls)
    const char *inline_block_elements[] = {
        "input", "button", 
        NULL
    };
    
    for (int i = 0; inline_block_elements[i] != NULL; i++) {
        if (strcasecmp(tag_name, inline_block_elements[i]) == 0) {
            return "inline-block";
        }
    }
    
    // Inline elements (your existing list)
    const char *inline_elements[] = {
        "span", "a", "strong", "em", "i", "b", "u", "code",
        "mark", "small", "sub", "sup", "time", "abbr", "cite",
        "dfn", "kbd", "samp", "var", "bdi", "bdo", "br",
        "wbr", "img", "label", // "img" already here
        NULL
    };
    
    for (int i = 0; inline_elements[i] != NULL; i++) {
        if (strcasecmp(tag_name, inline_elements[i]) == 0) {
            return "inline";
        }
    }
    
    return "block"; // Default to block
}

static double get_default_font_size(const char *tag_name) {
    if (!tag_name) return 16.0;
    
    if (strcasecmp(tag_name, "h1") == 0) return 32.0;
    if (strcasecmp(tag_name, "h2") == 0) return 24.0;
    if (strcasecmp(tag_name, "h3") == 0) return 18.7;
    if (strcasecmp(tag_name, "h4") == 0) return 16.0;
    if (strcasecmp(tag_name, "h5") == 0) return 13.3;
    if (strcasecmp(tag_name, "h6") == 0) return 10.7;
    
    return 16.0; // Default
}

const char* get_css_property(lxb_dom_element_t *element, 
                             const char *property_name,
                             const char *default_value) {
    // First check inline style
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(element, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        size_t style_len;
        const lxb_char_t *style_value = lxb_dom_attr_value(style_attr, &style_len);
        
        if (style_value && style_len > 0) {
            char *style_str = malloc(style_len + 1);
            if (style_str) {
                memcpy(style_str, style_value, style_len);
                style_str[style_len] = '\0';
                
                // Simple parsing - look for property
                char *pos = strstr(style_str, property_name);
                if (pos) {
                    pos += strlen(property_name);
                    while (*pos && (*pos == ' ' || *pos == ':')) pos++;
                    
                    char *end = pos;
                    while (*end && *end != ';' && *end != '!') end++;
                    
                    if (end > pos) {
                        char *value = malloc(end - pos + 1);
                        if (value) {
                            memcpy(value, pos, end - pos);
                            value[end - pos] = '\0';
                            
                            // Trim
                            char *start = value;
                            while (*start == ' ') start++;
                            char *val_end = start + strlen(start) - 1;
                            while (val_end > start && (*val_end == ' ' || *val_end == ';')) {
                                *val_end = '\0';
                                val_end--;
                            }
                            
                            if (strlen(start) > 0) {
                                free(style_str);
                                return value; // Caller must free
                            }
                            free(value);
                        }
                    }
                }
                free(style_str);
            }
        }
    }
    
    // Return default
    return strdup(default_value ? default_value : "");
}

ComputedStyle compute_element_style(lxb_dom_element_t *element, 
                                    ComputedStyle *parent_style) {
    ComputedStyle style = {0};
    
    // Get tag name
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(element, &len);
    char *tag = NULL;
    if (tag_name && len > 0) {
        tag = malloc(len + 1);
        memcpy(tag, tag_name, len);
        tag[len] = '\0';
    }
    
    // Default values
    style.font_size = get_default_font_size(tag);
    style.line_height = style.font_size * 1.2;
    style.display = get_default_display(tag);
    style.position = "static";
    
    // Get CSS properties
    char *display = (char*)get_css_property(element, "display", style.display);
    if (display && strlen(display) > 0) {
        style.display = strdup(display);
    }
    free(display);
    
    char *font_size = (char*)get_css_property(element, "font-size", NULL);
    if (font_size && strlen(font_size) > 0) {
        // Parse font size (simplified)
        char *endptr;
        double size = strtod(font_size, &endptr);
        if (endptr != font_size) {
            if (strstr(font_size, "px") || strlen(endptr) == 0) {
                style.font_size = size;
            } else if (strstr(font_size, "em") && parent_style) {
                style.font_size = size * parent_style->font_size;
            } else if (strstr(font_size, "rem")) {
                style.font_size = size * 16.0; // Assume root font size = 16px
            } else if (strstr(font_size, "%") && parent_style) {
                style.font_size = (size / 100.0) * parent_style->font_size;
            }
        }
    }
    free(font_size);
    
    // Parse margins and paddings (simplified)
    style.margin_top = style.margin_bottom = style.margin_left = style.margin_right = 0;
    style.padding_top = style.padding_bottom = style.padding_left = style.padding_right = 0;
    
    if (tag) free(tag);
    return style;
}

LayoutBox calculate_element_layout(lxb_dom_element_t *element, 
    double parent_x, double parent_y,
    double parent_width, double parent_height,
    ComputedStyle *parent_style) {
    
    LayoutBox box = {0};
    
    // ========== CRITICAL: NULL CHECK ==========
    if (!element) {
        printf("[LAYOUT ERROR] NULL element passed to calculate_element_layout()\n");
        return box;
    }
    
    printf("[LAYOUT DEBUG] calculate_element_layout called for element: %p\n", (void*)element);
    
    ComputedStyle style = compute_element_style(element, parent_style);
    
    // Get element info
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(element, &len);
    char *tag = NULL;
    
    if (tag_name && len > 0) {
        tag = malloc(len + 1);
        if (!tag) {
            printf("[LAYOUT ERROR] Failed to allocate memory for tag\n");
            return box;
        }
        memcpy(tag, tag_name, len);
        tag[len] = '\0';
        printf("[LAYOUT DEBUG] Element tag: %s\n", tag);
    } else {
        printf("[LAYOUT ERROR] No tag name for element\n");
        return box;
    }
    
    // Calculate content box
    double content_width = parent_width - style.margin_left - style.margin_right
                         - style.padding_left - style.padding_right;
    
    // Ensure content_width is not negative
    if (content_width < 0) {
        content_width = 100; // Minimum width
        printf("[LAYOUT WARNING] Negative content width, using %f\n", content_width);
    }
    
    // ========== FORM ELEMENT SPECIAL HANDLING ==========
    if (tag && strcasecmp(tag, "form") == 0) {
        printf("[LAYOUT DEBUG] Processing FORM element\n");
        box.width = fmax(400, content_width);
        box.height = 500; // Default form height (will be adjusted based on children)
        box.x = parent_x + style.margin_left;
        box.y = parent_y + style.margin_top;
        
        printf("[LAYOUT DEBUG] Form box: (%.1f, %.1f) %.1fx%.1f\n", 
               box.x, box.y, box.width, box.height);
        
        free(tag);
        return box;
    }
    
    // Default sizes based on element type
    if (strcasecmp(style.display, "block") == 0) {
        printf("[LAYOUT DEBUG] Block element: %s\n", tag);
        box.width = fmax(100, content_width); // Minimum 100px
        box.height = style.line_height;
        
        // ========== SAFE TEXT EXTRACTION ==========
        // Only get text for elements that can have text content
        char *text = NULL;
        int should_get_text = 1;
        
        // Elements that should NOT have text extracted
        const char *no_text_tags[] = {
            "input", "img", "br", "hr", "meta", "link", "form",
            "textarea", "select", "button", "fieldset", "legend",
            "option", "optgroup", "datalist", "output", "progress",
            "meter", "iframe", "canvas", "audio", "video", NULL
        };
        
        for (int i = 0; no_text_tags[i] != NULL; i++) {
            if (strcasecmp(tag, no_text_tags[i]) == 0) {
                should_get_text = 0;
                break;
            }
        }
        
        if (should_get_text) {
            text = get_element_text(element);
            printf("[LAYOUT DEBUG] Got text for %s: %s\n", tag, text ? text : "NULL");
        } else {
            printf("[LAYOUT DEBUG] Skipping text extraction for %s\n", tag);
        }
        
        if (text && strlen(text) > 0) {
            // Simple text height calculation
            int lines = (int)(strlen(text) / 50) + 1; // Rough estimate
            box.height = lines * style.line_height;
            printf("[LAYOUT DEBUG] Text requires %d lines, height: %.1f\n", 
                   lines, box.height);
        }
        
        if (text) {
            free(text);
        }
        
        // Special handling for specific block elements
        if (strcasecmp(tag, "textarea") == 0) {
            box.width = 300;
            box.height = 100;
            printf("[LAYOUT DEBUG] Textarea size: 300x100\n");
        }
        else if (strcasecmp(tag, "select") == 0) {
            box.width = 200;
            box.height = 32;
            printf("[LAYOUT DEBUG] Select size: 200x32\n");
        }
        else if (strcasecmp(tag, "h1") == 0) {
            box.height = 2.0 * style.line_height;
            printf("[LAYOUT DEBUG] H1 height: %.1f\n", box.height);
        }
        else if (strcasecmp(tag, "h2") == 0) {
            box.height = 1.8 * style.line_height;
            printf("[LAYOUT DEBUG] H2 height: %.1f\n", box.height);
        }
        else if (strcasecmp(tag, "h3") == 0) {
            box.height = 1.6 * style.line_height;
            printf("[LAYOUT DEBUG] H3 height: %.1f\n", box.height);
        }
        else if (strcasecmp(tag, "p") == 0) {
            box.height = 1.5 * style.line_height;
            printf("[LAYOUT DEBUG] Paragraph height: %.1f\n", box.height);
        }
        else if (strcasecmp(tag, "div") == 0) {
            box.height = 50; // Default div height
            printf("[LAYOUT DEBUG] Div height: %.1f\n", box.height);
        }
        
        // Position (block elements start new line)
        box.x = parent_x + style.margin_left;
        box.y = parent_y + style.margin_top;
        
    } else if (strcasecmp(style.display, "inline") == 0) {
        printf("[LAYOUT DEBUG] Inline element: %s\n", tag);
        
        // Inline elements - size based on content
        box.width = 100; // Default
        box.height = style.line_height;
        
        // ========== INPUT ELEMENT HANDLING ==========
        if (strcasecmp(tag, "input") == 0) {
            // Get input type to determine size
            const lxb_char_t *input_type = lxb_dom_element_get_attribute(
                element, (lxb_char_t*)"type", 4, &len);
            
            if (input_type && len > 0) {
                char type_str[32];
                size_t copy_len = len < 31 ? len : 31;
                memcpy(type_str, input_type, copy_len);
                type_str[copy_len] = '\0';
                
                printf("[LAYOUT DEBUG] Input type: %s\n", type_str);
                
                if (strcasecmp(type_str, "text") == 0 ||
                    strcasecmp(type_str, "password") == 0 ||
                    strcasecmp(type_str, "email") == 0) {
                    box.width = 200;
                    box.height = 32;
                }
                else if (strcasecmp(type_str, "checkbox") == 0 ||
                         strcasecmp(type_str, "radio") == 0) {
                    box.width = 16;
                    box.height = 16;
                }
                else if (strcasecmp(type_str, "button") == 0 ||
                         strcasecmp(type_str, "submit") == 0 ||
                         strcasecmp(type_str, "reset") == 0) {
                    box.width = 120;
                    box.height = 36;
                }
                else if (strcasecmp(type_str, "range") == 0) {
                    box.width = 200;
                    box.height = 20;
                }
                else if (strcasecmp(type_str, "color") == 0) {
                    box.width = 50;
                    box.height = 30;
                }
                else if (strcasecmp(type_str, "date") == 0 ||
                         strcasecmp(type_str, "time") == 0) {
                    box.width = 150;
                    box.height = 32;
                }
                else if (strcasecmp(type_str, "number") == 0) {
                    box.width = 100;
                    box.height = 32;
                }
                else if (strcasecmp(type_str, "file") == 0) {
                    box.width = 250;
                    box.height = 32;
                }
                else {
                    // Default for other input types
                    box.width = 200;
                    box.height = 32;
                }
            } else {
                // No type specified, default to text
                box.width = 200;
                box.height = 32;
            }
            
            printf("[LAYOUT DEBUG] Input size: %.1fx%.1f\n", box.width, box.height);
            
        } else if (strcasecmp(tag, "button") == 0) {
            box.width = 120;
            box.height = 36;
            printf("[LAYOUT DEBUG] Button size: 120x36\n");
            
        } else if (strcasecmp(tag, "label") == 0) {
            // Label size based on its text
            char *text = get_element_text(element);
            if (text && strlen(text) > 0) {
                box.width = strlen(text) * 8; // Rough estimate: 8px per character
                box.height = style.line_height;
                printf("[LAYOUT DEBUG] Label text: '%s', size: %.1fx%.1f\n", 
                       text, box.width, box.height);
            }
            if (text) free(text);
            
        } else if (strcasecmp(tag, "span") == 0) {
            // Span size based on its text
            char *text = get_element_text(element);
            if (text && strlen(text) > 0) {
                box.width = strlen(text) * 8;
                box.height = style.line_height;
                printf("[LAYOUT DEBUG] Span text: '%s', size: %.1fx%.1f\n", 
                       text, box.width, box.height);
            }
            if (text) free(text);
            
        } else if (strcasecmp(tag, "a") == 0) {
            // Link size based on its text
            char *text = get_element_text(element);
            if (text && strlen(text) > 0) {
                box.width = strlen(text) * 8;
                box.height = style.line_height;
                printf("[LAYOUT DEBUG] Link text: '%s', size: %.1fx%.1f\n", 
                       text, box.width, box.height);
            }
            if (text) free(text);
            
        } else if (strcasecmp(tag, "strong") == 0 ||
                   strcasecmp(tag, "em") == 0 ||
                   strcasecmp(tag, "b") == 0 ||
                   strcasecmp(tag, "i") == 0) {
            // Inline formatting elements
            char *text = get_element_text(element);
            if (text && strlen(text) > 0) {
                box.width = strlen(text) * 8;
                box.height = style.line_height;
                printf("[LAYOUT DEBUG] Formatting text: '%s', size: %.1fx%.1f\n", 
                       text, box.width, box.height);
            }
            if (text) free(text);
        }
        
        // Inline elements flow with text
        box.x = parent_x;
        box.y = parent_y;
        
    } else {
        // Unknown display type
        printf("[LAYOUT WARNING] Unknown display type: %s for %s\n", 
               style.display, tag);
        box.width = 100;
        box.height = 30;
        box.x = parent_x;
        box.y = parent_y;
    }
    
    // Add padding to get border box
    box.width += style.padding_left + style.padding_right;
    box.height += style.padding_top + style.padding_bottom;
    
    printf("[LAYOUT DEBUG] Final box with padding: (%.1f, %.1f) %.1fx%.1f\n", 
           box.x, box.y, box.width, box.height);
    
    free(tag);
    return box;
}

// Recursive layout calculation
static void calculate_layout_recursive(lxb_dom_node_t *node, 
    double parent_x, double parent_y,
    double parent_width, double parent_height,
    ComputedStyle *parent_style,
    cJSON *layout_result) {
if (!node || !layout_result) return;

static int depth = 0;
if (depth > 20) return; // Safety limit

depth++;

// DEBUG: Add node counter
static int node_count = 0;
int current_node = ++node_count;

if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
lxb_dom_element_t *element = lxb_dom_interface_element(node);

// Skip non-rendered elements
size_t len;
const lxb_char_t *tag_name = lxb_dom_element_qualified_name(element, &len);
char *tag = NULL;
if (tag_name && len > 0) {
tag = malloc(len + 1);
if (!tag) {
depth--;
return;
}
memcpy(tag, tag_name, len);
tag[len] = '\0';

printf("[LAYOUT] Node %d, depth %d, tag: %s\n", 
current_node, depth, tag);

// Skip these elements
if (strcasecmp(tag, "script") == 0 || 
strcasecmp(tag, "style") == 0 ||
strcasecmp(tag, "meta") == 0 ||
strcasecmp(tag, "link") == 0 ||
strcasecmp(tag, "title") == 0) {
printf("[LAYOUT] Skipping element: %s\n", tag);
free(tag);
depth--;
return;
}
} else {
printf("[LAYOUT] ERROR: No tag name for node %d!\n", current_node);
depth--;
return;
}

// Calculate layout for this element
LayoutBox box = calculate_element_layout(element, parent_x, parent_y,
    parent_width, parent_height,
    parent_style);

printf("[LAYOUT] Box for %s: (%.1f, %.1f) %.1fx%.1f\n", 
tag, box.x, box.y, box.width, box.height);

ComputedStyle style = compute_element_style(element, parent_style);

// Create element JSON
cJSON *elem_json = cJSON_CreateObject();
if (tag_name && len > 0) {
char *tag_str = malloc(len + 1);
if (tag_str) {
memcpy(tag_str, tag_name, len);
tag_str[len] = '\0';
cJSON_AddStringToObject(elem_json, "tag", tag_str);
free(tag_str);
}
}

cJSON_AddNumberToObject(elem_json, "x", box.x);
cJSON_AddNumberToObject(elem_json, "y", box.y);
cJSON_AddNumberToObject(elem_json, "width", box.width);
cJSON_AddNumberToObject(elem_json, "height", box.height);
cJSON_AddStringToObject(elem_json, "display", style.display);
cJSON_AddNumberToObject(elem_json, "font_size", style.font_size);

// Add to layout result
const lxb_char_t *id = lxb_dom_element_id(element, &len);
char *key = NULL;
if (id && len > 0) {
key = malloc(len + 1);
if (key) {
memcpy(key, id, len);
key[len] = '\0';
if (LAYOUT_DEBUG)
printf("[LAYOUT] Element ID: %s\n", key);
}
} 

if (!key) {
key = malloc(32);
if (key) {
snprintf(key, 32, "element_%d", current_node);
}
}

if (key) {
cJSON_AddItemToObject(layout_result, key, elem_json);
free(key);
} else {
cJSON_Delete(elem_json);
if (LAYOUT_DEBUG)
printf("[LAYOUT] ERROR: Failed to create key for element\n");
}

// Calculate children
double child_x = box.x + style.padding_left;
double child_y = box.y + style.padding_top;
double child_width = box.width - style.padding_left - style.padding_right;
double child_height = box.height - style.padding_top - style.padding_bottom;

printf("[LAYOUT] Child area for %s: (%.1f, %.1f) %.1fx%.1f\n", 
tag, child_x, child_y, child_width, child_height);

lxb_dom_node_t *child = lxb_dom_node_first_child(node);
double current_y = child_y;
int child_count = 0;

while (child) {
child_count++;
if (LAYOUT_DEBUG)
printf("[LAYOUT] Processing child %d of %s\n", child_count, tag);

if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
if (strcasecmp(style.display, "block") == 0) {
// Block children stack vertically
calculate_layout_recursive(child, child_x, current_y, 
  child_width, child_height,
  &style, layout_result);

// Update y position for next child
lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
if (child_elem) {
LayoutBox child_box = calculate_element_layout(
child_elem,
child_x, current_y, child_width, child_height, &style);
current_y += child_box.height + style.margin_bottom;
if (LAYOUT_DEBUG)
printf("[LAYOUT] Updated current_y to: %.1f\n", current_y);
}
} else {
// Inline children flow horizontally
calculate_layout_recursive(child, child_x, child_y,
  child_width, child_height,
  &style, layout_result);
}
} else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
printf("[LAYOUT] Skipping text node\n");
// Text nodes don't affect layout in our simple model
} else {
    if (LAYOUT_DEBUG)
printf("[LAYOUT] Skipping node type: %d\n", child->type);
}

child = lxb_dom_node_next(child);
}
if (LAYOUT_DEBUG)
printf("[LAYOUT] Processed %d children of %s\n", child_count, tag);

// FREE TAG HERE - AFTER ALL USES
free(tag);

} else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    if (LAYOUT_DEBUG)
printf("[LAYOUT] Text node (skipping)\n");
// Text nodes don't have layout boxes
} else {
    if (LAYOUT_DEBUG)
printf("[LAYOUT] Other node type: %d (skipping)\n", node->type);
}

depth--;
if (LAYOUT_DEBUG)
printf("[LAYOUT] Finished node %d\n\n", current_node);
}

cJSON* calculate_document_layout(lxb_html_document_t *document) {
    if (!document) {
        printf("ERROR: document is NULL\n");
        return NULL;
    }
    
    printf("=== CALCULATING DOCUMENT LAYOUT ===\n");
    
    cJSON *layout = cJSON_CreateObject();
    if (!layout) {
        printf("ERROR: Failed to create layout JSON\n");
        return NULL;
    }
    
    // Viewport
    cJSON *viewport = cJSON_CreateObject();
    if (!viewport) {
        printf("ERROR: Failed to create viewport JSON\n");
        cJSON_Delete(layout);
        return NULL;
    }
    cJSON_AddNumberToObject(viewport, "width", 800);
    cJSON_AddNumberToObject(viewport, "height", 600);
    cJSON_AddItemToObject(layout, "viewport", viewport);
    
    // Root style
    ComputedStyle root_style = {0};
    root_style.font_size = 16.0;
    root_style.line_height = 19.2;
    root_style.display = "block";
    root_style.position = "static";
    
    // Elements
    cJSON *elements = cJSON_CreateObject();
    if (!elements) {
        printf("ERROR: Failed to create elements JSON\n");
        cJSON_Delete(layout);
        return NULL;
    }
    cJSON_AddItemToObject(layout, "elements", elements);
    
    // Start calculation from body
    lxb_dom_document_t *dom_doc = lxb_dom_interface_document(document);
    if (!dom_doc) {
        printf("ERROR: Failed to get DOM document\n");
        cJSON_Delete(layout);
        return NULL;
    }
    
    lxb_dom_element_t *body = NULL;
    
    lxb_dom_collection_t *body_coll = lxb_dom_collection_make(dom_doc, 1);
    if (!body_coll) {
        printf("ERROR: Failed to create body collection\n");
        cJSON_Delete(layout);
        return NULL;
    }
    
    lxb_dom_element_t *doc_elem = lxb_dom_document_element(dom_doc);
    if (!doc_elem) {
        printf("ERROR: Failed to get document element\n");
        lxb_dom_collection_destroy(body_coll, true);
        cJSON_Delete(layout);
        return NULL;
    }
    
    printf("Searching for body element...\n");
    lxb_dom_elements_by_tag_name(doc_elem, body_coll, (lxb_char_t*)"body", 4);
    
    size_t body_count = lxb_dom_collection_length(body_coll);
    printf("Found %zu body elements\n", body_count);
    
    if (body_count > 0) {
        body = lxb_dom_collection_element(body_coll, 0);
        printf("Got body element pointer: %p\n", (void*)body);
        
        // Calculate body layout
        printf("Calculating body layout...\n");
        LayoutBox body_box = calculate_element_layout(body, 0, 0, 800, 600, &root_style);
        printf("Body layout calculated: %.1fx%.1f at (%.1f,%.1f)\n", 
               body_box.width, body_box.height, body_box.x, body_box.y);
        
        cJSON *body_json = cJSON_CreateObject();
        if (body_json) {
            cJSON_AddStringToObject(body_json, "tag", "body");
            cJSON_AddNumberToObject(body_json, "x", body_box.x);
            cJSON_AddNumberToObject(body_json, "y", body_box.y);
            cJSON_AddNumberToObject(body_json, "width", body_box.width);
            cJSON_AddNumberToObject(body_json, "height", body_box.height);
            cJSON_AddStringToObject(body_json, "display", "block");
            
            cJSON_AddItemToObject(elements, "body", body_json);
        }
        
        // Calculate children
        printf("Calculating body style...\n");
        ComputedStyle body_style = compute_element_style(body, &root_style);
        
        lxb_dom_node_t *body_node = lxb_dom_interface_node(body);
        if (body_node) {
            printf("Processing body children...\n");
            double child_y = body_box.y + body_style.padding_top;
            
            lxb_dom_node_t *child = lxb_dom_node_first_child(body_node);
            int child_count = 0;
            
            while (child) {
                printf("Processing child %d...\n", ++child_count);
                
                // Try-catch with simple check
                if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    calculate_layout_recursive(child, 
                                              body_box.x + body_style.padding_left,
                                              child_y,
                                              body_box.width - body_style.padding_left - body_style.padding_right,
                                              body_box.height - body_style.padding_top - body_style.padding_bottom,
                                              &body_style,
                                              elements);
                }
                
                // Update y position for next block child
                if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
                    if (child_elem) {
                        LayoutBox child_box = calculate_element_layout(child_elem,
                            body_box.x + body_style.padding_left,
                            child_y,
                            body_box.width - body_style.padding_left - body_style.padding_right,
                            body_box.height - body_style.padding_top - body_style.padding_bottom,
                            &body_style);
                        
                        child_y += child_box.height + body_style.margin_bottom;
                    }
                }
                
                child = lxb_dom_node_next(child);
            }
            
            printf("Processed %d body children\n", child_count);
        }
    } else {
        printf("WARNING: No body element found\n");
    }
    
    lxb_dom_collection_destroy(body_coll, true);
    printf("Layout calculation complete\n");
    
    return layout;
}