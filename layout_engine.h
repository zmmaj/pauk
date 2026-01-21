#ifndef LAYOUT_ENGINE_H
#define LAYOUT_ENGINE_H

#include "cjson.h"
#include "main.h"
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>

typedef struct {
    double x;
    double y;
    double width;
    double height;
} LayoutBox;

typedef struct {
    double font_size;
    double line_height;
    const char *display;
    const char *position;
    double margin_top;
    double margin_right;
    double margin_bottom;
    double margin_left;
    double padding_top;
    double padding_right;
    double padding_bottom;
    double padding_left;
} ComputedStyle;

// Layout calculation functions
LayoutBox calculate_element_layout(lxb_dom_element_t *element, 
                                   double parent_x, double parent_y,
                                   double parent_width, double parent_height,
                                   ComputedStyle *parent_style);

ComputedStyle compute_element_style(lxb_dom_element_t *element, 
                                    ComputedStyle *parent_style);

// Main layout function
cJSON* calculate_document_layout(lxb_html_document_t *document);

// Helper to get CSS property value
const char* get_css_property(lxb_dom_element_t *element, 
                             const char *property_name,
                             const char *default_value);

#endif