

// render_output.c - Creates final clean render file
#include "render_output.h"
#include "layout_calculator.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Clean an element for final rendering (remove parsing metadata)
cJSON* clean_element_for_render(cJSON *element) {
    if (!element) return NULL;
    
    cJSON *clean = cJSON_CreateObject();
    
    // Keep only rendering-relevant fields
    const char *keep_fields[] = {
        "tag", "type", "text",
        "x", "y", "calculated_width", "calculated_height",
        "bg_color", "color", "font_size", "font_family", "font_weight",
        "border_width", "border_color", "border_style", "border_radius",
        "src", "href", "alt", "title",
        "is_link", "is_image", "is_button", "is_clickable",
        "is_heading", "heading_level",
        "is_details", "details_open", "summary_text",
        "is_iframe", "iframe_src", "iframe_width", "iframe_height",
        "is_form", "form_file",
        "is_table", "table_file",
        "is_list", "list_file",
        "is_menu", "menu_file",
        "data_attributes", "aria_attributes",
        "children",
        NULL
    };
    
    for (int i = 0; keep_fields[i] != NULL; i++) {
        cJSON *item = cJSON_GetObjectItem(element, keep_fields[i]);
        if (item) {
            cJSON_AddItemToObject(clean, keep_fields[i], cJSON_Duplicate(item, 1));
        }
    }
    
    // Convert colors to hex
    cJSON *bg_color = cJSON_GetObjectItem(clean, "bg_color");
    if (bg_color && cJSON_IsString(bg_color)) {
        const char *hex = color_to_hex(bg_color->valuestring);
        cJSON_ReplaceItemInObject(clean, "bg_color", cJSON_CreateString(hex));
    }
    
    cJSON *color = cJSON_GetObjectItem(clean, "color");
    if (color && cJSON_IsString(color)) {
        const char *hex = color_to_hex(color->valuestring);
        cJSON_ReplaceItemInObject(clean, "color", cJSON_CreateString(hex));
    }
    
    // Process children recursively
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *clean_children = cJSON_CreateArray();
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            cJSON *clean_child = clean_element_for_render(child);
            if (clean_child) {
                cJSON_AddItemToArray(clean_children, clean_child);
            }
        }
        cJSON_AddItemToObject(clean, "children", clean_children);
    }
    
    return clean;
}

// Create final render file from all parsed files
cJSON* create_final_render_file(const char *main_output_file, 
                               const char **extracted_files, 
                               int file_count,
                               LayoutConfig config) {
    // 1. Load main output file
    FILE *f = fopen(main_output_file, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json_str = malloc(size + 1);
    fread(json_str, 1, size, f);
    json_str[size] = '\0';
    fclose(f);
    
    cJSON *main_json = cJSON_Parse(json_str);
    free(json_str);
    
    if (!main_json) return NULL;
    
    // 2. Load and merge extracted files (tables, forms, etc.)
    for (int i = 0; i < file_count; i++) {
        FILE *ef = fopen(extracted_files[i], "r");
        if (ef) {
            fseek(ef, 0, SEEK_END);
            long esize = ftell(ef);
            fseek(ef, 0, SEEK_SET);
            
            char *extracted_str = malloc(esize + 1);
            fread(extracted_str, 1, esize, ef);
            extracted_str[esize] = '\0';
            fclose(ef);
            
            cJSON *extracted_json = cJSON_Parse(extracted_str);
            free(extracted_str);
            
            if (extracted_json) {
                // Add reference to main JSON
                // You'll need to decide how to merge based on your structure
                cJSON_Delete(extracted_json);
            }
        }
    }
    
    // 3. Calculate layout
    cJSON *layout_json = izracunaj_document_layout(main_json, config);
    cJSON_Delete(main_json);
    
    if (!layout_json) return NULL;
    
    // 4. Clean for final render
    cJSON *final_render = NULL;
    
    if (cJSON_IsArray(layout_json)) {
        final_render = cJSON_CreateArray();
        cJSON *element;
        cJSON_ArrayForEach(element, layout_json) {
            cJSON *clean = clean_element_for_render(element);
            if (clean) {
                cJSON_AddItemToArray(final_render, clean);
            }
        }
    } else {
        final_render = clean_element_for_render(layout_json);
    }
    
    cJSON_Delete(layout_json);
    
    // 5. Add render metadata
    cJSON_AddNumberToObject(final_render, "render_version", 1);
    cJSON_AddNumberToObject(final_render, "render_timestamp", (double)time(NULL));
    cJSON_AddStringToObject(final_render, "render_engine", "html_parser_v1");
    
    return final_render;
}