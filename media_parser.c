#include "media_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"

void calculate_media_dimensions(cJSON *media_json) {
    if (!media_json) return;

    // Check if already calculated
    cJSON *calc_width = cJSON_GetObjectItem(media_json, "calculated_width");
    cJSON *calc_height = cJSON_GetObjectItem(media_json, "calculated_height");
    
    if (calc_width && calc_height) {
        return; // Already calculated
    }
    
    int width = 0;
    int height = 0;
    
    cJSON *media_type = cJSON_GetObjectItem(media_json, "media_type");
    if (!media_type || !cJSON_IsString(media_type)) return;
    
    const char *type = media_type->valuestring;
    
    // Check attribute width/height
    cJSON *attr_width = cJSON_GetObjectItem(media_json, "attr_width");
    cJSON *attr_height = cJSON_GetObjectItem(media_json, "attr_height");
    
    // Parse width
    if (attr_width) {
        if (cJSON_IsString(attr_width)) {
            char *width_str = attr_width->valuestring;
            char *endptr;
            double w = strtod(width_str, &endptr);
            if (endptr != width_str) {
                width = (int)w;
            } else if (strstr(width_str, "%")) {
                // Percentage - use default pixel value
                if (strcmp(type, "video") == 0) width = 640;
                else if (strcmp(type, "audio") == 0) width = 300;
                else if (strcmp(type, "canvas") == 0) width = 300;
                cJSON_AddStringToObject(media_json, "width_unit", "percentage");
            }
        } else if (cJSON_IsNumber(attr_width)) {
            width = attr_width->valueint;
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
            } else if (strstr(height_str, "%")) {
                // Percentage - use default pixel value
                if (strcmp(type, "video") == 0) height = 360;
                else if (strcmp(type, "audio") == 0) height = 50;
                else if (strcmp(type, "canvas") == 0) height = 150;
                cJSON_AddStringToObject(media_json, "height_unit", "percentage");
            }
        } else if (cJSON_IsNumber(attr_height)) {
            height = attr_height->valueint;
        }
    }
    
    // Default dimensions based on media type
    if (width <= 0) {
        if (strcmp(type, "video") == 0) width = 640;
        else if (strcmp(type, "audio") == 0) width = 300;
        else if (strcmp(type, "canvas") == 0) width = 300;
    }
    
    if (height <= 0) {
        if (strcmp(type, "video") == 0) height = 360;
        else if (strcmp(type, "audio") == 0) height = 50;
        else if (strcmp(type, "canvas") == 0) height = 150;
    }
    
    // ========== FIXED: Store calculated dimensions CORRECTLY ==========
    if (!calc_width) {
        cJSON_AddNumberToObject(media_json, "calculated_width", width);
    } else {
        cJSON_SetNumberValue(calc_width, width);  // CORRECT: Use width
    }

    if (!calc_height) {
        cJSON_AddNumberToObject(media_json, "calculated_height", height);
    } else {
        cJSON_SetNumberValue(calc_height, height);  // CORRECT: Use height
    }
    
    // ========== ENHANCED: Calculate aspect ratio ==========
    if (height > 0) {
        double aspect_ratio = (double)width / (double)height;
        cJSON_AddNumberToObject(media_json, "aspect_ratio", aspect_ratio);
    }
    
    // ========== Add placeholder info (for when media can't be played) ==========
    cJSON_AddBoolToObject(media_json, "needs_placeholder", true);
    
    // For audio: no visual content, just controls
    if (strcmp(type, "audio") == 0) {
        cJSON_AddStringToObject(media_json, "placeholder_type", "audio_controls");
        cJSON_AddNumberToObject(media_json, "placeholder_width", width);
        cJSON_AddNumberToObject(media_json, "placeholder_height", height);
        cJSON_AddStringToObject(media_json, "display", "inline-block");
    }
    // For video: show poster or generic video placeholder
    else if (strcmp(type, "video") == 0) {
        cJSON *poster = cJSON_GetObjectItem(media_json, "poster");
        if (poster && cJSON_IsString(poster)) {
            cJSON_AddStringToObject(media_json, "placeholder_type", "poster_image");
            cJSON_AddStringToObject(media_json, "placeholder_src", poster->valuestring);
        } else {
            cJSON_AddStringToObject(media_json, "placeholder_type", "video_placeholder");
            cJSON_AddStringToObject(media_json, "placeholder_text", "Video");
        }
        cJSON_AddNumberToObject(media_json, "placeholder_width", width);
        cJSON_AddNumberToObject(media_json, "placeholder_height", height);
        cJSON_AddStringToObject(media_json, "display", "inline-block");
    }
    // For canvas: empty drawing surface
    else if (strcmp(type, "canvas") == 0) {
        cJSON_AddStringToObject(media_json, "placeholder_type", "canvas_surface");
        cJSON_AddStringToObject(media_json, "placeholder_text", "Canvas");
        cJSON_AddNumberToObject(media_json, "placeholder_width", width);
        cJSON_AddNumberToObject(media_json, "placeholder_height", height);
        cJSON_AddStringToObject(media_json, "display", "inline-block");
    }
    
    // ========== ADDED: Border for visual separation ==========
    if (!cJSON_GetObjectItem(media_json, "border")) {
        cJSON_AddStringToObject(media_json, "border", "1px solid #cccccc");
    }
    
    // ========== ADDED: Media controls styling ==========
    if (strcmp(type, "audio") == 0 || strcmp(type, "video") == 0) {
        if (!cJSON_GetObjectItem(media_json, "controls_color")) {
            cJSON_AddStringToObject(media_json, "controls_color", "#333333");
        }
        if (!cJSON_GetObjectItem(media_json, "controls_bg")) {
            cJSON_AddStringToObject(media_json, "controls_bg", "#f0f0f0");
        }
    }
}