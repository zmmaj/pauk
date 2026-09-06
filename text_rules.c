#include "text_rules.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <lexbor/dom/dom.h>

/* ========== TAG CLASSIFICATION ========== */

/* Tags that should extract ONLY direct text (no child element text) */
static const char *direct_text_tags[] = {
    /* Block containers */
    "div", "section", "article", "main", "aside",
    "nav", "header", "footer", "body", "html",
    
    /* Text blocks */
    "p", "pre", "blockquote",
    
    /* Headings */
    "h1", "h2", "h3", "h4", "h5", "h6",
    
    /* List items */
    "li", "dt", "dd",
    
    /* Table cells */
    "td", "th", "caption",
    
    /* Other semantic blocks */
    "figcaption", "summary", "details",
    
    NULL
};

int is_inline(const char *tag)
{
    if (!tag || tag[0] == '\0')
        return 0;

    return (
        strcmp(tag, "a") == 0 ||   
        strcmp(tag, "abbr") == 0 ||    // Abbreviation OK
        strcmp(tag, "b") == 0 ||       // Bold  OK
        strcmp(tag, "bdi") == 0 ||     // Bidirectional OK
        strcmp(tag, "bdo") == 0 ||    //  Bidirectional OK
        strcmp(tag, "br") == 0 ||      //  Line break OK
        strcmp(tag, "cite") == 0 ||    // Citation ok
        strcmp(tag, "code") == 0 ||    // Code OK
        strcmp(tag, "data") == 0 ||   // Data OK
        strcmp(tag, "dfn") == 0 ||     // Definition --
        strcmp(tag, "em") == 0 ||     // Emphasis 
        strcmp(tag, "i") == 0 ||      // Italic OK
        strcmp(tag, "input") == 0 ||   // Input --
        strcmp(tag, "kbd") == 0 ||     // Keyboard OK
        strcmp(tag, "label") == 0 ||   // Label OK
        strcmp(tag, "mark") == 0 ||    // Mark OK
        strcmp(tag, "output") == 0 ||   // Output --
        strcmp(tag, "q") == 0 ||      // Quotation 
        strcmp(tag, "ruby") == 0 ||    // Ruby OK
        strcmp(tag, "rp") == 0 ||   // Ruby punctuation OK skip[ed]
        strcmp(tag, "rt") == 0 ||   // Ruby text OK skip[ed]
        strcmp(tag, "s") == 0 ||     // Strikethrough OK
        strcmp(tag, "samp") == 0 ||   // Sample OK
        strcmp(tag, "small") == 0 ||  // Small OK
        strcmp(tag, "span") == 0 ||    // Span OK
        strcmp(tag, "strong") == 0 ||   // Strong OK
        strcmp(tag, "sub") == 0 ||     // Subscript  ok
        strcmp(tag, "sup") == 0 ||    // Superscript  OK
        strcmp(tag, "time") == 0 ||   // Time OK
        strcmp(tag, "u") == 0 ||        // Underline OK
        strcmp(tag, "var") == 0 ||     // Variable OK
        strcmp(tag, "wbr") == 0 ||   // Word break OK

        /* obsolete but still seen */
        strcmp(tag, "big") == 0 ||  // Big OK
        strcmp(tag, "tt") == 0 ||    // Teletype OK
        strcmp(tag, "strike") == 0 ||
        strcmp(tag, "font") == 0 
    );
}

/* Tags that should extract ALL text (including nested elements) */
static const char *all_text_tags[] = {
    /* Inline text elements */
    "span", "a", "strong", "em", "i", "b", "u",
    "code", "mark", "small", "sub", "sup",
    "abbr", "cite", "label", "q", "s", "strike",
    "tt", "var", "bdi", "bdo",
    
    /* Form elements with text */
    "button", "textarea", "option",
    
    /* Other inline */
    "time", "data", "output", "progress", "meter",
    
    NULL
};

/* Tags that should NEVER have text extracted */
static const char *no_text_tags[] = {
    /* Structural */
    "head", "meta", "link", "style", "script",
    
    /* Media/void elements */
    "img", "br", "hr", "wbr", "input", "select",
    "iframe", "canvas", "audio", "video", "source",
    "track", "embed", "object", "param",
    
    /* Tables */
    "table", "thead", "tbody", "tfoot", "tr",
    "col", "colgroup",
    
    /* Lists */
    "ul", "ol", "dl",
    
    /* Forms */
    "form", "fieldset", "legend", "optgroup",
    
    /* Other */
    "menu", "dialog",
    
    NULL
};

