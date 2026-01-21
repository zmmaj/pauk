// css_parser.h
#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "cjson.h"
#include "js_executor_quickjs.h"
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/selectors.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>

#ifdef __cplusplus
extern "C" {
#endif

// CSS parser lifecycle
int css_parser_init(void);
void css_parser_cleanup(void);

// Property helpers
const char* get_css_property_name(lxb_css_property_type_t prop_id);
const lxb_css_entry_data_t* get_css_property_by_name(const char *name);

// CSS parsing functions
cJSON* parse_inline_styles(const char *style_str, void *css_proc);
cJSON* parse_css_styles_lxb(const lxb_char_t *css_text, size_t css_len);
cJSON* parse_stylesheet_lxb(const lxb_char_t *css_text, size_t css_len);

// DOM-specific CSS functions
cJSON* parse_element_styles(lxb_dom_element_t *element);
cJSON* parse_styles_from_attr(lxb_dom_attr_t *style_attr);

// Stylesheet extraction
void extract_and_parse_stylesheets(lxb_html_document_t *doc, cJSON *output_json);


lxb_css_parser_t* get_global_css_parser(void);
int parse_and_apply_document_styles(lxb_html_document_t *doc);

char* clean_css_text(const char *css);

cJSON* get_computed_styles_for_element(lxb_dom_element_t *element, 
    cJSON *all_stylesheets);
cJSON* collect_all_stylesheets(lxb_html_document_t *doc);

// CSS unit helpers
const lxb_css_data_t* get_css_unit_data(const char *unit_str);
const char* get_css_unit_type_name(uintptr_t unit_id);  
cJSON* parse_css_value_with_unit(const char *value_str);
cJSON* get_all_css_units_json(void);

// Enhanced parsing
cJSON* parse_css_property_enhanced(const char *prop_name, const char *value_str);


cJSON* parse_css_rule_complete(lxb_css_rule_t *rule);
cJSON* parse_declaration_complete(lxb_css_rule_declaration_t *decl);
void parse_style_rule_complete(lxb_css_rule_style_t *style_rule, cJSON *rule_json);
void parse_at_rule_complete(lxb_css_rule_at_t *at_rule, cJSON *rule_json);
void parse_media_rule_complete(lxb_css_at_rule_media_t *media_rule, cJSON *rule_json);



#ifdef __cplusplus
}
#endif

#endif // CSS_PARSER_H