#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "cjson.h"
#include "font_manager.h"
#include "layout_engine.h"
#include "tables_parser.h"
#include "text_rules.h"
#include "menus_parser.h"
#include "buttons.h"
#include "rendering_elements/defaults.h"
#include "js_executor_quickjs.h"


extern cJSON *global_computed_layout;
// Helper to estimate text width
int estimate_text_width(const char *text, int font_size, 
    const char *font_weight, 
    const char *font_style) {
if (!text) return 100;
if (font_size <= 0) font_size = DEFAULT_FONT_SIZE;

int len = strlen(text);
if (len == 0) return 50;

float total_width = 0;
float char_width;

// Style multipliers
float weight_mult = 1.0;
float style_mult = 1.0;

if (font_weight && strcmp(font_weight, "bold") == 0) {
weight_mult = 1.2;  // Bold text is wider
}

if (font_style && strcmp(font_style, "italic") == 0) {
style_mult = 1.1;   // Italic text slants, takes slightly more space
}

for (int i = 0; i < len; i++) {
char c = text[i];

// Character-specific widths (as percentage of font_size)
if (c == 'i' || c == 'l' || c == 'I' || c == '1' || c == 't') {
char_width = 0.3;  // Narrow characters
}
else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') {
char_width = 0.9;  // Wide characters
}
else if (c == ' ') {
char_width = 0.4;   // Space
}
else if (c == '.' || c == ',' || c == ':' || c == ';') {
char_width = 0.2;   // Punctuation
}
else {
char_width = 0.6;   // Average characters
}

// Apply style multipliers
char_width *= weight_mult * style_mult;

total_width += char_width;

// Add slight kerning between specific pairs
if (i > 0) {
char prev = text[i-1];
if ((prev == 'W' || prev == 'V') && (c == 'i' || c == 'a')) {
total_width -= 0.05 * weight_mult; // Less kerning for bold
}
}
}

return (int)(total_width * font_size);
}

int parse_css_length(const char *length_str, int reference_size)
{
    if (!length_str || *length_str == '\0')
        return 0;
    
    // Skip non-numeric values (colors, keywords, etc.)
    if (length_str[0] == '#' || 
        strcmp(length_str, "none") == 0 ||
        strcmp(length_str, "auto") == 0 ||
        strcmp(length_str, "normal") == 0 ||
        strcmp(length_str, "bold") == 0 ||
        strcmp(length_str, "italic") == 0 ||
        strcmp(length_str, "underline") == 0) {
        return 0;
    }
    
    // Check if the string contains spaces (multiple values)
    if (strchr(length_str, ' ') != NULL) {
        return 0;
    }

    char *endptr;
    double value = strtod(length_str, &endptr);

    if (endptr == length_str)
        return 0;

    while (*endptr == ' ' || *endptr == '\t')
        endptr++;

    if (*endptr == '\0' || strcmp(endptr, "px") == 0) {
        return (int)(value + 0.5);
    }

    if (strcmp(endptr, "rem") == 0) {
        return (int)(value * 16.0 + 0.5);
    }

    if (strcmp(endptr, "em") == 0) {
        return (int)(value * reference_size + 0.5);
    }

    if (strcmp(endptr, "%") == 0) {
        return (int)((value / 100.0) * reference_size + 0.5);
    }

    if (strcmp(endptr, "vw") == 0) {
        return (int)((value / 100.0) * 800 + 0.5);
    }

    if (strcmp(endptr, "vh") == 0) {
        return (int)((value / 100.0) * 600 + 0.5);
    }

    if (strcmp(endptr, "pt") == 0) {
        return (int)(value * 1.333333 + 0.5);
    }

    if (strcmp(endptr, "pc") == 0) {
        return (int)(value * 16.0 + 0.5);
    }

    if (strcmp(endptr, "mm") == 0) {
        return (int)(value * 3.779528 + 0.5);
    }

    if (strcmp(endptr, "cm") == 0) {
        return (int)(value * 37.79528 + 0.5);
    }

    if (strcmp(endptr, "in") == 0) {
        return (int)(value * 96.0 + 0.5);
    }

    return (int)(value + 0.5);
}

const char* get_json_string(cJSON *obj, const char *key, const char *default_value) {
    if (!obj) return default_value;
    
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return default_value;
}

void set_json_string(cJSON *obj, const char *key, const char *val) {
    if (!obj || !val) return;
    if (cJSON_HasObjectItem(obj, key)) {
        cJSON_ReplaceItemInObject(obj, key, cJSON_CreateString(val));
    } else {
        cJSON_AddItemToObject(obj, key, cJSON_CreateString(val));
    }
}

int get_json_number(cJSON *obj, const char *key, int default_value) {
    if (!obj) return default_value;
    
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item) return default_value;
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        if (strcasecmp(item->valuestring, "auto") == 0) {
            return default_value;
        }
        char *endptr = NULL;
        long val = strtol(item->valuestring, &endptr, 10);
        if (endptr != item->valuestring) {
            return (int)val;
        }
    }
    return default_value;
}

void set_json_number(cJSON *obj, const char *key, int val) {
    if (!obj) return;
    if (cJSON_HasObjectItem(obj, key)) {
        cJSON_ReplaceItemInObject(obj, key, cJSON_CreateNumber(val));
    } else {
        cJSON_AddItemToObject(obj, key, cJSON_CreateNumber(val));
    }
}

void set_json_bool(cJSON *obj, const char *key, int val) {
    if (!obj) return;
    if (cJSON_HasObjectItem(obj, key)) {
        cJSON_ReplaceItemInObject(obj, key, cJSON_CreateBool(val));
    } else {
        cJSON_AddBoolToObject(obj, key, val);
    }
}

int get_json_bool(cJSON *obj, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item) return default_value;

    // 1. Check if it's a real JSON boolean
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item); 
    }

    // 2. Check if it's a string (from Lexbor/HTML attributes)
    if (cJSON_IsString(item) && item->valuestring) {
        return (strcasecmp(item->valuestring, "true") == 0 ||
                strcasecmp(item->valuestring, "yes") == 0 ||
                strcasecmp(item->valuestring, "1") == 0 ||
                strcasecmp(item->valuestring, "on") == 0);
    }

    return default_value;
}

int get_container_width(cJSON *element, LayoutContext *ctx, pauk_ui_t *pauk_ui) {
    if (!element) return 800;  // Fallback
   


    // Get padding values (default to 0)
    int padding_left = get_json_number(element, "padding_left", 0);
    int padding_right = get_json_number(element, "padding_right", 0);
    int border_width = get_json_number(element, "border_width", 0);
    
    // Total horizontal space consumed by padding and border
    int horizontal_padding = padding_left + padding_right + (border_width * 2);
    
    // 1. Check if element has explicit CSS width
    int explicit_width = get_json_number(element, "width", 0);
    if (explicit_width > 0) {
        return explicit_width - horizontal_padding;
    }
    
    // 2. Check if parent has width
    if (ctx->parent_element) {
        int parent_width = get_json_number(ctx->parent_element, "width", 0);
        if (parent_width > 0) {
            return parent_width - horizontal_padding;;
        }
    }
    
    // 3. Use window width based on screen mode
    int window_width;
  
        window_width = pauk_ui->list_rect_base.p1.x - pauk_ui->list_rect_base.p0.x;
    
    // Subtract margins (20px on each side) and padding
    return window_width - horizontal_padding - 40;  // 40 = 20px left + 20px right margins
    
    // 4. Ultimate fallback
   // return 800 - horizontal_padding - 40;
}


void layout_flex_container(cJSON *element, LayoutContext *ctx) {
    const char *flex_direction = get_json_string(element, "flex_direction", "row");
   // const char *flex_wrap = get_json_string(element, "flex_wrap", "nowrap");
    
    int padding_left = get_json_number(element, "padding_left", 0);
    int padding_top = get_json_number(element, "padding_top", 0);
    int border_width = get_json_number(element, "border_width", 0);
    
    int content_x = ctx->parent_x + padding_left + border_width;
    int content_y = ctx->current_y + padding_top + border_width;
    
    // Store element position
    cJSON_ReplaceItemInObject(element, "x", cJSON_CreateNumber(ctx->parent_x));
    cJSON_ReplaceItemInObject(element, "y", cJSON_CreateNumber(ctx->current_y));
    
    if (strcmp(flex_direction, "row") == 0) {
        // Horizontal layout
        int current_x = content_x;
        int max_height = 0;
        
        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            cJSON *child;
            cJSON_ArrayForEach(child, children) {
                // Layout each child at current_x position
                LayoutContext child_ctx = *ctx;
                child_ctx.parent_x = current_x;
                child_ctx.parent_y = content_y;
                child_ctx.current_x = current_x;
                child_ctx.current_y = content_y;
                
                racunaj_pozicije(child, current_x, content_y, 0, &child_ctx);
                
                // Move to next position
                int child_width = get_json_number(child, "width", 0);
                int margin_right = get_json_number(child, "margin_right", 0);
                current_x += child_width + margin_right;
                
                // Track max height
                int child_height = get_json_number(child, "height", 0);
                int margin_top = get_json_number(child, "margin_top", 0);
                int margin_bottom = get_json_number(child, "margin_bottom", 0);
                int total_height = child_height + margin_top + margin_bottom;
                if (total_height > max_height) max_height = total_height;
            }
        }
        
        // Set container dimensions
        int container_width = current_x - content_x;
       // cJSON_ReplaceItemInObject(element, "width", cJSON_CreateNumber(container_width));
        set_json_number(element, "width", container_width); 
        // cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(max_height));
        set_json_number(element, "height", max_height); 
        // Update context for next element
        ctx->current_y = ctx->parent_y + max_height + padding_top + border_width;
    } else {
        // Fallback to block layout for column direction
        layout_block_element(element, ctx);
    }
}


