// layout_calculator.c
#include "layout_calculator.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

LayoutConfig default_layout_config(void) {
    LayoutConfig config = {
        .viewport_width = 800,
        .viewport_height = 600,
        .default_font_size = 16,
        .line_height_multiplier = 120, // 120% = 1.2
        .margin_between_elements = 10,
        .padding_inside_elements = 8,
        .scrollbar_width = 15
    };
    return config;
}

// Convert CSS length (px, em, rem, %, etc) to pixels
int parse_css_length(const char *length_str, int reference_size) {
    if (!length_str || strlen(length_str) == 0) return 0;
    
    char *endptr;
    double value = strtod(length_str, &endptr);
    if (endptr == length_str) return 0; // Not a number
    
    // Check unit
    if (strstr(endptr, "px") || strlen(endptr) == 0) {
        return (int)value;
    } else if (strstr(endptr, "em")) {
        return (int)(value * reference_size);
    } else if (strstr(endptr, "rem")) {
        return (int)(value * 16); // Root font size = 16px
    } else if (strstr(endptr, "%")) {
        return (int)((value / 100.0) * reference_size);
    } else if (strstr(endptr, "vw")) {
        // Viewport width percentage
        return (int)((value / 100.0) * 800); // Default viewport
    } else if (strstr(endptr, "vh")) {
        // Viewport height percentage
        return (int)((value / 100.0) * 600); // Default viewport
    }
    
    return (int)value; // Default assumption: pixels
}

// Convert color string to hex
const char* color_to_hex(const char *color_str) {
    static char hex_buffer[10];
    
    if (!color_str) return "#000000";
    
    // If already hex
    if (color_str[0] == '#') {
        // Validate hex length
        size_t len = strlen(color_str);
        if (len == 4 || len == 7 || len == 9) {
            return color_str;
        }
    }
    
    // Named colors (simplified)
    if (strcasecmp(color_str, "black") == 0) return "#000000";
    if (strcasecmp(color_str, "white") == 0) return "#FFFFFF";
    if (strcasecmp(color_str, "red") == 0) return "#FF0000";
    if (strcasecmp(color_str, "green") == 0) return "#00FF00";
    if (strcasecmp(color_str, "blue") == 0) return "#0000FF";
    if (strcasecmp(color_str, "gray") == 0 || strcasecmp(color_str, "grey") == 0) 
        return "#808080";
    if (strcasecmp(color_str, "lightgray") == 0 || strcasecmp(color_str, "lightgrey") == 0) 
        return "#D3D3D3";
    if (strcasecmp(color_str, "darkgray") == 0 || strcasecmp(color_str, "darkgrey") == 0) 
        return "#A9A9A9";
    if (strcasecmp(color_str, "yellow") == 0) return "#FFFF00";
    if (strcasecmp(color_str, "cyan") == 0) return "#00FFFF";
    if (strcasecmp(color_str, "magenta") == 0) return "#FF00FF";
    
    // RGB/RGBA format
    if (strncasecmp(color_str, "rgb(", 4) == 0) {
        int r, g, b;
        if (sscanf(color_str, "rgb(%d, %d, %d)", &r, &g, &b) == 3) {
            snprintf(hex_buffer, sizeof(hex_buffer), "#%02X%02X%02X", r, g, b);
            return hex_buffer;
        }
    }
    
    // Default to black
    return "#000000";
}

// Calculate font size in pixels
int font_size_to_pixels(const char *size_str, int parent_font_size) {
    if (!size_str || strlen(size_str) == 0) return parent_font_size;
    
    return parse_css_length(size_str, parent_font_size);
}

// Apply CSS box model (margin, border, padding)
void apply_css_box_model(cJSON *element) {
    if (!element) return;
    
    int width = 0, height = 0;
    
    // Get content dimensions
    cJSON *content_width = cJSON_GetObjectItem(element, "content_width");
    cJSON *content_height = cJSON_GetObjectItem(element, "content_height");
    
    if (content_width && cJSON_IsNumber(content_width)) {
        width = content_width->valueint;
    }
    if (content_height && cJSON_IsNumber(content_height)) {
        height = content_height->valueint;
    }
    
    // Add padding
    cJSON *padding_left = cJSON_GetObjectItem(element, "padding_left");
    cJSON *padding_right = cJSON_GetObjectItem(element, "padding_right");
    cJSON *padding_top = cJSON_GetObjectItem(element, "padding_top");
    cJSON *padding_bottom = cJSON_GetObjectItem(element, "padding_bottom");
    
    if (padding_left && cJSON_IsNumber(padding_left)) width += padding_left->valueint;
    if (padding_right && cJSON_IsNumber(padding_right)) width += padding_right->valueint;
    if (padding_top && cJSON_IsNumber(padding_top)) height += padding_top->valueint;
    if (padding_bottom && cJSON_IsNumber(padding_bottom)) height += padding_bottom->valueint;
    
    // Add border
    cJSON *border_width = cJSON_GetObjectItem(element, "border_width");
    if (border_width && cJSON_IsNumber(border_width)) {
        int border = border_width->valueint;
        width += border * 2;
        height += border * 2;
    }
    
    // Store final dimensions
    cJSON_AddNumberToObject(element, "total_width", width);
    cJSON_AddNumberToObject(element, "total_height", height);
}

