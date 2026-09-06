#ifndef TEXT_RULES_H
#define TEXT_RULES_H

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

/* Text extraction modes */
typedef enum {
    TEXT_EXTRACT_DIRECT_ONLY,     // Only direct text children
    TEXT_EXTRACT_ALL_INCLUDING_CHILDREN,  // All text including nested elements
    TEXT_EXTRACT_NONE             // No text extraction
} TextExtractMode;

int is_inline(const char *tag);

/* Get extraction mode for a tag */
TextExtractMode get_text_extract_mode(const char *tag);

/* Smart text extraction based on tag */
char* extract_text_smart(lxb_dom_element_t *elem, const char *tag);

/* Direct text only (no child element text) */
char* extract_direct_text_only(lxb_dom_element_t *elem);

/* All text including nested elements */
char* extract_all_text_including_children(lxb_dom_element_t *elem);

/* Check if element has child elements */
int element_has_child_elements(lxb_dom_element_t *elem);

int should_extract_text_for_element(const char *tag_name);


float get_line_height_for_element(const char *tag);
int calculate_text_width_for_layout(const char *text, int font_size, const char *font_family);

#endif /* TEXT_RULES_H */