// ========== SPECIALIZED LAYOUT FUNCTIONS ==========
void layout_inline_block_element(cJSON *element, LayoutContext *ctx) {
    // Get dimensions and margins
    int width = get_json_number(element, "width", 0);
    int height = get_json_number(element, "height", 0);
    int margin_top = get_json_number(element, "margin_top", 0);
    int margin_bottom = get_json_number(element, "margin_bottom", 0);
    int margin_left = get_json_number(element, "margin_left", 0);
    int margin_right = get_json_number(element, "margin_right", 0);
    
    int total_width = width + margin_left + margin_right;
    int total_height = height + margin_top + margin_bottom;
    
    // Check if fits on current line (inline behavior)
    int available_width = ctx->container_width - ctx->current_x;
    
    if (total_width > available_width && ctx->current_x > ctx->parent_x) {
        // Move to next line
        ctx->current_x = ctx->parent_x;
        ctx->line_height = 0;
    }
    
    // Calculate position (respect vertical margins)
    int x = ctx->current_x + margin_left;
    int y = ctx->current_y + margin_top;
    
    // Update element - STORE TOTAL HEIGHT (including margins)
    // Update element using set_json_number
    set_json_number(element, "x", x);
    set_json_number(element, "y", y);
    set_json_number(element, "width", width);
    set_json_number(element, "height", total_height);  // Store total height (including margins)
    // Update context
    ctx->current_x = x + width + margin_right;
    
    // Update line height (use total height)
    if (total_height > ctx->line_height) {
        ctx->line_height = total_height;
    }

    if (ctx->parent_element) {
        int element_bottom = y + total_height;
        int current_parent_bottom = ctx->current_y;
        if (element_bottom > current_parent_bottom) {
            ctx->current_y = element_bottom;
        }
    }

    set_json_number(element, "layout_calculated", 1);
}

void layout_block_element(cJSON *element, LayoutContext *ctx) {
    if (!element || !ctx) return;

// 2. Extract properties
const char *tag = get_json_string(element, "tag", "");
const char *type = get_json_string(element, "type", "block");
const char *display = get_json_string(element, "display", "block");
const char *menu_orientation = get_json_string(element, "menu_orientation", "horizontal");
const char *float_prop = get_json_string(element, "float", "none");

    // 🚀 OVDE UBACIŠ ČISTI ŠTIT KORISTEĆI SVOJE PROMENLJIVE:
    int is_visible_flag = get_json_bool(element, "is_visible", 1);
    if (strcmp(display, "none") == 0 || is_visible_flag == 0) {
        set_json_number(element, "width", 0);
        set_json_number(element, "height", 0);
        set_json_number(element, "layout_calculated", 1);
        return; // Odmah izlazimo, sasecamo rekurziju i proračun visine!
    }

int margin_left   = get_json_number(element, "margin_left", 0);
int margin_top    = get_json_number(element, "margin_top", 0);  // ← UNCOMMENT THIS!
int margin_bottom = get_json_number(element, "margin_bottom", 0);
int padding_left  = get_json_number(element, "padding_left", 0);
int padding_top   = get_json_number(element, "padding_top", 0);
int padding_bottom = get_json_number(element, "padding_bottom", 0);
int element_width = get_json_number(element, "width", 0);
int element_height = get_json_number(element, "height", 0);
int margin_right  = get_json_number(element, "margin_right", 0);

    int is_inline = (strcmp(type, "inline") == 0);
    int is_flex   = (strcmp(display, "flex") == 0);
    int is_floated_left = (strcmp(float_prop, "left") == 0);

    int is_vertical_nav = (strcmp(tag, "nav") == 0 && strcmp(menu_orientation, "vertical") == 0);
    // proveriti da ne postoji element "sidebar"
    int is_sidebar = is_vertical_nav;

    // 2a. Set default width for blocks
    if (element_width <= 0 && !is_inline) {
        int avail = (ctx->container_width > 0) ? ctx->container_width : 800;
        if (strcasecmp(tag, "td") == 0) {
            // 🚀 UNIVERZALNI FIKS ZA TABELE: Delimo širinu ekrana na 3 jednaka dela
            element_width = (avail - margin_left) / 3;
        } else {
            // Tvoja originalna logika za sve ostale standardne block elemente
            element_width = avail - margin_left;
        }
        
        if (element_width < 0) element_width = 0;
        set_json_number(element, "width", element_width);
    }


    // 3. Float Clearing (Legacy support)
    if (!is_floated_left && ctx->has_floated_left && ctx->current_y >= (ctx->floated_y + ctx->floated_height)) {
        ctx->has_floated_left = 0;
        ctx->current_x = ctx->parent_x;
    }

    // 4. Float influence
    if (!is_floated_left && ctx->has_floated_left) {
        int float_bottom = ctx->floated_y + ctx->floated_height;
        int float_right  = ctx->floated_left_x + ctx->floated_left_width;
        int available_width = ctx->container_width - ctx->floated_left_width;

        if (ctx->current_y < float_bottom && element_width <= available_width) {
            ctx->current_x = float_right + 10;
            if (ctx->current_y < ctx->floated_y)
                ctx->current_y = ctx->floated_y;
        } else {
            ctx->has_floated_left = 0;
            ctx->current_x = ctx->parent_x;
        }
    }

    
// 5. Calculate element position 
int x = ctx->current_x + margin_left;
int previous_bottom_margin = ctx->previous_margin_bottom;
int effective_margin = (margin_top > previous_bottom_margin) ? margin_top : previous_bottom_margin;
int y = ctx->current_y + effective_margin;
ctx->previous_margin_bottom = margin_bottom;

 // 6. Vertical nav styling for LI/A (keep as is)
    if (strcmp(tag, "li") == 0 && strcmp(menu_orientation, "vertical") == 0) {
        if (element->next != NULL) {
            set_json_string(element, "border_bottom_width", "1px");
            set_json_string(element, "border_bottom_style", "solid");
            set_json_string(element, "border_bottom_color", "#555555");
            margin_bottom += 15;
            set_json_number(element, "margin_bottom", margin_bottom);
        }

        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            cJSON *a_elem = cJSON_GetArrayItem(children, 0);
            if (a_elem && strcmp(get_json_string(a_elem, "tag", ""), "a") == 0) {
                set_json_string(a_elem, "display", "block");
                set_json_number(a_elem, "padding_top", 8);
                set_json_number(a_elem, "padding_bottom", 8);
            }
        }
    }

    // 7. Set element coordinates
    set_json_number(element, "x", x);
    set_json_number(element, "y", y);

     // 8. Layout children
     LayoutContext child_ctx = *ctx;
     child_ctx.has_floated_left = 0;
 
     // 🚀 UNIVERZALNI FIKS ZA UNUTRAŠNJOST TABELA: 
     // Ako je trenutni element TD, unutrašnji kursor MORA početi od njegove lične X pozicije (x + padding),
     // a ne od prethodne globalne X vrednosti koja je akumulirana u ctx->current_x!
     if (strcasecmp(tag, "td") == 0) {
         child_ctx.parent_x = x + padding_left;
         child_ctx.current_x = x + padding_left;
     } else {
         child_ctx.parent_x = x + padding_left;
         child_ctx.current_x = x + padding_left;
     }
 
     child_ctx.parent_y = y + padding_top;
     child_ctx.current_y = y + padding_top;
     child_ctx.container_width = (element_width > 0) ? element_width : ((ctx->container_width > 0) ? ctx->container_width : 800);
     child_ctx.available_width = child_ctx.container_width;
 
     // Tvoj originalni Flexbox i Block rekurzivni layout za decu se nastavlja normalno ovde...
     if (is_flex) {
         layout_flex_element(element, &child_ctx);
     }
     else {
         cJSON *children = cJSON_GetObjectItem(element, "children");
         if (children && cJSON_IsArray(children)) {
             cJSON *child;
             cJSON_ArrayForEach(child, children) {
                 layout_block_element(child, &child_ctx);
 
                 ctx->current_x = child_ctx.current_x;
                 ctx->current_y = child_ctx.current_y;
             }
         }
     }
 

// 9. Auto-calculate height
if (element_height == 0) {
    int max_child_bottom = y + padding_top;  // Remove margin_bottom+margin_top
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            int child_y = get_json_number(child, "y", 0);
            int child_h = get_json_number(child, "height", 0);
            int child_bottom = child_y + child_h;
            if (child_bottom > max_child_bottom) {
                max_child_bottom = child_bottom;
            }
        }
    }
    element_height = (max_child_bottom - y) + padding_bottom;
    if (element_height < 20) element_height = 20;
    set_json_number(element, "height", element_height);
}


    // 10. Update cursor for next sibling - THIS IS WHERE THE PARENT UPDATES ITS CONTEXT
    if (is_sidebar) {
        ctx->has_floated_left = 1;
        ctx->floated_left_x = x;
        ctx->floated_left_width = element_width + margin_right;
        ctx->floated_y = y;
        ctx->floated_height = element_height + margin_bottom;
        ctx->current_x = x + element_width + margin_right + 10;
        ctx->current_y = y;
    }
    else if (is_floated_left) {
        ctx->has_floated_left = 1;
        ctx->floated_left_x = x;
        ctx->floated_left_width = element_width + margin_right;
        ctx->floated_y = y;
        ctx->floated_height = element_height + margin_bottom;
        ctx->current_x = x + element_width + margin_right + 10;
        
        // 🚀 SPASILAC GEOMETRIJE: Resetujemo Y kursor za sledećeg brata (sibling) da ne ode u minus!
        ctx->current_y = y; 
    }
    else if (is_flex && element_width < (ctx->container_width * 0.7)) {
        ctx->current_x = x + element_width + DEFAULT_ELEMENT_SPACING;
    }
    else if (is_inline) {
        ctx->current_x = x + element_width + DEFAULT_INLINE_SPACING;
    }
    else {
        // Regular block elements - add vertical spacing
        ctx->current_y = y + element_height + margin_bottom;
        ctx->current_x = ctx->parent_x;
    }

}


