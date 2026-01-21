

#include "headings_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static char* get_element_text_clean(lxb_dom_element_t *elem) {
    if (!elem) return strdup("");
    
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    size_t text_len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(node, &text_len);
    
    if (!text || text_len == 0) {
        if (text) {
            lxb_dom_document_t *owner_doc = node->owner_document;
            if (owner_doc) lxb_dom_document_destroy_text(owner_doc, text);
        }
        return strdup("");
    }
    
    char *result = malloc(text_len + 1);
    if (!result) {
        lxb_dom_document_t *owner_doc = node->owner_document;
        if (owner_doc) lxb_dom_document_destroy_text(owner_doc, text);
        return strdup("");
    }
    
    memcpy(result, text, text_len);
    result[text_len] = '\0';
    
    // Trim whitespace
    char *start = result;
    while (*start && isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        *end = '\0';
        end--;
    }
    
    // Create clean copy
    char *clean = strdup(start);
    free(result);
    
    lxb_dom_document_t *owner_doc = node->owner_document;
    if (owner_doc) lxb_dom_document_destroy_text(owner_doc, text);
    
    return clean ? clean : strdup("");
}

int get_heading_level(const char *tag_name) {
    if (!tag_name || strlen(tag_name) < 2) return 0;
    
    if (tag_name[0] == 'h' || tag_name[0] == 'H') {
        char level_char = tag_name[1];
        if (level_char >= '1' && level_char <= '6') {
            return level_char - '0';
        }
    }
    return 0;
}

const char* get_heading_level_name(int level) {
    switch (level) {
        case 1: return "h1";
        case 2: return "h2";
        case 3: return "h3";
        case 4: return "h4";
        case 5: return "h5";
        case 6: return "h6";
        default: return "unknown";
    }
}

int should_reset_heading_counter(const char *tag_name) {
    if (!tag_name) return 0;
    
    const char *reset_tags[] = {
        "article", "section", "aside", "nav", "main", NULL
    };
    
    for (int i = 0; reset_tags[i] != NULL; i++) {
        if (strcasecmp(tag_name, reset_tags[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void update_section_depth(const char *tag_name, DocumentOutline *outline, int is_closing) {
    if (!tag_name || !outline) return;
    
    const char *section_tags[] = {
        "section", "article", "aside", "nav", "main", NULL
    };
    
    for (int i = 0; section_tags[i] != NULL; i++) {
        if (strcasecmp(tag_name, section_tags[i]) == 0) {
            if (is_closing) {
                outline->current_section_depth--;
                if (outline->current_section_depth < 0) {
                    outline->current_section_depth = 0;
                }
            } else {
                outline->current_section_depth++;
            }
            break;
        }
    }
}

void init_document_outline(DocumentOutline *outline) {
    if (!outline) return;
    
    outline->headings = NULL;
    outline->count = 0;
    outline->capacity = 0;
    outline->current_section_depth = 0;
    outline->outline = cJSON_CreateArray();
}

void parse_heading_element(lxb_dom_element_t *elem, cJSON *elem_json, DocumentOutline *outline) {
    if (!elem || !elem_json || !outline) return;
    
    // Get tag name and level
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
    if (!tag_name || tag_len == 0) return;
    
    char *tag_str = malloc(tag_len + 1);
    memcpy(tag_str, tag_name, tag_len);
    tag_str[tag_len] = '\0';
    
    int level = get_heading_level(tag_str);
    if (level == 0) {
        free(tag_str);
        return; // Not a heading
    }
    
    // Store heading info
    if (outline->count >= outline->capacity) {
        outline->capacity = outline->capacity == 0 ? 8 : outline->capacity * 2;
        outline->headings = realloc(outline->headings, 
                                   outline->capacity * sizeof(HeadingInfo));
    }
    
    HeadingInfo *info = &outline->headings[outline->count];
    info->level = level;
    info->position = outline->count + 1;
    info->section_depth = outline->current_section_depth;
    info->element_json = cJSON_Duplicate(elem_json, 1);
    
    // Get heading text
    info->text = get_element_text_clean(elem);
    
    // Get ID
    size_t id_len;
    const lxb_char_t *id = lxb_dom_element_id(elem, &id_len);
    if (id && id_len > 0) {
        char *id_str = malloc(id_len + 1);
        memcpy(id_str, id, id_len);
        id_str[id_len] = '\0';
        info->id = id_str;
    } else {
        info->id = NULL;
    }
    
    // Get class
    const lxb_char_t *cls = lxb_dom_element_class(elem, &id_len);
    if (cls && id_len > 0) {
        char *class_str = malloc(id_len + 1);
        memcpy(class_str, cls, id_len);
        class_str[id_len] = '\0';
        info->class = class_str;
    } else {
        info->class = NULL;
    }
    
    // Update element JSON with heading-specific info
    cJSON_AddNumberToObject(elem_json, "heading_level", level);
    cJSON_AddNumberToObject(elem_json, "heading_position", info->position);
    cJSON_AddNumberToObject(elem_json, "section_depth", outline->current_section_depth);
    
    // Add to outline
    outline->count++;
    
    free(tag_str);
}

void build_document_outline(DocumentOutline *outline) {
    if (!outline || !outline->outline) return;
    
    // Clear existing outline
    cJSON *array = outline->outline;
    while (cJSON_GetArraySize(array) > 0) {
        cJSON_DeleteItemFromArray(array, 0);
    }
    
    // Build hierarchical outline
    for (int i = 0; i < outline->count; i++) {
        HeadingInfo *info = &outline->headings[i];
        
        cJSON *heading_node = cJSON_CreateObject();
        cJSON_AddStringToObject(heading_node, "text", info->text);
        cJSON_AddNumberToObject(heading_node, "level", info->level);
        cJSON_AddNumberToObject(heading_node, "position", info->position);
        cJSON_AddNumberToObject(heading_node, "section_depth", info->section_depth);
        
        if (info->id) {
            cJSON_AddStringToObject(heading_node, "id", info->id);
        }
        if (info->class) {
            cJSON_AddStringToObject(heading_node, "class", info->class);
        }
        
        // Add anchor link
        if (info->id) {
            char anchor[256];
            snprintf(anchor, sizeof(anchor), "#%s", info->id);
            cJSON_AddStringToObject(heading_node, "anchor", anchor);
        }
        
        cJSON_AddItemToArray(array, heading_node);
    }
}

cJSON* get_document_outline_json(DocumentOutline *outline) {
    if (!outline) return NULL;
    
    build_document_outline(outline);
    return cJSON_Duplicate(outline->outline, 1);
}

void free_document_outline(DocumentOutline *outline) {
    if (!outline) return;
    
    for (int i = 0; i < outline->count; i++) {
        HeadingInfo *info = &outline->headings[i];
        
        if (info->text) free((void*)info->text);
        if (info->id) free((void*)info->id);
        if (info->class) free((void*)info->class);
        if (info->element_json) cJSON_Delete(info->element_json);
    }
    
    if (outline->headings) free(outline->headings);
    if (outline->outline) cJSON_Delete(outline->outline);
    
    outline->headings = NULL;
    outline->count = 0;
    outline->capacity = 0;
    outline->current_section_depth = 0;
    outline->outline = NULL;
}