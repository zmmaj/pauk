
#ifndef FORMS_PARSER_H
#define FORMS_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

// Form extraction
cJSON* extract_form_structure(lxb_dom_element_t *form_elem);
cJSON* extract_form_elements(lxb_dom_element_t *form_elem);
cJSON* extract_input_element(lxb_dom_element_t *input_elem, int element_index);
cJSON* extract_textarea_element(lxb_dom_element_t *textarea_elem, int element_index);
cJSON* extract_select_element(lxb_dom_element_t *select_elem, int element_index);
cJSON* extract_button_element(lxb_dom_element_t *button_elem, int element_index);

// Form helpers
const char* get_form_element_type(lxb_dom_element_t *elem);
cJSON* get_form_element_attributes(lxb_dom_element_t *elem);
void calculate_form_dimensions(cJSON *form_json);

// Global form storage (similar to tables)
void store_form_for_extraction(lxb_dom_element_t *form_elem, const char *filename);
cJSON* get_all_forms_json(void);
void cleanup_forms_storage(void);

//===  GETTER FUNCTIONS =====
int get_form_count(void);
const char* get_form_filename(int index);
lxb_dom_element_t* get_form_element(int index);



cJSON* extract_label_element(lxb_dom_element_t *label_elem, int element_index);
cJSON* extract_fieldset_element(lxb_dom_element_t *fieldset_elem, int element_index);
cJSON* extract_legend_element(lxb_dom_element_t *legend_elem, int element_index);
cJSON* extract_datalist_element(lxb_dom_element_t *datalist_elem, int element_index);

// Style creation functions
cJSON* create_default_form_input_styles(cJSON *input_json);
cJSON* create_default_textarea_styles(void);
cJSON* create_default_select_styles(void);
cJSON* create_default_button_styles(cJSON *button_json);

//element extractors
cJSON* extract_output_element(lxb_dom_element_t *output_elem, int element_index);
cJSON* extract_optgroup_element(lxb_dom_element_t *optgroup_elem, int element_index);

void add_default_form_input_styles(cJSON *styles, cJSON *input_json);
void add_default_textarea_styles(cJSON *styles);
void add_default_select_styles(cJSON *styles);
void add_default_button_styles(cJSON *styles, cJSON *button_json);

#endif // FORMS_PARSER_H