void layout_inline_element(cJSON *element, LayoutContext *ctx) {
    if (!element || !ctx) return;

    // 🚀 QUICK CHECK: If hidden, zero out immediately
    const char *display = get_json_string(element, "display", "inline");
    const char *input_type = get_json_string(element, "input_type", "");
    if (strcmp(display, "none") == 0 || strcmp(input_type, "hidden") == 0) {
        set_json_number(element, "width", 0);
        set_json_number(element, "height", 0);
        set_json_number(element, "layout_calculated", 1);
        return;
    }

    int margin_right = get_json_number(element, "margin_right", 0);
    const char *tag = get_json_string(element, "tag", "");
    input_type = get_json_string(element, "input_type", "");
    
    int is_link = (strcmp(tag, "a") == 0);
    int is_button = get_json_bool(element, "is_button", 0) || 
                    (strcmp(tag, "input") == 0 && (strcmp(input_type, "submit") == 0 || strcmp(input_type, "button") == 0));
    
    // Get text content
    const char *text = "";
    int width = 0, height = 0;
    
    // For links, use full_text if available
    if (is_link) {
        const char *full_text = get_json_string(element, "full_text", "");
        if (strlen(full_text) > 0) {
            text = full_text;
        } else {
            // Fallback: check children for text node
            cJSON *children = cJSON_GetObjectItem(element, "children");
            if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
                cJSON *child = cJSON_GetArrayItem(children, 0);
                if (strcmp(get_json_string(child, "tag", ""), "text") == 0) {
                    text = get_json_string(child, "content", "");
                }
            }
        }
    }
    // 🚀 FIX FOR GOOGLE BUTTONS: Ako je submit/button input, tekst je u "value" ili "input_value"
    else if (is_button) {
        const char *val_text = get_json_string(element, "value", "");
        if (strlen(val_text) > 0) {
            text = val_text;
        } else {
            const char *in_val_text = get_json_string(element, "input_value", "");
            if (strlen(in_val_text) > 0) {
                text = in_val_text;
            } else {
                // Fallback na decu ako postoje
                cJSON *children = cJSON_GetObjectItem(element, "children");
                if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
                    cJSON *child = cJSON_GetArrayItem(children, 0);
                    if (strcmp(get_json_string(child, "tag", ""), "text") == 0) {
                        text = get_json_string(child, "content", "");
                    }
                }
            }
        }
        // Fallback ako je dugme i dalje prazno
        if (strlen(text) == 0) {
            text = "Submit";
        }
    }
    // For other inline elements (mark, strong, em, etc.)
    else {
        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
            cJSON *child = cJSON_GetArrayItem(children, 0);
            if (strcmp(get_json_string(child, "tag", ""), "text") == 0) {
                text = get_json_string(child, "content", "");
            }
        }
    }
    
    // If no text from children/specific fields, check direct content field
    if (strlen(text) == 0) {
        cJSON *content_item = cJSON_GetObjectItem(element, "content");
        text = content_item && cJSON_IsString(content_item) ? content_item->valuestring : "";
    }
    
    int font_size = DEFAULT_FONT_SIZE;
    if (strlen(text) > 0) {
        cJSON *font_size_item = cJSON_GetObjectItem(element, "font_size");
        int padding_top = get_json_number(element, "padding_top", 0);
        int padding_bottom = get_json_number(element, "padding_bottom", 0);
        
        if (font_size_item) {
            if (cJSON_IsNumber(font_size_item)) {
                font_size = font_size_item->valueint;
            } else if (cJSON_IsString(font_size_item)) {
                font_size = parse_css_length(font_size_item->valuestring, DEFAULT_FONT_SIZE);
            }
        }
        
        const char *weight = get_json_string(element, "font_weight", "normal");
        const char *style = get_json_string(element, "font_style", "normal");
        width = estimate_text_width(text, font_size, weight, style);
        
        // For buttons, add button-specific padding i fiksne dimenzije za Google izgled
        if (is_button) {
            int button_padding_left = get_json_number(element, "padding_left", 16);
            int button_padding_right = get_json_number(element, "padding_right", 16);
            width = width + button_padding_left + button_padding_right;
            
            // Osiguravamo minimalnu širinu za Google dugmad da tekst ne bude zgnječen
            if (width < 120) width = 120;
            height = font_size + padding_top + padding_bottom + 12;
        } else {
            height = font_size + padding_top + padding_bottom + 8;
        }
        
        // Store dimensions
        set_json_number(element, "width", width);
        set_json_number(element, "height", height);
    } else {
        width = get_json_number(element, "width", 0);
        height = get_json_number(element, "height", 0);
    }
    
    // Calculate position
    int x = ctx->current_x;
    int y;

    // Check if this is an output element
    int is_output = get_json_bool(element, "is_output", 0);

    if (ctx->parent_element && !is_inline(get_json_string(ctx->parent_element, "tag", ""))) {
        if (is_output) {
            y = ctx->current_y;
        } else {
            y = ctx->current_y + font_size;
        }
    } else {
        y = ctx->parent_y;
    }

    // Update element position
    set_json_number(element, "x", x);
    set_json_number(element, "y", y);
    
    // Update line height
    if (height > ctx->line_height) {
        ctx->line_height = height;
    }
    
    // Update context for next element - pomera horizontalni flow u desno za sledeće dugme!
    ctx->current_x = x + width + margin_right;
// ========== 🚀 DODATO: Rekurzivno obradi decu ==========
cJSON *children = cJSON_GetObjectItem(element, "children");
if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
    // ✅ JEDAN kontekst za svu decu - izvan petlje!
    LayoutContext child_ctx = *ctx;
    child_ctx.parent_element = element;
    child_ctx.parent_x = x;
    child_ctx.parent_y = y;
    child_ctx.current_x = x + get_json_number(element, "padding_left", 0);
    child_ctx.current_y = y;
    child_ctx.line_height = 0;
    
    cJSON *child;
    cJSON_ArrayForEach(child, children) {
        const char *child_tag = get_json_string(child, "tag", "");
        
        // Preskoči text node-ove - oni nemaju layout
        if (strcmp(child_tag, "text") == 0) continue;
        
        const char *child_display = get_json_string(child, "display", "inline");
        
        if (strcmp(child_display, "block") == 0) {
            layout_block_element(child, &child_ctx);
        } else {
            layout_inline_element(child, &child_ctx);  // ← Ovde se child_ctx ažurira!
        }
    }
    
    // ✅ Ažuriraj i glavni ctx na kraju
    ctx->current_x = child_ctx.current_x;
    ctx->current_y = child_ctx.current_y;
    if (child_ctx.line_height > ctx->line_height) {
        ctx->line_height = child_ctx.line_height;
    }
}
// ========== KRAJ DODATKA ==========
}


void layout_text_node(cJSON *element, LayoutContext *ctx) {
    // Text margins
    int margin_left = get_json_number(element, "margin_left", 0);
    int margin_top = get_json_number(element, "margin_top", 0);
    int margin_right = get_json_number(element, "margin_right", 0);
    
    // Get available width
    int max_width = ctx->available_width;
    if (max_width <= 0) max_width = 800;
    
    // Get text content
    const char *content = "";
    cJSON *content_item = cJSON_GetObjectItem(element, "content");
    if (content_item && cJSON_IsString(content_item)) {
        content = content_item->valuestring;
    }
    
    if (!content || strlen(content) == 0) {
        return;
    }
    
    // Get font properties
    int font_size = DEFAULT_FONT_SIZE;
    cJSON *font_size_item = cJSON_GetObjectItem(element, "font_size");
    if (font_size_item) {
        if (cJSON_IsNumber(font_size_item)) font_size = font_size_item->valueint;
        else if (cJSON_IsString(font_size_item)) font_size = parse_css_length(font_size_item->valuestring, DEFAULT_FONT_SIZE);
    }
    
    const char *weight = get_json_string(element, "font_weight", "normal");
    const char *style = get_json_string(element, "font_style", "normal");
    
    // Calculate single line width
    int single_line_width = estimate_text_width(content, font_size, weight, style);
    int available_content_width = max_width - margin_left - margin_right;
    
    // ========== CHECK IF WRAPPING IS NEEDED ==========
    if (single_line_width > available_content_width && max_width > 0 && strlen(content) > 3) {
        // ========== WORD WRAPPING LOGIC ==========
        char *text_copy = strdup(content);
        if (!text_copy) {
            printf("❌ Failed to allocate memory for text copy\n");
            return;
        }
        
        // Tokenizer - split into words (NO space tokens)
        char *words[1024];
        int word_count = 0;
        
        char *p = text_copy;
        int in_word = 0;
        char *word_start = NULL;
        
        while (*p && word_count < 1024) {
            if (*p == ' ' || *p == '\t' || *p == '\n') {
                if (in_word) {
                    *p = '\0';
                    words[word_count++] = word_start;
                    in_word = 0;
                }
            } else {
                if (!in_word) {
                    word_start = p;
                    in_word = 1;
                }
            }
            p++;
        }
        
        if (in_word && word_count < 1024) {
            words[word_count++] = word_start;
        }
        
        // Build lines
        char **lines = malloc(1024 * sizeof(char*));
        if (!lines) {
            free(text_copy);
            return;
        }
        
        int line_count = 0;
        char *current_line = malloc(4096);
        if (!current_line) {
            free(lines);
            free(text_copy);
            return;
        }
        current_line[0] = '\0';
        int current_line_width = 0;
        
        for (int i = 0; i < word_count; i++) {
            int word_width = estimate_text_width(words[i], font_size, weight, style);
            int space_width = (i > 0 && current_line_width > 0) ? 
                              estimate_text_width(" ", font_size, weight, style) : 0;
            
            if (current_line_width + space_width + word_width <= available_content_width) {
                // Add to current line
                if (current_line_width > 0) {
                    strcat(current_line, " ");
                    current_line_width += space_width;
                }
                strcat(current_line, words[i]);
                current_line_width += word_width;
            } else {
                // Start new line
                if (current_line_width > 0) {
                    lines[line_count] = strdup(current_line);
                    if (lines[line_count]) line_count++;
                }
                strcpy(current_line, words[i]);
                current_line_width = word_width;
            }
        }
        
        // Add last line
        if (current_line_width > 0 && line_count < 1024) {
            lines[line_count] = strdup(current_line);
            if (lines[line_count]) line_count++;
        }
        
        int line_height = font_size + 4;
        int total_height = line_count * line_height;
        
        int x = ctx->current_x + margin_left;
        int y = ctx->current_y + margin_top;
        
        cJSON_ReplaceItemInObject(element, "x", cJSON_CreateNumber(x));
        cJSON_ReplaceItemInObject(element, "y", cJSON_CreateNumber(y));
        
        // Build wrapped text
        size_t wrapped_size = strlen(content) + line_count + 100;
        char *wrapped_text = malloc(wrapped_size);
        if (wrapped_text) {
            wrapped_text[0] = '\0';
            for (int i = 0; i < line_count; i++) {
                if (i > 0) strcat(wrapped_text, "\n");
                strcat(wrapped_text, lines[i]);
                free(lines[i]);
            }
            cJSON_ReplaceItemInObject(element, "text", cJSON_CreateString(wrapped_text));
            free(wrapped_text);
        }
        
        cJSON_ReplaceItemInObject(element, "width", cJSON_CreateNumber(available_content_width));
        cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(total_height));
        
        ctx->current_x = x + available_content_width + margin_right;
        ctx->current_y = y + total_height;
        if (total_height > ctx->line_height) ctx->line_height = total_height;
        
        free(current_line);
        free(lines);
        free(text_copy);
 
    } else {
        // ========== SINGLE LINE TEXT ==========
        int x = ctx->current_x + margin_left;
        int y = ctx->current_y + margin_top;
        int text_height = font_size + 4;
        
        cJSON_ReplaceItemInObject(element, "x", cJSON_CreateNumber(x));
        cJSON_ReplaceItemInObject(element, "y", cJSON_CreateNumber(y));
        cJSON_ReplaceItemInObject(element, "text", cJSON_CreateString(content));
        cJSON_ReplaceItemInObject(element, "width", cJSON_CreateNumber(single_line_width));
        cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(text_height));
        
        ctx->current_x = x + single_line_width + margin_right;
        ctx->current_y += text_height;
        if (text_height > ctx->line_height) ctx->line_height = text_height;

    }
    
}


