#include "img_parser.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

cJSON* parse_srcset(const char *srcset_str) {
    if (!srcset_str || strlen(srcset_str) == 0) {
        return NULL;
    }
    
    cJSON *srcset_array = cJSON_CreateArray();
    if (!srcset_array) {
        return NULL;
    }
    
    // Make a copy we can modify
    char *srcset_copy = strdup(srcset_str);
    if (!srcset_copy) {
        cJSON_Delete(srcset_array);
        return NULL;
    }
    
    char *saveptr = NULL;
    char *token = strtok_r(srcset_copy, ",", &saveptr);
    int item_count = 0;
    
    while (token) {
        // Trim whitespace
        char *start = token;
        while (*start && isspace(*start)) start++;
        
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) {
            *end = '\0';
            end--;
        }
        
        if (strlen(start) > 0) {
            cJSON *item = cJSON_CreateObject();
            if (!item) {
                // Skip this item if we can't create JSON
                token = strtok_r(NULL, ",", &saveptr);
                continue;
            }
            
            // Look for width descriptor (e.g., "image.jpg 800w")
            char *space = strrchr(start, ' ');
            if (space) {
                *space = '\0';
                char *url = start;
                char *descriptor = space + 1;
                
                cJSON_AddStringToObject(item, "url", url);
                
                // Check descriptor type
                if (strstr(descriptor, "w") || strstr(descriptor, "W")) {
                    // Width descriptor
                    char *endptr;
                    long width = strtol(descriptor, &endptr, 10);
                    if (endptr != descriptor) {
                        cJSON_AddNumberToObject(item, "width", width);
                        cJSON_AddStringToObject(item, "descriptor_type", "width");
                    }
                } else if (strstr(descriptor, "x") || strstr(descriptor, "X")) {
                    // Pixel density descriptor
                    char *endptr;
                    double density = strtod(descriptor, &endptr);
                    if (endptr != descriptor) {
                        cJSON_AddNumberToObject(item, "density", density);
                        cJSON_AddStringToObject(item, "descriptor_type", "density");
                    }
                }
            } else {
                // No descriptor, just URL
                cJSON_AddStringToObject(item, "url", start);
            }
            
            cJSON_AddItemToArray(srcset_array, item);
            item_count++;
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(srcset_copy);
    
    if (item_count > 0) {
        cJSON_AddNumberToObject(srcset_array, "item_count", item_count);
        return srcset_array;
    } else {
        cJSON_Delete(srcset_array);
        return NULL;
    }
}

void calculate_image_dimensions(cJSON *image_json) {
    if (!image_json) return;
    
    // Check if already calculated
    cJSON *calc_width = cJSON_GetObjectItem(image_json, "calculated_width");
    cJSON *calc_height = cJSON_GetObjectItem(image_json, "calculated_height");
    
    if (calc_width && calc_height) {
        return; // Already calculated
    }
    
    int width = 0;
    int height = 0;
    
    // Try to get width/height from attributes first
    cJSON *attr_width = cJSON_GetObjectItem(image_json, "attr_width");
    cJSON *attr_height = cJSON_GetObjectItem(image_json, "attr_height");
    
    // Also check CSS width/height if available
    cJSON *css_width = cJSON_GetObjectItem(image_json, "width");
    cJSON *css_height = cJSON_GetObjectItem(image_json, "height");
    
    // Parse width
    if (attr_width) {
        if (cJSON_IsString(attr_width)) {
            char *width_str = attr_width->valuestring;
            char *endptr;
            double w = strtod(width_str, &endptr);
            if (endptr != width_str) {
                width = (int)w;
                cJSON_AddStringToObject(image_json, "width_unit", "pixels");
            } else if (strstr(width_str, "%")) {
                // Percentage - use default pixel value
                width = 400;
                cJSON_AddStringToObject(image_json, "width_unit", "percentage");
            }
        } else if (cJSON_IsNumber(attr_width)) {
            width = attr_width->valueint;
            cJSON_AddStringToObject(image_json, "width_unit", "pixels");
        }
    } else if (css_width && cJSON_IsString(css_width)) {
        // Try CSS width
        char *width_str = css_width->valuestring;
        if (strstr(width_str, "px")) {
            char *endptr;
            double w = strtod(width_str, &endptr);
            if (endptr != width_str) {
                width = (int)w;
                cJSON_AddStringToObject(image_json, "width_unit", "pixels");
            }
        } else if (strstr(width_str, "%")) {
            // Percentage - use default
            width = 400;
            cJSON_AddStringToObject(image_json, "width_unit", "percentage");
        } else if (strstr(width_str, "auto")) {
            width = 400; // Default for auto
            cJSON_AddStringToObject(image_json, "width_unit", "auto");
        }
    }
    
    // Parse height
    if (attr_height) {
        if (cJSON_IsString(attr_height)) {
            char *height_str = attr_height->valuestring;
            char *endptr;
            double h = strtod(height_str, &endptr);
            if (endptr != height_str) {
                height = (int)h;
                cJSON_AddStringToObject(image_json, "height_unit", "pixels");
            } else if (strstr(height_str, "%")) {
                // Percentage - use default pixel value
                height = 300;
                cJSON_AddStringToObject(image_json, "height_unit", "percentage");
            }
        } else if (cJSON_IsNumber(attr_height)) {
            height = attr_height->valueint;
            cJSON_AddStringToObject(image_json, "height_unit", "pixels");
        }
    } else if (css_height && cJSON_IsString(css_height)) {
        // Try CSS height
        char *height_str = css_height->valuestring;
        if (strstr(height_str, "px")) {
            char *endptr;
            double h = strtod(height_str, &endptr);
            if (endptr != height_str) {
                height = (int)h;
                cJSON_AddStringToObject(image_json, "height_unit", "pixels");
            }
        } else if (strstr(height_str, "%")) {
            // Percentage - use default
            height = 300;
            cJSON_AddStringToObject(image_json, "height_unit", "percentage");
        } else if (strstr(height_str, "auto")) {
            height = 300; // Default for auto
            cJSON_AddStringToObject(image_json, "height_unit", "auto");
        }
    }
    
    // ========== FIXED: Default dimensions if not specified ==========
    if (width <= 0) width = 400;
    if (height <= 0) height = 300;
    
    // ========== FIXED: Store calculated dimensions CORRECTLY ==========
    if (!calc_width) {
        cJSON_AddNumberToObject(image_json, "calculated_width", width);
    } else {
        cJSON_SetNumberValue(calc_width, width);  // CORRECT: width
    }
    
    if (!calc_height) {
        cJSON_AddNumberToObject(image_json, "calculated_height", height);
    } else {
        cJSON_SetNumberValue(calc_height, height);  // CORRECT: height
    }
    
    // Calculate aspect ratio
    if (height > 0) {
        double aspect_ratio = (double)width / (double)height;
        cJSON_AddNumberToObject(image_json, "aspect_ratio", aspect_ratio);
    }
    
    // ========== Add image-specific rendering properties ==========
    
    // Placeholder for broken images
    if (!cJSON_GetObjectItem(image_json, "needs_placeholder")) {
        cJSON_AddBoolToObject(image_json, "needs_placeholder", true);
        
        cJSON *alt_text = cJSON_GetObjectItem(image_json, "alt");
        if (alt_text && cJSON_IsString(alt_text) && strlen(alt_text->valuestring) > 0) {
            cJSON_AddStringToObject(image_json, "placeholder_type", "alt_text");
            cJSON_AddStringToObject(image_json, "placeholder_text", alt_text->valuestring);
        } else {
            cJSON_AddStringToObject(image_json, "placeholder_type", "image_placeholder");
            cJSON_AddStringToObject(image_json, "placeholder_text", "Image");
        }
        
        cJSON_AddNumberToObject(image_json, "placeholder_width", width);
        cJSON_AddNumberToObject(image_json, "placeholder_height", height);
    }
    
    // Default image border (for img tags without styling)
    if (!cJSON_GetObjectItem(image_json, "border")) {
        cJSON_AddStringToObject(image_json, "border", "none");
    }
    
    // Image display mode
    if (!cJSON_GetObjectItem(image_json, "display")) {
        cJSON_AddStringToObject(image_json, "display", "inline-block");
    }
    
    // Object-fit style (for responsive images)
    if (!cJSON_GetObjectItem(image_json, "object_fit")) {
        cJSON_AddStringToObject(image_json, "object_fit", "fill");
    }
    
    // ========== Handle srcset if present ==========
    cJSON *srcset = cJSON_GetObjectItem(image_json, "srcset");
    if (srcset && cJSON_IsString(srcset)) {
        cJSON *parsed_srcset = parse_srcset(srcset->valuestring);
        if (parsed_srcset) {
            cJSON_AddItemToObject(image_json, "srcset_parsed", parsed_srcset);
            
            // Find the largest width in srcset for default selection
            cJSON *largest_item = NULL;
            int largest_width = 0;
            
            cJSON *item;
            cJSON_ArrayForEach(item, parsed_srcset) {
                if (cJSON_IsObject(item)) {
                    cJSON *item_width = cJSON_GetObjectItem(item, "width");
                    if (item_width && cJSON_IsNumber(item_width)) {
                        if (item_width->valueint > largest_width) {
                            largest_width = item_width->valueint;
                            largest_item = item;
                        }
                    }
                }
            }
            
            if (largest_item) {
                cJSON *largest_url = cJSON_GetObjectItem(largest_item, "url");
                if (largest_url && cJSON_IsString(largest_url)) {
                    cJSON_AddStringToObject(image_json, "srcset_default", largest_url->valuestring);
                }
            }
        }
    }
    
    // ========== Handle loading attribute ==========
    cJSON *loading = cJSON_GetObjectItem(image_json, "loading");
    if (!loading || !cJSON_IsString(loading)) {
        cJSON_AddStringToObject(image_json, "loading", "eager"); // HTML default
    }
    
    // ========== Handle decoding attribute ==========
    cJSON *decoding = cJSON_GetObjectItem(image_json, "decoding");
    if (!decoding || !cJSON_IsString(decoding)) {
        cJSON_AddStringToObject(image_json, "decoding", "auto"); // HTML default
    }
}