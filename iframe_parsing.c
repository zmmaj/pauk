

#include "iframe_parsing.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "main.h"

void calculate_iframe_dimensions(cJSON *iframe_json) {
    if (!iframe_json) return;
    
    // Check if already calculated
    cJSON *calc_width = cJSON_GetObjectItem(iframe_json, "calculated_width");
    cJSON *calc_height = cJSON_GetObjectItem(iframe_json, "calculated_height");
    
    if (calc_width && calc_height) {
        return; // Already calculated
    }
    
    int width = 0;
    int height = 0;
    
    // Try to get width/height from iframe-specific attributes first
    cJSON *width_attr = cJSON_GetObjectItem(iframe_json, "iframe_width");
    cJSON *height_attr = cJSON_GetObjectItem(iframe_json, "iframe_height");
    
    // Also check regular width/height attributes
    if (!width_attr) {
        width_attr = cJSON_GetObjectItem(iframe_json, "attr_width");
    }
    if (!height_attr) {
        height_attr = cJSON_GetObjectItem(iframe_json, "attr_height");
    }
    
    // Parse width
    if (width_attr) {
        if (cJSON_IsString(width_attr)) {
            char *width_str = width_attr->valuestring;
            
            // Check for percentage
            if (strstr(width_str, "%")) {
                // For percentage, use default pixel value
                width = 400; // Default width for percentage-based iframes
                cJSON_AddStringToObject(iframe_json, "width_unit", "percentage");
            } else {
                char *endptr;
                double w = strtod(width_str, &endptr);
                if (endptr != width_str) {
                    width = (int)w;
                    cJSON_AddStringToObject(iframe_json, "width_unit", "pixels");
                }
            }
        } else if (cJSON_IsNumber(width_attr)) {
            width = width_attr->valueint;
            cJSON_AddStringToObject(iframe_json, "width_unit", "pixels");
        }
    }
    
    // Parse height
    if (height_attr) {
        if (cJSON_IsString(height_attr)) {
            char *height_str = height_attr->valuestring;
            
            // Check for percentage
            if (strstr(height_str, "%")) {
                // For percentage, use default pixel value
                height = 300; // Default height for percentage-based iframes
                cJSON_AddStringToObject(iframe_json, "height_unit", "percentage");
            } else {
                char *endptr;
                double h = strtod(height_str, &endptr);
                if (endptr != height_str) {
                    height = (int)h;
                    cJSON_AddStringToObject(iframe_json, "height_unit", "pixels");
                }
            }
        } else if (cJSON_IsNumber(height_attr)) {
            height = height_attr->valueint;
            cJSON_AddStringToObject(iframe_json, "height_unit", "pixels");
        }
    }
    
    // Default dimensions if not specified
    if (width <= 0) width = 400;
    if (height <= 0) height = 300;
    
    // Store calculated dimensions
    if (!calc_width) {
        cJSON_AddNumberToObject(iframe_json, "calculated_width", width);
    } else {
        cJSON_SetNumberValue(calc_width, width);
    }
    
    if (!calc_height) {
        cJSON_AddNumberToObject(iframe_json, "calculated_height", height);
    } else {
        cJSON_SetNumberValue(calc_height, height);
    }
    
    // Add placeholder info (for when iframe content can't be loaded)
    if (!cJSON_GetObjectItem(iframe_json, "needs_placeholder")) {
        cJSON_AddBoolToObject(iframe_json, "needs_placeholder", true);
        
        // Determine iframe type for appropriate placeholder
        cJSON *iframe_type = cJSON_GetObjectItem(iframe_json, "iframe_type");
        if (iframe_type && cJSON_IsString(iframe_type)) {
            const char *type = iframe_type->valuestring;
            
            if (strcmp(type, "youtube_embed") == 0) {
                cJSON_AddStringToObject(iframe_json, "placeholder_type", "youtube_placeholder");
                cJSON_AddStringToObject(iframe_json, "placeholder_text", "YouTube Video");
            } else if (strcmp(type, "inline_html") == 0) {
                cJSON_AddStringToObject(iframe_json, "placeholder_type", "inline_content");
                cJSON_AddStringToObject(iframe_json, "placeholder_text", "Inline Content");
            } else if (strcmp(type, "external_content") == 0) {
                cJSON_AddStringToObject(iframe_json, "placeholder_type", "external_content");
                cJSON_AddStringToObject(iframe_json, "placeholder_text", "External Content");
            } else {
                cJSON_AddStringToObject(iframe_json, "placeholder_type", "iframe_placeholder");
                cJSON_AddStringToObject(iframe_json, "placeholder_text", "Frame");
            }
        } else {
            cJSON_AddStringToObject(iframe_json, "placeholder_type", "iframe_placeholder");
            cJSON_AddStringToObject(iframe_json, "placeholder_text", "Frame");
        }
        
        cJSON_AddNumberToObject(iframe_json, "placeholder_width", width);
        cJSON_AddNumberToObject(iframe_json, "placeholder_height", height);
    }
    
    // Default border for iframes (visual indication it's a frame)
    // Use cJSON_ReplaceItemInObject to ensure they're set
    cJSON *border_width = cJSON_GetObjectItem(iframe_json, "border_width");
    if (!border_width) {
        cJSON_AddNumberToObject(iframe_json, "border_width", 1);
    } else {
        cJSON_SetNumberValue(border_width, 1);
    }
    
    cJSON *border_style = cJSON_GetObjectItem(iframe_json, "border_style");
    if (!border_style) {
        cJSON_AddStringToObject(iframe_json, "border_style", "solid");
    } else {
        cJSON_SetValuestring(border_style, "solid");
    }
    
    cJSON *border_color = cJSON_GetObjectItem(iframe_json, "border_color");
    if (!border_color) {
        cJSON_AddStringToObject(iframe_json, "border_color", "#cccccc");
    } else {
        cJSON_SetValuestring(border_color, "#cccccc");
    }
    
    // Add scrollbar indication (iframes often need scrolling)
    cJSON *overflow = cJSON_GetObjectItem(iframe_json, "overflow");
    if (!overflow) {
        cJSON_AddStringToObject(iframe_json, "overflow", "auto");
    } else {
        cJSON_SetValuestring(overflow, "auto");
    }
    
    // Also set CSS border property for rendering
    char border_css[64];
    snprintf(border_css, sizeof(border_css), "1px solid #cccccc");
    cJSON_AddStringToObject(iframe_json, "border", border_css);
}