void layout_flex_element(cJSON *element, LayoutContext *ctx) {
    const char *flex_direction = get_json_string(element, "flex_direction", "row");

    if (strcmp(flex_direction, "row") == 0) {
        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            int max_height = 0;
            int num_children = cJSON_GetArraySize(children);
            int avail_w = (ctx->container_width > 0) ? ctx->container_width : 800;
            int default_w = (num_children > 0) ? (avail_w / num_children) : avail_w;
            
            cJSON *child;
            int idx = 0;
            cJSON_ArrayForEach(child, children) {
                int child_w = get_json_number(child, "width", default_w);
                if (child_w <= 0) child_w = default_w;

                // CRITICAL: Set the child's parent context properly
                LayoutContext child_ctx = *ctx;
                child_ctx.parent_x = ctx->current_x;
                child_ctx.parent_y = ctx->current_y;
                child_ctx.container_width = child_w;
                child_ctx.available_width = child_w;
                
                // Layout the child using racunaj_pozicije, not layout_inline_block_element directly
                racunaj_pozicije(child, ctx->current_x, ctx->current_y, 
                                 child_ctx.container_width, &child_ctx);
                
                // Get child dimensions and update context
                int child_x = get_json_number(child, "x", 0);
                int actual_child_w = get_json_number(child, "width", child_w);
                int child_h = get_json_number(child, "height", 0);

                // Update current_x for next child
                if (child_x + actual_child_w > ctx->current_x) {
                    ctx->current_x = child_x + actual_child_w;
                }
                
                if (child_h > max_height) max_height = child_h;
                idx++;
            }
            
            // Set container dimensions
            int padding_top = get_json_number(element, "padding_top", 0);
            int padding_bottom = get_json_number(element, "padding_bottom", 0);
            int container_height = max_height + padding_top + padding_bottom;
            
            set_json_number(element, "height", container_height);
        }
    } else {
        layout_block_element(element, ctx);
    }
}


void layout_break_element(cJSON *element, LayoutContext *ctx) {

    const char *tag = get_json_string(element, "tag", "");
    
    // 1. Calculate where the break should be
    int font_size = DEFAULT_FONT_SIZE;
    if (ctx->parent_element) {
        // Get the font_size item from JSON
        cJSON *font_size_item = cJSON_GetObjectItem(ctx->parent_element, "font_size");
        
        if (font_size_item) {
            if (cJSON_IsNumber(font_size_item)) {
                font_size = font_size_item->valueint;
            } else if (cJSON_IsString(font_size_item)) {
                font_size = parse_css_length(font_size_item->valuestring, DEFAULT_FONT_SIZE);
            }
        }
    }
    
    //1.5 change block and display
    cJSON_ReplaceItemInObject(element, "type", cJSON_CreateString("inline"));
    cJSON_ReplaceItemInObject(element, "display", cJSON_CreateString("inline"));
    
    // 2. Save the BREAK POSITION in JSON
    cJSON_ReplaceItemInObject(element, "x", cJSON_CreateNumber(ctx->current_x));
    cJSON_ReplaceItemInObject(element, "y", cJSON_CreateNumber(ctx->current_y));
    cJSON_ReplaceItemInObject(element, "width", cJSON_CreateNumber(0));
    cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(0));
    
    // 3. Handle line break based on tag
    if (strcmp(tag, "br") == 0) {
        // <br> forces a line break
        ctx->current_y += font_size;
        ctx->current_x = ctx->parent_x;
        ctx->line_height = 0;
    }
    // <wbr> does nothing for now - just a placeholder for future line wrapping
}

int has_ancestor_with_class(cJSON *elem, const char *target_class) {
    if (!elem) return 0;
    
    int parent_id = get_json_number(elem, "parent_id", -1);
    if (parent_id == -1) return 0;
    
    cJSON *ancestor = find_element_by_id(global_computed_layout, parent_id);
    
    while (ancestor) {
        const char *classes = get_json_string(ancestor, "class_string", "");
        if (strstr(classes, target_class) != NULL) {
            return 1;
        }
        
        parent_id = get_json_number(ancestor, "parent_id", -1);
        if (parent_id == -1) break;
        ancestor = find_element_by_id(global_computed_layout, parent_id);
    }
    
    return 0;
}

// Helper function to find an element by ID
cJSON* find_element_by_id(cJSON *root, int target_id) {
    if (!root) return NULL;
    
    // If root is an array, search each item
    if (cJSON_IsArray(root)) {
        cJSON *item;
        cJSON_ArrayForEach(item, root) {
            cJSON *found = find_element_by_id(item, target_id);
            if (found) return found;
        }
        return NULL;
    }
    
    // Check if this element has the target ID
    int id = get_json_number(root, "element_id", -1);
    if (id == target_id) return root;
    
    // Check children recursively
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            cJSON *found = find_element_by_id(child, target_id);
            if (found) return found;
        }
    }
    
    return NULL;
}

cJSON* find_element_by_string_id(cJSON *root, const char *target_id) {
    if (!root || !target_id) return NULL;
    
    // If root is an array, get the body element
    if (cJSON_IsArray(root)) {
        cJSON *body = cJSON_GetArrayItem(root, 0);
        if (!body) return NULL;
        return find_element_by_string_id(body, target_id);
    }
    
    // Check current element
    const char *elem_id = get_json_string(root, "id", NULL);
    if (elem_id && strcmp(elem_id, target_id) == 0) {
        return root;
    }
    
    // Try numeric id
    int num_id = atoi(target_id);
    if (num_id > 0) {
        int elem_num_id = get_json_number(root, "id", -1);
        if (elem_num_id == num_id) {
            return root;
        }
    }
    
    // Recursively check children
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            cJSON *found = find_element_by_string_id(child, target_id);
            if (found) return found;
        }
    }
    
    return NULL;
}



cJSON* find_element_by_element_id(cJSON *root, const char *target_id) {
    if (!root || !target_id) return NULL;
    
    if (cJSON_IsArray(root)) {
        cJSON *body = cJSON_GetArrayItem(root, 0);
        if (!body) return NULL;
        return find_element_by_element_id(body, target_id);
    }
    
    // Proveri "element_id"
    const char *elem_id = get_json_string(root, "element_id", NULL);
    if (elem_id && strcmp(elem_id, target_id) == 0) {
        return root;
    }
    
    // Proveri "id" (za backward kompatibilnost)
    elem_id = get_json_string(root, "id", NULL);
    if (elem_id && strcmp(elem_id, target_id) == 0) {
        return root;
    }
    
    // Numerička provera
    int num_id = atoi(target_id);
    if (num_id > 0) {
        int elem_num_id = get_json_number(root, "element_id", -1);
        if (elem_num_id == num_id) return root;
        elem_num_id = get_json_number(root, "id", -1);
        if (elem_num_id == num_id) return root;
    }
    
    // Rekurzivno pretraživanje
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            cJSON *found = find_element_by_element_id(child, target_id);
            if (found) return found;
        }
    }
    
    return NULL;
}



cJSON* find_parent_element_by_id(cJSON *root, int target_id) {
    if (!root) return NULL;
    
    // Check if this element has the target ID
    int id = get_json_number(root, "parent_id", -1);
    if (id == target_id) return root;
    
    // Check children
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            cJSON *found = find_parent_element_by_id(child, target_id);
            if (found) return found;
        }
    }
    
    return NULL;
}