// Process a single element for layout
cJSON* process_element_layout(cJSON *element, LayoutConfig *config, 
                              int parent_x, int parent_y, 
                              int parent_width, int parent_height,
                              int *current_y) {
    if (!element) return NULL;
    
    // Skip non-rendering elements
    cJSON *visible = cJSON_GetObjectItem(element, "visible");
    if (visible && cJSON_IsFalse(visible)) {
        return element;
    }
    
    cJSON *display = cJSON_GetObjectItem(element, "display");
    const char *display_str = display && cJSON_IsString(display) ? 
                              display->valuestring : "block";
    
  //  cJSON *position = cJSON_GetObjectItem(element, "position");
    //const char *position_str = position && cJSON_IsString(position) ? 
     //                          position->valuestring : "static";
    
    // Get element type
   // cJSON *type = cJSON_GetObjectItem(element, "type");
   // const char *type_str = type && cJSON_IsString(type) ? type->valuestring : "block";
    
    cJSON *tag = cJSON_GetObjectItem(element, "tag");
    const char *tag_str = tag && cJSON_IsString(tag) ? tag->valuestring : "div";
    
    // ========== CALCULATE DIMENSIONS ==========
    
    // Default dimensions based on element type
    int width = 0, height = 0;
    
    // Try to get explicit dimensions
    cJSON *width_json = cJSON_GetObjectItem(element, "width");
    cJSON *height_json = cJSON_GetObjectItem(element, "height");
    
    if (width_json && cJSON_IsString(width_json)) {
        width = parse_css_length(width_json->valuestring, parent_width);
    }
    
    if (height_json && cJSON_IsString(height_json)) {
        height = parse_css_length(height_json->valuestring, parent_height);
    }
    
    // If no explicit dimensions, calculate based on content/element type
    if (width == 0) {
        if (strcmp(display_str, "block") == 0) {
            width = parent_width; // Block elements take full width
        } else if (strcmp(display_str, "inline") == 0) {
            // Inline elements size based on content
            cJSON *text = cJSON_GetObjectItem(element, "text");
            if (text && cJSON_IsString(text)) {
                // Rough estimate: 8px per character
                width = strlen(text->valuestring) * 8;
                if (width > parent_width) width = parent_width;
            } else {
                width = 100; // Default inline width
            }
        }
    }
    
    if (height == 0) {
        // Default heights based on element type
        if (strcmp(tag_str, "h1") == 0) height = 48;
        else if (strcmp(tag_str, "h2") == 0) height = 36;
        else if (strcmp(tag_str, "h3") == 0) height = 28;
        else if (strcmp(tag_str, "p") == 0) height = 24;
        else if (strcmp(tag_str, "img") == 0) height = 100; // Default image height
        else height = 30; // Default
    }
    
    // Store content dimensions (before padding/border)
    cJSON_AddNumberToObject(element, "content_width", width);
    cJSON_AddNumberToObject(element, "content_height", height);
    
    // Apply box model
    apply_css_box_model(element);
    
    // Get final dimensions after box model
    cJSON *total_width = cJSON_GetObjectItem(element, "total_width");
    cJSON *total_height = cJSON_GetObjectItem(element, "total_height");
    
    int final_width = total_width ? total_width->valueint : width;
    int final_height = total_height ? total_height->valueint : height;
    
    // ========== CALCULATE POSITION ==========
    
    int x = parent_x, y = *current_y;
    
    // Handle display types
    if (strcmp(display_str, "block") == 0) {
        // Block elements start on new line
        x = parent_x;
        y = *current_y;
        *current_y += final_height + config->margin_between_elements;
    } else if (strcmp(display_str, "inline") == 0) {
        // Inline elements flow with text
        // Simple inline flow: if doesn't fit, move to next line
        static int inline_x = 0;
        if (inline_x + final_width > parent_width) {
            // Move to next line
            inline_x = 0;
            *current_y += final_height + config->margin_between_elements;
        }
        x = parent_x + inline_x;
        y = *current_y;
        inline_x += final_width + config->margin_between_elements;
    }
    
    // Store position
    cJSON_AddNumberToObject(element, "x", x);
    cJSON_AddNumberToObject(element, "y", y);
    cJSON_AddNumberToObject(element, "calculated_width", final_width);
    cJSON_AddNumberToObject(element, "calculated_height", final_height);
    
    // ========== PROCESS CHILDREN ==========
    
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        int child_y = y; // Start children below this element
        
        // Add this element's padding to child starting position
        cJSON *padding_top = cJSON_GetObjectItem(element, "padding_top");
        if (padding_top && cJSON_IsNumber(padding_top)) {
            child_y += padding_top->valueint;
        }
        
        int child_x = x;
        cJSON *padding_left = cJSON_GetObjectItem(element, "padding_left");
        if (padding_left && cJSON_IsNumber(padding_left)) {
            child_x += padding_left->valueint;
        }
        
        // Available space for children (minus padding)
        int child_width = final_width;
        if (padding_left && cJSON_IsNumber(padding_left)) {
            child_width -= padding_left->valueint;
        }
        cJSON *padding_right = cJSON_GetObjectItem(element, "padding_right");
        if (padding_right && cJSON_IsNumber(padding_right)) {
            child_width -= padding_right->valueint;
        }
        
        int child_height = final_height;
        if (padding_top && cJSON_IsNumber(padding_top)) {
            child_height -= padding_top->valueint;
        }
        cJSON *padding_bottom = cJSON_GetObjectItem(element, "padding_bottom");
        if (padding_bottom && cJSON_IsNumber(padding_bottom)) {
            child_height -= padding_bottom->valueint;
        }
        
        // Process each child
        int child_current_y = child_y;
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            if (cJSON_IsObject(child)) {
                process_element_layout(child, config, child_x, child_y, 
                                      child_width, child_height, &child_current_y);
            }
        }
        
        // Update parent height if children expanded it
        int children_height = child_current_y - child_y;
        if (children_height + (padding_top ? padding_top->valueint : 0) + 
            (padding_bottom ? padding_bottom->valueint : 0) > final_height) {
            // Adjust parent height
            final_height = children_height + 
                          (padding_top ? padding_top->valueint : 0) + 
                          (padding_bottom ? padding_bottom->valueint : 0);
            cJSON_ReplaceItemInObject(element, "calculated_height", 
                                     cJSON_CreateNumber(final_height));
        }
    }
    
    return element;
}

