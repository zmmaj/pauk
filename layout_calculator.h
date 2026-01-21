

// layout_calculator.h
#ifndef LAYOUT_CALCULATOR_H
#define LAYOUT_CALCULATOR_H

#include "cjson.h"

// Layout configuration
typedef struct {
    int viewport_width;
    int viewport_height;
    int default_font_size;
    int line_height_multiplier; // e.g., 120% = 1.2
    int margin_between_elements;
    int padding_inside_elements;
    int scrollbar_width;
} LayoutConfig;

// Element positioning
typedef struct {
    int x;
    int y;
    int width;
    int height;
    int layer; // z-index equivalent
    int is_visible;
    int clip_x;
    int clip_y;
    int clip_width;
    int clip_height;
} ElementLayout;

// Main functions
LayoutConfig default_layout_config(void);
cJSON* izracunaj_document_layout(cJSON *document_json, LayoutConfig config);
cJSON* process_element_layout(cJSON *element, LayoutConfig *config, 
                              int parent_x, int parent_y, 
                              int parent_width, int parent_height,
                              int *current_y);
void apply_css_box_model(cJSON *element);
int parse_css_length(const char *length_str, int reference_size);
const char* color_to_hex(const char *color_str);
int font_size_to_pixels(const char *size_str, int parent_font_size);

#endif // LAYOUT_CALCULATOR_H