void layout_list_item(cJSON *element, LayoutContext *ctx)
{
 // ako je dugme, preskoci:
 if (get_json_bool(element, "is_button", 0)) {
    layout_inline_block_element(element, ctx);
    return;
}

    static int li_counter = 0;
    li_counter++;

    const char *orientation = get_json_string(element, "menu_orientation", NULL);
    int is_menu_item = (orientation != NULL);
    int is_horizontal = (is_menu_item && strcmp(orientation, "horizontal") == 0);
    
    int margin_left = get_json_number(element, "margin_left", 0);
    int margin_top  = get_json_number(element, "margin_top", 2);
    int margin_right = get_json_number(element, "margin_right", 0);
    int indent = is_menu_item ? 0 : 20;

    int x, y;
    int item_width = 0;

    // ===== CALCULATE WIDTH FROM CONTENT =====
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            const char *child_tag = get_json_string(child, "tag", "");
            
            if (strcmp(child_tag, "a") == 0) {
                cJSON *a_children = cJSON_GetObjectItem(child, "children");
                if (a_children && cJSON_IsArray(a_children)) {
                    cJSON *text_child;
                    cJSON_ArrayForEach(text_child, a_children) {
                        if (strcmp(get_json_string(text_child, "tag", ""), "text") == 0) {
                            const char *text = get_json_string(text_child, "content", "");
                            int font_size = get_json_number(text_child, "font_size", 16);
                            const char *weight = get_json_string(text_child, "font_weight", "normal");
                            const char *style = get_json_string(text_child, "font_style", "normal");
                            
                            item_width = estimate_text_width(text, font_size, weight, style);
                            item_width += margin_left + margin_right;
                            
                            int padding_left = get_json_number(element, "padding_left", 0);
                            int padding_right = get_json_number(element, "padding_right", 0);
                            item_width += padding_left + padding_right;
                            break;
                        }
                    }
                }
            } else if (strcmp(child_tag, "text") == 0) {
                const char *text = get_json_string(child, "content", "");
                int font_size = get_json_number(child, "font_size", 16);
                const char *weight = get_json_string(child, "font_weight", "normal");
                const char *style = get_json_string(child, "font_style", "normal");
                
                item_width = estimate_text_width(text, font_size, weight, style);
                item_width += margin_left + margin_right;
                break;
            }
        }
    }
    
    if (item_width == 0) {
        item_width = 80;
    }

    // -----------------------------
    // HORIZONTAL MENU
    // -----------------------------
    if (is_menu_item && is_horizontal) {
        x = ctx->current_x + margin_left + indent;
        y = ctx->parent_y + margin_top;
        
        ctx->current_x = x + item_width + margin_right + 10;
    }
    // -----------------------------
    // VERTICAL MENU OR NORMAL LIST
    // -----------------------------
    else
    {
        x = ctx->parent_x + margin_left + indent;
        y = ctx->current_y + margin_top;
        
        // FOR VERTICAL MENU: Use full container width for the li
        int full_width = ctx->container_width;
        
        set_json_number(element, "x", x);
        set_json_number(element, "y", y);
        set_json_number(element, "width", full_width);  // ← Full width, not text width
        set_json_string(element, "display", "block");
        
        // Update current_y for next item (will be adjusted after child layout)
        ctx->current_y = y + 22;
    }

    // For horizontal menu, store width here
    if (is_menu_item && is_horizontal) {
        set_json_number(element, "x", x);
        set_json_number(element, "y", y);
        set_json_number(element, "width", item_width);
        set_json_string(element, "display", "inline-block");
    }

    int height = 20;

    // -----------------------------
    // CHILD LAYOUT (for links inside li)
    // -----------------------------
    children = cJSON_GetObjectItem(element, "children");

    if (children && cJSON_IsArray(children))
    {
        LayoutContext child_ctx = *ctx;
        child_ctx.parent_x = x;
        child_ctx.parent_y = y;
        child_ctx.current_x = x;
        child_ctx.current_y = y;
        
        // Use full width for vertical menu children
        int child_width = is_horizontal ? item_width : ctx->container_width;
        child_ctx.container_width = child_width;
        child_ctx.available_width = child_width;
        cJSON *child;
        cJSON_ArrayForEach(child, children)
        {
            racunaj_pozicije(child, x, y, child_width, &child_ctx);
        }

        int max_child_bottom = y;
        cJSON_ArrayForEach(child, children)
        {
            int cy = get_json_number(child, "y", 0);
            int ch = get_json_number(child, "height", 0);
            if (cy + ch > max_child_bottom) {
                max_child_bottom = cy + ch;
            }
        }

        height = max_child_bottom - y;
        
        // Update current_y with actual height for vertical menu
        if (!is_horizontal) {
            ctx->current_y = y + height;
        }
    }

    set_json_number(element, "height", height);

    // Bullet support only for non-menu lists
    if (!is_menu_item)
    {
        const char *list_style = get_json_string(ctx->parent_element, "list_style_type", "disc");
        const char *bullet = "*";
        if (strcmp(list_style, "circle") == 0) bullet = "o";
        else if (strcmp(list_style, "square") == 0) bullet = "#";
        else if (strcmp(list_style, "none") == 0) bullet = "";
    
        set_json_string(element, "bullet_char", bullet);
        set_json_number(element, "bullet_x", x - 15);
        set_json_number(element, "bullet_y", y);
    }
}

// Detect if a menu should be treated as a sidebar
int is_sidebar_candidate(cJSON *element, LayoutContext *ctx) {
    if (!element) return 0;
    
    const char *tag = get_json_string(element, "tag", "");
    
    // Check if it's a menu container (nav, ul, or has menu items)
    int is_menu_container = 0;
    
    if (strcmp(tag, "nav") == 0 || strcmp(tag, "menu") == 0) {
        is_menu_container = 1;
    }
    else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
        // Check if it contains list items with links (menu structure)
        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            cJSON *child;
            cJSON_ArrayForEach(child, children) {
                const char *child_tag = get_json_string(child, "tag", "");
                if (strcmp(child_tag, "li") == 0) {
                    is_menu_container = 1;
                    break;
                }
            }
        }
    }
    
    if (!is_menu_container) return 0;
    
    // Check orientation (must be vertical - not horizontal flex)
    const char *flex_direction = get_json_string(element, "flex_direction", "");
    if (strcmp(flex_direction, "row") == 0) {
        return 0;  // Horizontal flex menu
    }
    
    // Check if it's the first or last child in parent (sidebar position)
    cJSON *parent = ctx->parent_element;
    if (parent) {
        cJSON *parent_children = cJSON_GetObjectItem(parent, "children");
        if (parent_children && cJSON_IsArray(parent_children)) {
            int child_count = cJSON_GetArraySize(parent_children);
            for (int i = 0; i < child_count; i++) {
                cJSON *child = cJSON_GetArrayItem(parent_children, i);
                if (child == element) {
                    // First child -> potential left sidebar
                    // Last child -> potential right sidebar
                    if (i == 0 || i == child_count - 1) {
                        return 1;
                    }
                    break;
                }
            }
        }
    }
    
    return 0;
}


void layout_sidebar_element(cJSON *element, LayoutContext *ctx) {
    if (!element) return;
    
    // Determine if left or right sidebar
    int is_left = 1;
    cJSON *parent = ctx->parent_element;
    
    if (parent) {
        cJSON *parent_children = cJSON_GetObjectItem(parent, "children");
        if (parent_children && cJSON_IsArray(parent_children)) {
            int child_count = cJSON_GetArraySize(parent_children);
            for (int i = 0; i < child_count; i++) {
                cJSON *child = cJSON_GetArrayItem(parent_children, i);
                if (child == element) {
                    is_left = (i == 0);
                    break;
                }
            }
        }
    }
    
    // Get sidebar width
    int sidebar_width = get_json_number(element, "sidebar_width", 200);
    
    int x = is_left ? ctx->parent_x : ctx->parent_x + ctx->container_width - sidebar_width;
    int y = ctx->current_y;
    
    set_json_number(element, "x", x);
    set_json_number(element, "y", y);
    set_json_number(element, "width", sidebar_width);
    
  //  printf("📐 SIDEBAR: x=%d, y=%d, width=%d\n", x, y, sidebar_width);
    
    // Layout sidebar children vertically
    cJSON *children = cJSON_GetObjectItem(element, "children");
    int sidebar_bottom = y;
    
    if (children && cJSON_IsArray(children)) {
        // Don't create a copy - use the original ctx but with modified parent values
        int original_parent_x = ctx->parent_x;
        int original_container_width = ctx->container_width;
        
        ctx->parent_x = x;
        ctx->container_width = sidebar_width;
        ctx->current_x = x;
        ctx->current_y = y;
        
        cJSON *item;
        int item_count = 0;
        cJSON_ArrayForEach(item, children) {
            racunaj_pozicije(item, x, ctx->current_y, sidebar_width, ctx);

            // ========== ADD TOP BORDER TO LI ITEMS (except first) ==========
    // Check if this item is a ul element
    if (strcmp(get_json_string(item, "tag", ""), "ul") == 0) {
        cJSON *li_items = cJSON_GetObjectItem(item, "children");
        if (li_items && cJSON_IsArray(li_items)) {
            int li_index = 0;
            cJSON *li;
            cJSON_ArrayForEach(li, li_items) {
                if (li_index > 0) {
                    set_json_string(li, "border_top_width", "1px");
                    set_json_string(li, "border_top_style", "solid");
                    set_json_string(li, "border_top_color", "#555555");
                }
                li_index++;
            }
        }
    }
    // ================================================================
            item_count++;
        }
        
        sidebar_bottom = ctx->current_y;
        int sidebar_height = sidebar_bottom - y;
        
        // Add padding
        int padding_top = get_json_number(element, "padding_top", 0);
        int padding_bottom = get_json_number(element, "padding_bottom", 0);
        sidebar_height = sidebar_height + padding_top + padding_bottom;
        
        if (sidebar_height < 50) sidebar_height = 50;
        
        set_json_number(element, "height", sidebar_height);
        
     //   printf("  SIDEBAR: y=%d, bottom=%d, height=%d\n", y, sidebar_bottom, sidebar_height);
        
        // Restore original context values
        ctx->parent_x = original_parent_x;
        ctx->container_width = original_container_width;
    } else {
        set_json_number(element, "height", 200);
    }
    
    // Ensure background color is set
    const char *bg_color = get_json_string(element, "bg_color", "");
    if (strlen(bg_color) == 0 || strcmp(bg_color, "#FFFFFF") == 0) {
        set_json_string(element, "bg_color", "#444");
    }
    
    // Update context for main content
    if (is_left) {
        ctx->parent_x += sidebar_width;
        ctx->container_width -= sidebar_width;
        ctx->current_y = y;
    } else {
        ctx->container_width -= sidebar_width;
    }
}