// Main function to calculate document layout
cJSON* izracunaj_document_layout(cJSON *document_json, LayoutConfig config) {
    if (!document_json) return NULL;
    
    // Create a copy to avoid modifying original
    cJSON *layout_result = cJSON_Duplicate(document_json, 1);
    if (!layout_result) return NULL;
    
    // Find body or root element
    cJSON *body = NULL;
    
    // If it's an array (your main output), find body element
    if (cJSON_IsArray(layout_result)) {
        int array_size = cJSON_GetArraySize(layout_result);
        for (int i = 0; i < array_size; i++) {
            cJSON *item = cJSON_GetArrayItem(layout_result, i);
            cJSON *tag = cJSON_GetObjectItem(item, "tag");
            if (tag && cJSON_IsString(tag) && strcmp(tag->valuestring, "body") == 0) {
                body = item;
                break;
            }
        }
    } else if (cJSON_IsObject(layout_result)) {
        // Single object, check if it's body
        cJSON *tag = cJSON_GetObjectItem(layout_result, "tag");
        if (tag && cJSON_IsString(tag) && strcmp(tag->valuestring, "body") == 0) {
            body = layout_result;
        }
    }
    
    if (!body) {
        // Create default body
        body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "tag", "body");
        cJSON_AddStringToObject(body, "type", "block");
        cJSON_AddStringToObject(body, "display", "block");
        cJSON_AddStringToObject(body, "bg_color", "#FFFFFF");
        
        if (cJSON_IsArray(layout_result)) {
            // Wrap everything in body
            cJSON *new_array = cJSON_CreateArray();
            cJSON_AddItemToArray(new_array, body);
            cJSON_Delete(layout_result);
            layout_result = new_array;
        }
    }
    
    // Set body dimensions to viewport
    cJSON_AddNumberToObject(body, "x", 0);
    cJSON_AddNumberToObject(body, "y", 0);
    cJSON_AddNumberToObject(body, "content_width", config.viewport_width);
    cJSON_AddNumberToObject(body, "content_height", config.viewport_height);
    apply_css_box_model(body);
    
    // Process body layout
    int start_y = 0;
    process_element_layout(body, &config, 0, 0, 
                          config.viewport_width, config.viewport_height, 
                          &start_y);
    
    // Calculate document height (for scrolling)
    int doc_height = start_y;
    cJSON_AddNumberToObject(layout_result, "document_width", config.viewport_width);
    cJSON_AddNumberToObject(layout_result, "document_height", doc_height);
    
    return layout_result;
}