/* ========== MODE DETERMINATION ========== */

TextExtractMode get_text_extract_mode(const char *tag) {
    if (!tag || tag[0] == '\0') return TEXT_EXTRACT_DIRECT_ONLY;
    
    /* Check no-text tags first */
    for (int i = 0; no_text_tags[i]; i++) {
        if (strcasecmp(tag, no_text_tags[i]) == 0) {
            return TEXT_EXTRACT_NONE;
        }
    }
    
    /* Check all-text tags */
    for (int i = 0; all_text_tags[i]; i++) {
        if (strcasecmp(tag, all_text_tags[i]) == 0) {
            return TEXT_EXTRACT_ALL_INCLUDING_CHILDREN;
        }
    }
    
    /* Check direct-text tags */
    for (int i = 0; direct_text_tags[i]; i++) {
        if (strcasecmp(tag, direct_text_tags[i]) == 0) {
            return TEXT_EXTRACT_DIRECT_ONLY;
        }
    }
    
    /* Default: direct only */
    return TEXT_EXTRACT_DIRECT_ONLY;
}

/* ========== TEXT EXTRACTION FUNCTIONS ========== */

char* extract_direct_text_only(lxb_dom_element_t *elem) {
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    char *result = strdup("");
    size_t total_len = 0;
    
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            /* Get text content using correct Lexbor function */
            size_t len;
            const lxb_char_t *content = lxb_dom_node_text_content(child, &len);
            
            if (content && len > 0) {
                /* Resize result buffer */
                char *new_result = realloc(result, total_len + len + 1);
                if (!new_result) {
                    free(result);
                    return strdup("");
                }
                result = new_result;
                
                /* Copy text */
                memcpy(result + total_len, content, len);
                total_len += len;
                result[total_len] = '\0';
            }
        }
        /* DO NOT process child elements */
        child = lxb_dom_node_next(child);
    }
    
    /* Trim result */
    if (total_len > 0) {
        /* Trim leading whitespace */
        char *start = result;
        while (*start && isspace((unsigned char)*start)) {
            start++;
            total_len--;
        }
        if (start != result) {
            memmove(result, start, total_len + 1);
        }
        
        /* Trim trailing whitespace */
        while (total_len > 0 && isspace((unsigned char)result[total_len-1])) {
            result[--total_len] = '\0';
        }
    }
    
    return result;
}

char* extract_all_text_including_children(lxb_dom_element_t *elem) {
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    char *result = strdup("");
    size_t total_len = 0;
    
    /* Stack-based traversal to avoid recursion */
    lxb_dom_node_t *stack[64];
    int stack_top = 0;
    stack[stack_top++] = node;
    
    while (stack_top > 0) {
        lxb_dom_node_t *current = stack[--stack_top];
        
        /* Push next sibling */
        lxb_dom_node_t *next = lxb_dom_node_next(current);
        if (next) stack[stack_top++] = next;
        
        /* Push first child */
        lxb_dom_node_t *first_child = lxb_dom_node_first_child(current);
        if (first_child) stack[stack_top++] = first_child;
        
        /* Process text nodes */
        if (current->type == LXB_DOM_NODE_TYPE_TEXT) {
            /* Get text content using correct Lexbor function */
            size_t len;
            const lxb_char_t *content = lxb_dom_node_text_content(current, &len);
            
            if (content && len > 0) {
                /* Check if we need to add a space before this text */
                if (total_len > 0 && !isspace((unsigned char)result[total_len-1])) {
                    char *new_result = realloc(result, total_len + len + 2);
                    if (!new_result) {
                        free(result);
                        return strdup("");
                    }
                    result = new_result;
                    result[total_len++] = ' ';
                }
                
                /* Add text */
                char *new_result = realloc(result, total_len + len + 1);
                if (!new_result) {
                    free(result);
                    return strdup("");
                }
                result = new_result;
                memcpy(result + total_len, content, len);
                total_len += len;
                result[total_len] = '\0';
            }
        }
    }
    
    return result;
}