void racunaj_pozicije(cJSON *element,
    int my_x,
    int my_y,
    int container_width,
    LayoutContext *ctx)
{
    if (!element) return;

    // ==========================================================
    // 1. BASIC PROPERTIES - USE TAG + DISPLAY, NOT TYPE!
    // ==========================================================

    const char *tag     = get_json_string(element, "tag", "");
    const char *display = get_json_string(element, "display", "block");
    const char *type    = get_json_string(element, "type", "");
    
    // ==========================================================
    // 2. INITIALIZE CONTEXT FOR THIS ELEMENT
    // ==========================================================

    ctx->parent_x        = my_x;
    ctx->parent_y        = my_y;
    ctx->container_width = container_width;
    ctx->parent_element  = element;

    if (ctx->current_x == 0 && ctx->current_y == 0) {
        ctx->current_x = my_x;
        ctx->current_y = my_y;
    }

      // ==========================================================
    // 3. LAYOUT THIS ELEMENT ITSELF - BASED ON TAG + DISPLAY
    // ==========================================================
    // Skip elements with display: none
    //  ULTRA-PAMETAN VISIBILITY GATE

    display = get_json_string(element, "display", "block");
    
    // =========================================================================
    // 🚀 GEOMETRIJSKI ŠTIT: Skuplja ćelije tabele i sklanja skriveni otpad
    // =========================================================================
    if (strcmp(tag, "td") == 0 || strcmp(tag, "span") == 0) {
        // Prisno menjamo display u inline-block da ćelije tabele i spanovi oko dugmadi
        // ne bi lomili redove i gurali elemente naniže!
        display = "inline-block";
        set_json_string(element, "display", "inline-block");
    }

    // Ako je u pitanju skriveni input, poništavamo mu prostor da ne gura input polje 'q' desno
    const char *input_type_attr = get_json_string(element, "input_type", "");
    if (strcmp(tag, "input") == 0 && strcmp(input_type_attr, "hidden") == 0) {
        set_json_number(element, "width", 0);
        set_json_number(element, "height", 0);
        set_json_number(element, "layout_calculated", 1);
        return; // Puni prekid za hidden polja!
    }
    
    // 🛡️ KONAČNI REKURZIVNI OSIGURAČ (Proširen za Next.js skrivene x/y elemente)
    const char *this_input_type = get_json_string(element, "input_type", "");
    if (strcmp(display, "none") == 0 || 
        strcmp(this_input_type, "hidden") == 0 ||
        get_json_bool(element, "is_visible", 1) == 0 ||
        get_json_number(element, "x", 0) < -5000 || 
        get_json_number(element, "y", 0) < -5000) {
        
        set_json_number(element, "x", my_x);
        set_json_number(element, "y", my_y);
        set_json_number(element, "width", 0);
        set_json_number(element, "height", 0);
        set_json_number(element, "layout_calculated", 1);
        return; // ⚡ BOOM! Odsecamo rekurziju ovde i štedimo CPU vreme u QEMU!
    }
    
    if (strcmp(tag, "text") == 0 && strcmp(type, "text") == 0) {
        layout_text_node(element, ctx);
        return;
    }
    
    else if (strcmp(tag, "br") == 0) {
        layout_break_element(element, ctx);
        return;
    }
    else if (strcmp(tag, "table") == 0) {
        layout_table_element(element, ctx);
            // ===== OČISTI ROWSPAN MATRICU NAKON TABELE =====
    cleanup_table_layout(ctx);
        return;
    }
    else if (strcmp(tag, "thead") == 0 || 
             strcmp(tag, "tbody") == 0 || 
             strcmp(tag, "tfoot") == 0) {
        layout_table_section(element, ctx);
        return;
    }
    else if (strcmp(tag, "tr") == 0) {
        layout_table_row(element, ctx);
        return;
    }
    else if (strcmp(tag, "li") == 0) {
        layout_list_item(element, ctx);
    }
    else if (get_json_bool(element, "is_sidebar", 0)) {
        const char *sidebar_place = get_json_string(element, "sidebar_place", "none");
        if (strcmp(sidebar_place, "left") == 0) {
            layout_sidebar_element(element, ctx);
            ctx->sidebar_y = get_json_number(element, "y", 0);
            ctx->has_sidebar = 1;
            return;
        } else if (strcmp(sidebar_place, "right") == 0) {
            layout_sidebar_element(element, ctx);
            return;
        }
    }
    else if (get_json_bool(element, "is_button", 0)) {
        // Treat button as inline element
        layout_inline_element(element, ctx);
    }
    else if (strcmp(display, "inline") == 0 && strcmp(tag, "text") != 0) {
        layout_inline_element(element, ctx);
    }

    else if (get_json_bool(element, "is_textarea", 0)) {

        // Treat textarea as inline-block
        layout_inline_block_element(element, ctx);
    }

    else if (strcmp(display, "inline-block") == 0) {
        layout_inline_block_element(element, ctx);
    }
    else if (strcmp(display, "flex") == 0) {
        layout_flex_element(element, ctx);  // ← Use layout_flex_element, not layout_flex_container
        return;
    }
    else if (strcmp(tag, "details") == 0) {
    // Treat as block element
    layout_block_element(element, ctx);
    return;
}
    else {
        layout_block_element(element, ctx);
        const char *float_prop = get_json_string(element, "float", "");
        if (strcmp(float_prop, "left") == 0 || strcmp(float_prop, "right") == 0) {
            return;
        }
    }

    // ==========================================================
    // 4. GENERIC CHILD PROCESSING
    // ==========================================================
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (!children || !cJSON_IsArray(children))
        return;
    
    cJSON *child;  // Declare child for loops
    
    int this_x = get_json_number(element, "x", my_x);
    int this_y = get_json_number(element, "y", my_y);
    
    int padding_left   = get_json_number(element, "padding_left", 0);
    int padding_top    = get_json_number(element, "padding_top", 0);
    int padding_right  = get_json_number(element, "padding_right", 0);
   // int padding_bottom = get_json_number(element, "padding_bottom", 0);
    int border_width   = get_json_number(element, "border_width", 0);
    
    int content_x = this_x + padding_left + border_width;
    int content_y = this_y + padding_top + border_width;
    int content_width = container_width - padding_left - padding_right - border_width * 2;
    
    int max_x = content_x;
    int max_y = content_y;
    int flow_x = content_x;
    int flow_y = content_y;
    int line_height = 0;
   // int parent_y_saved = ctx->current_y;
    
  // ========== PRE-PASS: Find and layout horizontal menu FIRST ==========
cJSON *horizontal_menu = NULL;
int menu_index = -1;
int child_index = 0;

cJSON_ArrayForEach(child, children) {
    const char *child_tag = get_json_string(child, "tag", "");
    const char *menu_orientation = get_json_string(child, "menu_orientation", "");
    
    if ((strcmp(child_tag, "nav") == 0 || strcmp(child_tag, "menu") == 0) &&
        strcmp(menu_orientation, "horizontal") == 0) {
        horizontal_menu = child;
        menu_index = child_index;
        break;
    }
    child_index++;
}

// Only pre-layout if the horizontal menu is the FIRST child (index 0)
if (horizontal_menu && menu_index == 0) {
    int original_content_x = content_x;
    int original_content_width = content_width;
    
    LayoutContext menu_ctx = *ctx;
    menu_ctx.parent_x = content_x;
    menu_ctx.parent_y = flow_y;
    menu_ctx.container_width = content_width;
    menu_ctx.current_x = content_x;
    menu_ctx.current_y = flow_y;
    
    layout_block_element(horizontal_menu, &menu_ctx);
    
    int menu_y = get_json_number(horizontal_menu, "y", 0);
    int menu_height = get_json_number(horizontal_menu, "height", 0);
    int menu_margin_bottom = get_json_number(horizontal_menu, "margin_bottom", 0);
    
    flow_y = menu_y + menu_height + menu_margin_bottom;
    
    if (content_x + content_width > max_x) max_x = content_x + content_width;
    if (flow_y > max_y) max_y = flow_y;
    
    content_x = original_content_x;
    content_width = original_content_width;
    flow_x = content_x;
}
    
    // ========== FIRST PASS: Find sidebars ==========
    cJSON *left_sidebar = NULL;
    cJSON *right_sidebar = NULL;
    
    cJSON_ArrayForEach(child, children) {
        if (child == horizontal_menu) continue;
        
        if (get_json_bool(child, "is_sidebar", 0)) {
            const char *place = get_json_string(child, "sidebar_place", "none");
            if (strcmp(place, "left") == 0) {
                left_sidebar = child;
            } else if (strcmp(place, "right") == 0) {
                right_sidebar = child;
            } else if (!left_sidebar) {
                left_sidebar = child;
            } else if (!right_sidebar) {
                right_sidebar = child;
            }
        }
    }
    
// ========== Process Left Sidebar ==========
if (left_sidebar) {
    const char *sidebar_display = get_json_string(left_sidebar, "display", "block");
    if (strcmp(sidebar_display, "none") == 0) {
        set_json_number(left_sidebar, "width", 0);
        set_json_number(left_sidebar, "height", 0);
        set_json_number(left_sidebar, "layout_calculated", 1);
        left_sidebar = NULL; // Prevent further processing
    }
}
if (left_sidebar) {
    int sidebar_width = get_json_number(left_sidebar, "sidebar_width", 200);
    int sidebar_x = content_x;
    int sidebar_y = flow_y;
    
    set_json_number(left_sidebar, "x", sidebar_x);
    set_json_number(left_sidebar, "y", sidebar_y);
    set_json_number(left_sidebar, "width", sidebar_width);
    
    // Layout sidebar children
    LayoutContext sidebar_ctx = *ctx;
    sidebar_ctx.parent_x = sidebar_x;
    sidebar_ctx.parent_y = sidebar_y;
    sidebar_ctx.container_width = sidebar_width;
    sidebar_ctx.current_x = sidebar_x;
    sidebar_ctx.current_y = sidebar_y;
    
    // Track first and last li positions
    int first_li_y = 0;
    int last_li_bottom = 0;
    int li_count = 0;
    
    cJSON *sidebar_children = cJSON_GetObjectItem(left_sidebar, "children");
    if (sidebar_children && cJSON_IsArray(sidebar_children)) {
        cJSON *item;
        cJSON_ArrayForEach(item, sidebar_children) {
            racunaj_pozicije(item, sidebar_x, sidebar_ctx.current_y, sidebar_width, &sidebar_ctx);
            
            // Find the ul inside
            if (strcmp(get_json_string(item, "tag", ""), "ul") == 0) {
                cJSON *ul_children = cJSON_GetObjectItem(item, "children");
                if (ul_children && cJSON_IsArray(ul_children)) {
                    cJSON *li;
                    cJSON_ArrayForEach(li, ul_children) {
                        int li_y = get_json_number(li, "y", 0);
                        int li_h = get_json_number(li, "height", 0);
                        int li_bottom = li_y + li_h;
                        
                        if (li_count == 0) first_li_y = li_y;
                        last_li_bottom = li_bottom;
                        li_count++;
                    }
                }
            }
        }
    }
    
  // Calculate height from li positions
  int padding_bottom = get_json_number(left_sidebar, "padding_bottom", 0);
  int sidebar_height = last_li_bottom - first_li_y - padding_bottom;

// Don't add padding_bottom again - the li positions already include their spacing
// Just ensure minimum height
if (sidebar_height < 50) sidebar_height = 50;

set_json_number(left_sidebar, "height", sidebar_height);

if (sidebar_x + sidebar_width > max_x) max_x = sidebar_x + sidebar_width;
if (sidebar_y + sidebar_height > max_y) max_y = sidebar_y + sidebar_height;
    
    content_x += sidebar_width;
    content_width -= sidebar_width;
    flow_x = content_x;
}

// ========== Process Right Sidebar ==========
if (right_sidebar) {
    const char *sidebar_display = get_json_string(right_sidebar, "display", "block");
    if (strcmp(sidebar_display, "none") == 0) {
        set_json_number(right_sidebar, "width", 0);
        set_json_number(right_sidebar, "height", 0);
        set_json_number(right_sidebar, "layout_calculated", 1);
        right_sidebar = NULL; // Prevent further processing
    }
}
if (right_sidebar) {
    int sidebar_width = get_json_number(right_sidebar, "sidebar_width", 200);
    int sidebar_x = content_x + content_width - sidebar_width;
    int sidebar_y = flow_y;
    
    set_json_number(right_sidebar, "x", sidebar_x);
    set_json_number(right_sidebar, "y", sidebar_y);
    set_json_number(right_sidebar, "width", sidebar_width);
    
    LayoutContext sidebar_ctx = *ctx;
    sidebar_ctx.parent_x = sidebar_x;
    sidebar_ctx.parent_y = sidebar_y;
    sidebar_ctx.container_width = sidebar_width;
    sidebar_ctx.current_x = sidebar_x;
    sidebar_ctx.current_y = sidebar_y;
    
    cJSON *sidebar_children = cJSON_GetObjectItem(right_sidebar, "children");
    if (sidebar_children && cJSON_IsArray(sidebar_children)) {
        cJSON *item;
        cJSON_ArrayForEach(item, sidebar_children) {
            racunaj_pozicije(item, sidebar_x, sidebar_ctx.current_y, sidebar_width, &sidebar_ctx);
        }
    }
    
    int sidebar_height = sidebar_ctx.current_y - sidebar_y;
    set_json_number(right_sidebar, "height", sidebar_height);
    
    if (sidebar_x + sidebar_width > max_x) max_x = sidebar_x + sidebar_width;
    if (sidebar_y + sidebar_height > max_y) max_y = sidebar_y + sidebar_height;
    
    content_width -= sidebar_width;
}

// ========== SECOND PASS: Process remaining children ==========
    cJSON_ArrayForEach(child, children) {

        if (child == horizontal_menu || child == left_sidebar || child == right_sidebar) {
            continue;
        }
        
        const char *child_display = get_json_string(child, "display", "block");
        const char *child_tag = get_json_string(child, "tag", "");
        const char *child_input_type = get_json_string(child, "input_type", "");

        // 🚀 DRIFT RECOVERY: Force center search box if it's drifting
        const char* child_name = get_json_string(child, "name", "");
        if (strcmp(child_tag, "input") == 0 && strcmp(child_name, "q") == 0) {
            int q_width = get_json_number(child, "width", 0);
            if (q_width > 400) {
                flow_x = content_x + (content_width - q_width) / 2;
                if (flow_x < content_x) flow_x = content_x;
            }
        }

        // 🚀 HARD RESET for hidden or none-display elements to prevent accumulation
        if (strcmp(child_display, "none") == 0 || strcmp(child_input_type, "hidden") == 0) {
            set_json_number(child, "width", 0);
            set_json_number(child, "height", 0);
            set_json_number(child, "layout_calculated", 1);
            continue;  // Skip processing and horizontal/vertical movement
        }
         
        child_tag = get_json_string(child, "tag", "");

                // ==========  Skip text nodes inside output elements ==========
                if (get_json_bool(element, "is_output", 0) && strcmp(child_tag, "text") == 0) {
                    continue;  // Output elements render their own text, don't layout child text nodes
                }
                // ================================================================

        int margin_top    = get_json_number(child, "margin_top", 0);
        int margin_bottom = get_json_number(child, "margin_bottom", 0);
        int margin_right  = get_json_number(child, "margin_right", 0);
        int child_x, child_y;

        child_display = get_json_string(child, "display", "");
        bool child_inline = (strcmp(child_display, "inline") == 0);
        bool is_inline_block = (strcmp(child_display, "inline-block") == 0);
        bool is_text_node = (strcmp(child_tag, "text") == 0);
        
        if (strcmp(child_tag, "br") == 0) {
            flow_y += line_height;
            flow_x = content_x;
            line_height = 0;
            child_x = flow_x;
            child_y = flow_y;
            LayoutContext child_ctx = *ctx;
            child_ctx.current_x = child_x;
            child_ctx.current_y = child_y;
            racunaj_pozicije(child, child_x, child_y, content_width, &child_ctx);
            continue;
        }

   
        if (is_text_node || child_inline || is_inline_block) {
            // 🚀 INLINE SHIELD: Ako je dugme i širina mu je 0, bezbedno mu dodajemo procenjenu širinu preko tvoje funkcije
            int current_width = get_json_number(child, "width", 0);
            bool is_button_elem = get_json_bool(child, "is_button", 0) || strcmp(get_json_string(child, "tag", ""), "button") == 0;
            
            if (current_width == 0 && is_button_elem) {
                cJSON *btn_children = cJSON_GetObjectItem(child, "children");
                if (btn_children && cJSON_IsArray(btn_children)) {
                    cJSON *btn_text = cJSON_GetArrayItem(btn_children, 0);
                    const char *txt = get_json_string(btn_text, "content", "Button");
                    current_width = strlen(txt) * 9 + 32; // Brza procena širine dugmeta u pikselima
                } else {
                    current_width = 120;
                }
                // Tvoja funkcija dodaje/prepisuje podatak u cJSON memoriju jer nije postojao!
                set_json_number(child, "width", current_width);
            }

            int current_child_width = get_json_number(child, "width", 0);

            // 🚀 AUTOMATSKI PRELOM REDA (Line-wrapping osigurač):
            // Ako trenutni flow_x + širina dugmeta probijaju ivicu roditelja, skočimo u novi red
            // EXCEPT for buttons which we want to keep together if they are small
            if (flow_x + current_child_width > content_x + content_width && flow_x > content_x) {
                flow_y += (line_height > 0) ? line_height : 24;
                flow_x = content_x;
                line_height = 0;

                // 🚀 BUTTON ALIGNMENT FIX: Center buttons in the new line if they are Google-style
                if (is_button_elem) {
                    // Peek ahead to see total width of consecutive buttons
                    int buttons_total_w = current_child_width;
                    cJSON* next = child->next;
                    while(next) {
                        if (get_json_bool(next, "is_button", 0)) {
                            buttons_total_w += get_json_number(next, "width", 120) + 10;
                            next = next->next;
                        } else break;
                    }
                    if (buttons_total_w < content_width) {
                        flow_x = content_x + (content_width - buttons_total_w) / 2;
                    }
                }
            }

            child_x = flow_x;
            child_y = flow_y;
        } else {
            const char *child_float = get_json_string(child, "float", "none");
            if (strcmp(child_float, "left") == 0) {
                child_x = flow_x;
                child_y = flow_y;
            } else if (ctx->has_floated_left && flow_y < (ctx->floated_y + ctx->floated_height)) {
                child_x = flow_x;
                child_y = flow_y + margin_top;
            } else {
                flow_x = content_x;
                child_x = content_x;
                child_y = flow_y + margin_top;
            }
        }

        
        int child_container_width = content_width;
        if (child_x > content_x) {
            child_container_width = (content_x + content_width) - child_x;
            if (child_container_width < 0) child_container_width = 0;
        }

        LayoutContext child_ctx = *ctx;
        child_ctx.current_x = child_x;
        child_ctx.current_y = child_y;
        if (strcmp(child_tag, "span") == 0 || strcmp(child_tag, "div") == 0 || child_inline) {
            child_ctx.parent_x = child_x;
            child_ctx.parent_y = child_y; // Prosleđujemo ispravnu poziciju deci
        }
        child_ctx.container_width = child_container_width;
        child_ctx.available_width = child_container_width;
        racunaj_pozicije(child, child_x, child_y, child_container_width, &child_ctx);

        ctx->has_floated_left = child_ctx.has_floated_left;
        ctx->floated_left_x = child_ctx.floated_left_x;
        ctx->floated_left_width = child_ctx.floated_left_width;
        ctx->floated_y = child_ctx.floated_y;
        ctx->floated_height = child_ctx.floated_height;
        ctx->current_x = child_ctx.current_x;
        ctx->current_y = child_ctx.current_y;

        int child_width = get_json_number(child, "width", 0);
        int child_height = get_json_number(child, "height", 0);

        if (strcmp(child_tag, "a") == 0) {
            // Always get text from children
            char full_link_text[1024] = {0};
            cJSON *children = cJSON_GetObjectItem(child, "children");
            
            if (children && cJSON_IsArray(children)) {
                for (int j = 0; j < cJSON_GetArraySize(children); j++) {
                    cJSON *text_node = cJSON_GetArrayItem(children, j);
                    const char *node_tag = get_json_string(text_node, "tag", "");
                    
                    if (strcmp(node_tag, "text") == 0) {
                        const char *text_content = get_json_string(text_node, "content", "");
                        if (strlen(text_content) > 0 && strcmp(text_content, "0") != 0) {
                            strcat(full_link_text, text_content);
                        }
                    }
                }
            }
            
            // Fallback to full_text if children had no text
            if (strlen(full_link_text) == 0) {
                const char *full_text = get_json_string(child, "full_text", "");
                if (strlen(full_text) > 0 && strcmp(full_text, "0") != 0) {
                    strcpy(full_link_text, full_text);
                }
            }
            
  }

        int child_actual_x = child_x;
        int child_actual_width = child_width;

        if (!is_text_node && child_inline) {
            cJSON *grandchildren = cJSON_GetObjectItem(child, "children");
            if (grandchildren && cJSON_IsArray(grandchildren) && cJSON_GetArraySize(grandchildren) > 0) {
                cJSON *text_child = cJSON_GetArrayItem(grandchildren, 0);
                child_actual_x = get_json_number(text_child, "x", child_x);
                child_actual_width = get_json_number(text_child, "width", child_width);
            }
        }

        if (child_actual_x + child_actual_width > max_x)
            max_x = child_actual_x + child_actual_width;
        if (child_y + child_height > max_y)
            max_y = child_y + child_height;

        const char *child_float = get_json_string(child, "float", "none");
        bool is_floated_left = (strcmp(child_float, "left") == 0);

        if (is_text_node || child_inline || is_inline_block || is_floated_left) {
            if (is_floated_left) {
                flow_x = child_x + child_width + margin_right + 10;
            } else {
                flow_x = child_actual_x + child_actual_width + margin_right;
            }
            if (child_height > line_height) line_height = child_height;
            
            cJSON *next_child = child->next;
            if (next_child) {
                const char *next_display = get_json_string(next_child, "display", "");
                if (strcmp(next_display, "block") == 0) {
                    flow_y += line_height;
                    line_height = 0;
                }
            }
        } else {
            flow_y = child_y + child_height + margin_bottom;
            flow_x = content_x;
            line_height = 0;
            if (ctx->has_floated_left && flow_y >= (ctx->floated_y + ctx->floated_height)) {
                ctx->has_floated_left = 0;
                flow_x = content_x;
            }
        }
    }


    // =================================================

// ========== UPDATE PARENT HEIGHT FROM CHILDREN ==========
// Calculate the actual bottom position from all children
int actual_bottom = max_y;
if (actual_bottom < this_y + get_json_number(element, "height", 0)) {
    actual_bottom = this_y + get_json_number(element, "height", 0);
}

// Update element height to fit all children
int new_height = actual_bottom - this_y;
if (new_height > 0) {
    int old_height = get_json_number(element, "height", 0);
    if (new_height > old_height) {
        cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(new_height));

      //  printf("📏 Updated %s height: %d -> %d\n", tag_name, old_height, new_height);
    }
}

