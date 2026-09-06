#ifndef FORMS_PARSER_H
#define FORMS_PARSER_H

#include "cjson.h"
#include <lexbor/html/html.h>
#include "render_func.h"

// Initialization functions
void init_form_element(cJSON *form_json, lxb_dom_element_t *elem);
void init_input_element(cJSON *input_json, lxb_dom_element_t *elem);
void init_datalist_element(cJSON *datalist_json, lxb_dom_element_t *elem);
void init_output_element(cJSON *output_json, lxb_dom_element_t *elem);
void process_form_element(cJSON *elem_json, lxb_dom_element_t *elem, const char *tag);

// Rendering functions - MATCH these signatures exactly
void render_form_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font);
void render_input_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font);
void render_output_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font);
void render_textarea_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font);

#endif