char* extract_text_smart(lxb_dom_element_t *elem, const char *tag) {
    TextExtractMode mode = get_text_extract_mode(tag);
    
    switch (mode) {
        case TEXT_EXTRACT_DIRECT_ONLY:
            return extract_direct_text_only(elem);
            
        case TEXT_EXTRACT_ALL_INCLUDING_CHILDREN:
            return extract_all_text_including_children(elem);
            
        case TEXT_EXTRACT_NONE:
            return strdup("");
            
        default:
            return strdup("");
    }
}

int element_has_child_elements(lxb_dom_element_t *elem) {
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            return 1;
        }
        child = lxb_dom_node_next(child);
    }
    
    return 0;
}


int should_extract_text_for_element(const char *tag_name) {
    if (!tag_name) return 0;
    
    // Elements that should have their own text extracted
    const char *text_elements[] = {
        "a", "button", "textarea", "title", "label", "option",
        "abbr", "acronym", "b", "bdi", "bdo", "big", "cite",
        "code", "data", "dfn", "em", "i", "kbd", "mark",
        "meter", "output", "progress", "q", "rp", "rt",
        "ruby", "s", "samp", "small", "span", "strong",
        "sub", "sup", "time", "tt", "u", "var", "wbr",
        NULL
    };
    
    // Elements that should NOT have direct text (text comes from children)
    const char *container_elements[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6",
        "li", "ul", "ol", "td", "th", "tr", "table", "form",
        "article", "section", "header", "footer", "nav", "aside",
        "main", "figure", "figcaption", "blockquote", "pre",
        "legend", "caption", "summary", "details", "dialog",
        "fieldset", "dl", "dt", "dd", "body", "html",
        "address", "center", "dir", "menu", "menuitem", "nobr",
        "noembed", "noframes", "noscript", "plaintext", "rb",
        "rtc", "strike", "template", "xmp",
        NULL
    };
    
    // Check if it's a text element
    for (int i = 0; text_elements[i] != NULL; i++) {
        if (strcasecmp(tag_name, text_elements[i]) == 0) {
            return 1;
        }
    }
    
    // Check if it's a container element
    for (int i = 0; container_elements[i] != NULL; i++) {
        if (strcasecmp(tag_name, container_elements[i]) == 0) {
            return 0;  // Don't extract text for containers
        }
    }
    
    // Default: extract text
    return 1;
}


// Add these functions to text_rules.c (or a new layout_rules.c)

/* ========== TEXT MEASUREMENT FOR LAYOUT ========== */

// Calculate text width based on actual content and font size
int calculate_text_width_for_layout(const char *text, int font_size, const char *font_family) {
    if (!text || font_size <= 0) return 0;
    
    int width = 0;
    const char *p = text;
    
    // Character width multipliers (relative to font_size)
    while (*p) {
        unsigned char c = *p;
        
        if (c == 'i' || c == 'l' || c == 'I' || c == '1' || c == 't' || c == 'f' || c == 'r') {
            width += (int)(font_size * 0.3);  // Narrow characters
        } else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') {
            width += (int)(font_size * 0.9);  // Wide characters
        } else if (c == ' ') {
            width += (int)(font_size * 0.4);  // Spaces
        } else if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?') {
            width += (int)(font_size * 0.2);  // Punctuation
        } else {
            width += (int)(font_size * 0.6);  // Average characters
        }
        p++;
    }
    
    // Ensure minimum width
    if (strlen(text) > 0 && width < font_size) {
        width = font_size;
    }
    
    return width;
}

// Get default line height multiplier for element type
float get_line_height_for_element(const char *tag) {
    if (!tag) return 1.2f;
    
    if (strcmp(tag, "h1") == 0) return 1.1f;  // Tighter for headings
    if (strcmp(tag, "h2") == 0) return 1.15f;
    if (strcmp(tag, "h3") == 0) return 1.2f;
    if (strcmp(tag, "p") == 0) return 1.5f;   // Looser for paragraphs
    if (strcmp(tag, "li") == 0) return 1.4f;
    if (strcmp(tag, "pre") == 0) return 1.0f; // Monospace usually has less line height
    
    return 1.2f;  // Default
}