// Update context for sibling elements - THIS IS CRITICAL!
ctx->current_x = max_x;
ctx->current_y = this_y + new_height;

// Also update parent's current_y if this is a block element
if (strcmp(display, "block") == 0 || strcmp(tag, "p") == 0 || strcmp(tag, "div") == 0) {
    // This ensures the parent's context moves past this block
    // The parent's ctx->current_y will be updated when this function returns
}
}



// Document-level layout
void layout_document(cJSON *document,pauk_ui_t *pauk_ui, font_manager_t *font_mgr) {
    if (!document) return;
    
    // Document viewport dimensions
    int viewport_width = 800;
   // int viewport_height = 600;
    
    // Initialize layout context
    LayoutContext root_ctx = {
        .parent_x = 0,    
        .parent_y = 0,   
        .current_x = 0,
        .current_y = 0,
        .line_height = 0,
        .container_width = viewport_width, 
        .available_width = viewport_width,
        .parent_element = NULL, 
        .previous_block = NULL,   
        .font_manager = font_mgr,
        .pauk_ui = pauk_ui,
        .col_widths = NULL,  // Add this
        .col_count = 0,
        .root_element = document, 
        .current_menu_orientation = NULL,
            // Initialize float tracking
    .has_floated_left = 0,
    .floated_left_x = 0,
    .floated_left_width = 0,
    .floated_y = 0,
    .floated_height = 0,
    // sidebar
    .has_sidebar=0,
    .sidebar_x=0,
    .sidebar_width=0,
    .sidebar_y=0,
    .sidebar_height=0,
    .previous_margin_bottom=0,

    };

    
    // Perform layout starting from document root
    racunaj_pozicije(document, 0, 0, viewport_width, &root_ctx);

}



