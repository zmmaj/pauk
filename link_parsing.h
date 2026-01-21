// link_parsing.h - COMPLETE VERSION
#ifndef LINK_PARSING_H
#define LINK_PARSING_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

// ========== MAIN PUBLIC API ==========
cJSON* parse_link_element_complete(lxb_dom_element_t *link_elem, cJSON *base_json);

// ========== INDIVIDUAL ATTRIBUTE PARSERS ==========
void parse_link_target_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_rel_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_download_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_title_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_type_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_hreflang_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_ping_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_referrerpolicy_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_media_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_charset_attribute(lxb_dom_element_t *elem, cJSON *link_json);
void parse_link_name_attribute(lxb_dom_element_t *elem, cJSON *link_json);

// ========== LINK TYPE DETECTION ==========
void detect_link_type(cJSON *link_json);
const char* get_link_type_from_href(const char *href);
int is_mailto_link(const char *href);
int is_tel_link(const char *href);
int is_javascript_link(const char *href);
int is_anchor_link(const char *href);
int is_ftp_link(const char *href);
int is_external_url(const char *href);

// ========== SECURITY ANALYSIS ==========
void analyze_link_security(cJSON *link_json);
int is_potentially_unsafe_link(cJSON *link_json);
const char* get_security_recommendation(cJSON *link_json);

#endif // LINK_PARSING_H