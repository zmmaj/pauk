#include "img_parser.h"
#include "main.h"
#include "layout_engine.h"
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
    
    int width = 0;
    int height = 0;
    
    // Get pre-converted pixel values from attributes
    cJSON *attr_width = cJSON_GetObjectItem(image_json, "attr_width");
    cJSON *attr_height = cJSON_GetObjectItem(image_json, "attr_height");
    
    if (attr_width && cJSON_IsNumber(attr_width)) {
        width = attr_width->valueint;
    }
    
    if (attr_height && cJSON_IsNumber(attr_height)) {
        height = attr_height->valueint;
    }
    
    // Default dimensions if not specified
    if (width <= 0) width = 400;
    if (height <= 0) height = 300;
    
    // Set main width/height fields directly
    cJSON_ReplaceItemInObject(image_json, "width", cJSON_CreateNumber(width));
    cJSON_ReplaceItemInObject(image_json, "height", cJSON_CreateNumber(height));
    
    // Calculate aspect ratio (useful for maintaining proportions)
 /*   if (height > 0) {
        double aspect_ratio = (double)width / (double)height;
        cJSON_AddNumberToObject(image_json, "aspect_ratio", aspect_ratio);
    }
  */  
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
    
    // Default image border
    if (!cJSON_GetObjectItem(image_json, "border")) {
        cJSON_AddStringToObject(image_json, "border", "none");
    }
    
    // Object-fit style
    if (!cJSON_GetObjectItem(image_json, "object_fit")) {
        cJSON_AddStringToObject(image_json, "object_fit", "fill");
    }
    
    // ========== Handle srcset if present ==========
    cJSON *srcset = cJSON_GetObjectItem(image_json, "srcset");
    if (srcset && cJSON_IsString(srcset)) {
        cJSON *parsed_srcset = parse_srcset(srcset->valuestring);
        if (parsed_srcset) {
            cJSON_AddItemToObject(image_json, "srcset_parsed", parsed_srcset);
            
            // Find the largest width in srcset
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
        cJSON_AddStringToObject(image_json, "loading", "eager");
    }
    
    // ========== Handle decoding attribute ==========
    cJSON *decoding = cJSON_GetObjectItem(image_json, "decoding");
    if (!decoding || !cJSON_IsString(decoding)) {
        cJSON_AddStringToObject(image_json, "decoding", "auto");
    }
}

// Function to detect if buffer contains binary image data
int is_binary_image_data(const unsigned char *buffer, size_t size) {
    if (size < 8) return 0;
    
    // Check magic bytes for common image formats
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G') {
        printf("🔍 Detected PNG format\n");
        return 1;
    }
    
    // JPEG: FF D8 FF
    if (buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF) {
        printf("🔍 Detected JPEG format\n");
        return 1;
    }
    
    // GIF: GIF87a or GIF89a
    if ((buffer[0] == 'G' && buffer[1] == 'I' && buffer[2] == 'F') &&
        (buffer[3] == '8' && (buffer[4] == '7' || buffer[4] == '9') && buffer[5] == 'a')) {
        printf("🔍 Detected GIF format\n");
        return 1;
    }
    
    // WEBP: RIFF....WEBP
    if (buffer[0] == 'R' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == 'F' &&
        buffer[8] == 'W' && buffer[9] == 'E' && buffer[10] == 'B' && buffer[11] == 'P') {
        printf("🔍 Detected WEBP format\n");
        return 1;
    }
    
    // BMP: BM
    if (buffer[0] == 'B' && buffer[1] == 'M') {
        printf("🔍 Detected BMP format\n");
        return 1;
    }
    
    return 0;
}

// Function to check if buffer is pure binary (not text/HTML)
int is_binary_data(const unsigned char *buffer, size_t size) {
    if (size == 0) return 0;
    
    // Check for common binary magic bytes first
    if (is_binary_image_data(buffer, size)) return 1;
    
    // Heuristic: If first N bytes contain null bytes or high ASCII, it's binary
    int null_count = 0;
    int non_printable = 0;
    size_t check_size = size < 256 ? size : 256;
    
    for (size_t i = 0; i < check_size; i++) {
        if (buffer[i] == 0) null_count++;
        if (buffer[i] < 32 && buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] != '\t') {
            non_printable++;
        }
    }
    
    // If many null bytes or non-printable chars, it's likely binary
    if (null_count > 5 || non_printable > (int)check_size / 4) {
        printf("🔍 Detected binary data (%d nulls, %d non-printable)\n", 
               null_count, non_printable);
        return 1;
    }
    
    return 0;
}