// Call this after all rendering is complete to get the total content height
int find_document_bottom(cJSON *element) {
    if (!element) return 0;
    
    int max_bottom = 0;
    
    // Handle arrays
    if (cJSON_IsArray(element)) {
        cJSON *child;
        cJSON_ArrayForEach(child, element) {
            int child_bottom = find_document_bottom(child);
            if (child_bottom > max_bottom) max_bottom = child_bottom;
        }
        return max_bottom;
    }
    
    // Handle objects
    if (cJSON_IsObject(element)) {
        // Get this element's bottom position
        int y = get_json_number(element, "y", 0);
        int height = get_json_number(element, "height", 0);
        int bottom = y + height;
        
        if (bottom > max_bottom) max_bottom = bottom;
        
        // Check children
        cJSON *children = cJSON_GetObjectItem(element, "children");
        if (children) {
            int child_bottom = find_document_bottom(children);
            if (child_bottom > max_bottom) max_bottom = child_bottom;
        }
        
        // Special handling for tables - also check table_data
        const char *tag = get_json_string(element, "tag", "");
        if (strcmp(tag, "table") == 0) {
            cJSON *table_data = cJSON_GetObjectItem(element, "table_data");
            if (table_data) {
                // Table height is already in element->height from layout
                // But we can double-check with table_data if needed
                int table_bottom = y + height;
                if (table_bottom > max_bottom) max_bottom = table_bottom;
            }
        }
    }
    
    return max_bottom;
}


void update_scrollbar_ratio(pauk_ui_t *pauk_ui) {
    if (!pauk_ui || !pauk_ui->vscrollbar) return;
    
    // Get viewport height from TAB area
    gfx_coord_t view_height = pauk_ui->tab_rect_base.p1.y - pauk_ui->tab_rect_base.p0.y;
    
    // Calculate actual content height
    int content_height = find_document_bottom(pauk_ui->rendering_json) + 50;
    pauk_ui->content_height = content_height;
    
    // Get scrollbar dimensions
    gfx_coord_t trough_length = ui_scrollbar_trough_length(pauk_ui->vscrollbar);
    
    // Set thumb size based on visible ratio
    if (content_height > view_height) {
        float visible_ratio = (float)view_height / content_height;
        int thumb_height = (int)(trough_length * visible_ratio);
        if (thumb_height < 10) thumb_height = 10;
        ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, thumb_height);
        
        // Calculate conversion factor (pixels per scrollbar unit)
        gfx_coord_t move_length = ui_scrollbar_move_length(pauk_ui->vscrollbar);
        pauk_ui->pixels_per_scroll_unit = (float)(content_height - view_height) / move_length;
        
        // Calculate scroll step (about 1% of content per click)
        int scroll_step_content = content_height / 100;
        if (scroll_step_content < 20) scroll_step_content = 20;
        pauk_ui->scroll_step = (int)(scroll_step_content / pauk_ui->pixels_per_scroll_unit);
        if (pauk_ui->scroll_step < 1) pauk_ui->scroll_step = 1;
        
        // Page step = 1/4 of scrollbar track
        pauk_ui->page_step = move_length / 4;
        if (pauk_ui->page_step < 1) pauk_ui->page_step = 1;
    } else {
        // Content fits - full thumb, no scrolling needed
        ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, trough_length);
        pauk_ui->pixels_per_scroll_unit = 1.0f;
        pauk_ui->scroll_step = 1;
        pauk_ui->page_step = 1;
    }
    
    // Reset scroll position to top
    ui_scrollbar_set_pos(pauk_ui->vscrollbar, 0);
    pauk_ui->scroll_y = 0;
}


void update_scrollbar_ratio_preserve(pauk_ui_t *pauk_ui) {
    if (!pauk_ui || !pauk_ui->vscrollbar) return;
    int sc_y = ui_scrollbar_get_pos(pauk_ui->vscrollbar);
    // Get viewport height from TAB area
    gfx_coord_t view_height = pauk_ui->tab_rect_base.p1.y - pauk_ui->tab_rect_base.p0.y;
    
    // Calculate actual content height
    int content_height = find_document_bottom(pauk_ui->rendering_json) + 50;
    pauk_ui->content_height = content_height;
    
    // Get scrollbar dimensions
    gfx_coord_t trough_length = ui_scrollbar_trough_length(pauk_ui->vscrollbar);
    
    // Set thumb size based on visible ratio
    if (content_height > view_height) {
        float visible_ratio = (float)view_height / content_height;
        int thumb_height = (int)(trough_length * visible_ratio);
        if (thumb_height < 10) thumb_height = 10;
        ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, thumb_height);
        
        // Calculate conversion factor (pixels per scrollbar unit)
        gfx_coord_t move_length = ui_scrollbar_move_length(pauk_ui->vscrollbar);
        pauk_ui->pixels_per_scroll_unit = (float)(content_height - view_height) / move_length;
        
        // Calculate scroll step (about 1% of content per click)
        int scroll_step_content = content_height / 100;
        if (scroll_step_content < 20) scroll_step_content = 20;
        pauk_ui->scroll_step = (int)(scroll_step_content / pauk_ui->pixels_per_scroll_unit);
        if (pauk_ui->scroll_step < 1) pauk_ui->scroll_step = 1;
        
        // Page step = 1/4 of scrollbar track
        pauk_ui->page_step = move_length / 4;
        if (pauk_ui->page_step < 1) pauk_ui->page_step = 1;
    } else {
        // Content fits - full thumb, no scrolling needed
        ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, trough_length);
        pauk_ui->pixels_per_scroll_unit = 1.0f;
        pauk_ui->scroll_step = 1;
        pauk_ui->page_step = 1;
    }
    
    // Reset scroll position to top
    ui_scrollbar_set_pos(pauk_ui->vscrollbar, sc_y);
    pauk_ui->scroll_y = sc_y;
}

// Helper function to find max x + width
static void find_max_right_recursive(cJSON *element, int *max_right) {
    if (!element) return;
    
    int x = get_json_number(element, "x", 0);
    int w = get_json_number(element, "width", 0);
    int right = x + w;
    
    if (right > *max_right) *max_right = right;
    
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            find_max_right_recursive(child, max_right);
        }
    }
}
// Main function
int find_document_right(cJSON *root, int viewport_width) {
    if (!root) return viewport_width;
    
    int max_right = 0;
    int page_width = viewport_width;  // Default to viewport
    
    // Start from body
    if (cJSON_IsArray(root)) {
        for (int i = 0; i < cJSON_GetArraySize(root); i++) {
            cJSON *elem = cJSON_GetArrayItem(root, i);
            const char *tag = get_json_string(elem, "tag", "");
            if (strcmp(tag, "body") == 0) {
                // Check if body already has a width set (from CSS)
                cJSON *width_item = cJSON_GetObjectItem(elem, "width");
                if (width_item && cJSON_IsNumber(width_item)) {
                    page_width = width_item->valueint;
                } else {
                    // No CSS width, set body width to viewport
                    cJSON_ReplaceItemInObject(elem, "width", cJSON_CreateNumber(viewport_width));
                    page_width = viewport_width;
                }
                
                cJSON *children = cJSON_GetObjectItem(elem, "children");
                if (children && cJSON_IsArray(children)) {
                    cJSON *child;
                    
                    // First pass: find max width from all elements
                    cJSON_ArrayForEach(child, children) {
                        find_max_right_recursive(child, &max_right);
                    }
                    
                    // Second pass: update horizontal navs to match page width
                    cJSON_ArrayForEach(child, children) {
                        const char *child_tag = get_json_string(child, "tag", "");
                        if (strcmp(child_tag, "nav") == 0) {
                            const char *orientation = get_json_string(child, "menu_orientation", "");
                            int is_horizontal = (strcmp(orientation, "horizontal") == 0);
                            
                            if (is_horizontal) {
                                cJSON_ReplaceItemInObject(child, "width", cJSON_CreateNumber(page_width));
                            }
                        }
                    }
                }
                break;
            }
        }
    }

    // Return the larger of page width and content width
    return (max_right > page_width) ? max_right : page_width;
}




