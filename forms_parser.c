// forms_parser.c - COMPLETE VERSION
#include "forms_parser.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Global form storage
typedef struct {
    lxb_dom_element_t *form_elem;
    char *filename;
    int form_index;
} FormToExtract;

static FormToExtract *forms_to_extract = NULL;
static int form_count = 0;
static int form_capacity = 0;

// ==================== HELPER FUNCTIONS ====================

// Check if attribute exists (boolean attributes)
static int has_boolean_attribute(lxb_dom_element_t *elem, const char *attr_name) {
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)attr_name, strlen(attr_name));
    return attr != NULL;
}

 void add_default_form_input_styles(cJSON *styles, cJSON *input_json) {
    if (!cJSON_GetObjectItem(styles, "border")) {
        cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "6px 8px");
    }
    if (!cJSON_GetObjectItem(styles, "border-radius")) {
        cJSON_AddStringToObject(styles, "border-radius", "4px");
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "font-family")) {
        cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#333333");
    }
    if (!cJSON_GetObjectItem(styles, "background-color")) {
        cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    }
    if (!cJSON_GetObjectItem(styles, "width")) {
        cJSON_AddStringToObject(styles, "width", "200px");
    }
    if (!cJSON_GetObjectItem(styles, "height")) {
        cJSON_AddStringToObject(styles, "height", "32px");
    }
    if (!cJSON_GetObjectItem(styles, "box-sizing")) {
        cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    }
}

 void add_default_textarea_styles(cJSON *styles) {
    if (!cJSON_GetObjectItem(styles, "border")) {
        cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "8px");
    }
    if (!cJSON_GetObjectItem(styles, "border-radius")) {
        cJSON_AddStringToObject(styles, "border-radius", "4px");
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "font-family")) {
        cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#333333");
    }
    if (!cJSON_GetObjectItem(styles, "background-color")) {
        cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    }
    if (!cJSON_GetObjectItem(styles, "width")) {
        cJSON_AddStringToObject(styles, "width", "300px");
    }
    if (!cJSON_GetObjectItem(styles, "height")) {
        cJSON_AddStringToObject(styles, "height", "100px");
    }
    if (!cJSON_GetObjectItem(styles, "resize")) {
        cJSON_AddStringToObject(styles, "resize", "vertical");
    }
}

 void add_default_select_styles(cJSON *styles) {
    if (!cJSON_GetObjectItem(styles, "border")) {
        cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "6px 8px");
    }
    if (!cJSON_GetObjectItem(styles, "border-radius")) {
        cJSON_AddStringToObject(styles, "border-radius", "4px");
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "font-family")) {
        cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#333333");
    }
    if (!cJSON_GetObjectItem(styles, "background-color")) {
        cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    }
    if (!cJSON_GetObjectItem(styles, "width")) {
        cJSON_AddStringToObject(styles, "width", "200px");
    }
    if (!cJSON_GetObjectItem(styles, "height")) {
        cJSON_AddStringToObject(styles, "height", "32px");
    }
    if (!cJSON_GetObjectItem(styles, "box-sizing")) {
        cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    }
    if (!cJSON_GetObjectItem(styles, "cursor")) {
        cJSON_AddStringToObject(styles, "cursor", "pointer");
    }
}

void add_default_button_styles(cJSON *styles, cJSON *button_json) {
    if (!cJSON_GetObjectItem(styles, "background-color")) {
        cJSON_AddStringToObject(styles, "background-color", "#007bff");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#ffffff");
    }
    if (!cJSON_GetObjectItem(styles, "border")) {
        cJSON_AddStringToObject(styles, "border", "1px solid #007bff");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "10px 20px");
    }
    if (!cJSON_GetObjectItem(styles, "border-radius")) {
        cJSON_AddStringToObject(styles, "border-radius", "4px");
    }
    if (!cJSON_GetObjectItem(styles, "font-size")) {
        cJSON_AddStringToObject(styles, "font-size", "14px");
    }
    if (!cJSON_GetObjectItem(styles, "font-family")) {
        cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    }
    if (!cJSON_GetObjectItem(styles, "font-weight")) {
        cJSON_AddStringToObject(styles, "font-weight", "bold");
    }
    if (!cJSON_GetObjectItem(styles, "cursor")) {
        cJSON_AddStringToObject(styles, "cursor", "pointer");
    }
    if (!cJSON_GetObjectItem(styles, "text-align")) {
        cJSON_AddStringToObject(styles, "text-align", "center");
    }
    if (!cJSON_GetObjectItem(styles, "width")) {
        cJSON *button_type = cJSON_GetObjectItem(button_json, "button_type");
        if (button_type && cJSON_IsString(button_type) && 
            strcasecmp(button_type->valuestring, "submit") == 0) {
            cJSON_AddStringToObject(styles, "width", "auto");
        } else {
            cJSON_AddStringToObject(styles, "width", "120px");
        }
    }
    if (!cJSON_GetObjectItem(styles, "height")) {
        cJSON_AddStringToObject(styles, "height", "36px");
    }
    if (!cJSON_GetObjectItem(styles, "box-sizing")) {
        cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    }
}

// Get attribute value as string
static char* get_attribute_value(lxb_dom_element_t *elem, const char *attr_name) {
    size_t len;
    const lxb_char_t *attr = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)attr_name, strlen(attr_name), &len);
    
    if (attr && len > 0) {
        char *value = malloc(len + 1);
        memcpy(value, attr, len);
        value[len] = '\0';
        return value;
    }
    return NULL;
}

// ==================== GETTER FUNCTIONS ====================

int get_form_count(void) {
    return form_count;
}

const char* get_form_filename(int index) {
    if (index < 0 || index >= form_count) {
        return NULL;
    }
    return forms_to_extract[index].filename;
}

lxb_dom_element_t* get_form_element(int index) {
    if (index < 0 || index >= form_count) {
        return NULL;
    }
    return forms_to_extract[index].form_elem;
}

// ==================== FORM STORAGE ====================

void store_form_for_extraction(lxb_dom_element_t *form_elem, const char *filename) {
    if (!form_elem || !filename) return;
    
    // Resize array if needed
    if (form_count >= form_capacity) {
        form_capacity = form_capacity == 0 ? 4 : form_capacity * 2;
        forms_to_extract = realloc(forms_to_extract, form_capacity * sizeof(*forms_to_extract));
    }
    
    // Store form
    forms_to_extract[form_count].form_elem = form_elem;
    forms_to_extract[form_count].filename = strdup(filename);
    forms_to_extract[form_count].form_index = form_count + 1;
    form_count++;
}

// ==================== FORM EXTRACTION ====================

cJSON* extract_form_structure(lxb_dom_element_t *form_elem) {
    if (!form_elem) return NULL;
    
    cJSON *form_json = cJSON_CreateObject();
    if (!form_json) return NULL;
    
    cJSON_AddStringToObject(form_json, "type", "form");
    
    // ===== GET ALL FORM ATTRIBUTES =====
    char *id = get_attribute_value(form_elem, "id");
    if (id) {
        cJSON_AddStringToObject(form_json, "id", id);
        free(id);
    }
    
    char *name = get_attribute_value(form_elem, "name");
    if (name) {
        cJSON_AddStringToObject(form_json, "name", name);
        free(name);
    }
    
    char *action = get_attribute_value(form_elem, "action");
    if (action) {
        cJSON_AddStringToObject(form_json, "action", action);
        free(action);
    } else {
        cJSON_AddStringToObject(form_json, "action", ""); // Default: current page
    }
    
    char *method = get_attribute_value(form_elem, "method");
    if (method) {
        cJSON_AddStringToObject(form_json, "method", method);
        free(method);
    } else {
        cJSON_AddStringToObject(form_json, "method", "GET"); // HTML default
    }
    
    char *target = get_attribute_value(form_elem, "target");
    if (target) {
        cJSON_AddStringToObject(form_json, "target", target);
        free(target);
    } else {
        cJSON_AddStringToObject(form_json, "target", "_self"); // HTML default
    }
    
    char *enctype = get_attribute_value(form_elem, "enctype");
    if (enctype) {
        cJSON_AddStringToObject(form_json, "enctype", enctype);
        free(enctype);
    } else {
        cJSON_AddStringToObject(form_json, "enctype", "application/x-www-form-urlencoded");
    }
    
    char *accept_charset = get_attribute_value(form_elem, "accept-charset");
    if (accept_charset) {
        cJSON_AddStringToObject(form_json, "accept_charset", accept_charset);
        free(accept_charset);
    }
    
    char *autocomplete = get_attribute_value(form_elem, "autocomplete");
    if (autocomplete) {
        cJSON_AddStringToObject(form_json, "autocomplete", autocomplete);
        free(autocomplete);
    }
    
    char *novalidate = get_attribute_value(form_elem, "novalidate");
    if (novalidate || has_boolean_attribute(form_elem, "novalidate")) {
        cJSON_AddBoolToObject(form_json, "novalidate", true);
    }
    free(novalidate);
    
    // ===== EXTRACT FORM ELEMENTS =====
    cJSON *elements = extract_form_elements(form_elem);
    if (elements) {
        cJSON_AddItemToObject(form_json, "elements", elements);
        cJSON_AddNumberToObject(form_json, "element_count", cJSON_GetArraySize(elements));
    } else {
        cJSON_AddItemToObject(form_json, "elements", cJSON_CreateArray());
        cJSON_AddNumberToObject(form_json, "element_count", 0);
    }
    
    // ===== CALCULATE FORM DIMENSIONS =====
    calculate_form_dimensions(form_json);
    
    return form_json;
}

// ==================== FORM ELEMENTS EXTRACTION ====================

cJSON* extract_form_elements(lxb_dom_element_t *form_elem) {
    if (!form_elem) return NULL;
    
    cJSON *elements_array = cJSON_CreateArray();
    if (!elements_array) return NULL;
    
    int element_index = 0;
    lxb_dom_node_t *form_node = lxb_dom_interface_node(form_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(form_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                if (!tag) {
                    child = lxb_dom_node_next(child);
                    continue;
                }
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                cJSON *element_json = NULL;
                
                if (strcasecmp(tag, "input") == 0) {
                    element_json = extract_input_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "textarea") == 0) {
                    element_json = extract_textarea_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "select") == 0) {
                    element_json = extract_select_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "button") == 0) {
                    element_json = extract_button_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "label") == 0) {
                    element_json = extract_label_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "fieldset") == 0) {
                    element_json = extract_fieldset_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "legend") == 0) {
                    element_json = extract_legend_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "datalist") == 0) {
                    element_json = extract_datalist_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "output") == 0) {
                    element_json = extract_output_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "optgroup") == 0) {
                    element_json = extract_optgroup_element(child_elem, element_index++);
                } else if (strcasecmp(tag, "br") == 0) {
                    // Skip <br> tags
                    free(tag);
                    child = lxb_dom_node_next(child);
                    continue;
                } else {
                    // Generic element in form
                    element_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(element_json, "type", "container");
                    cJSON_AddStringToObject(element_json, "tag", tag);
                    cJSON_AddNumberToObject(element_json, "index", element_index++);
                }
                
                if (element_json) {
                    cJSON_AddItemToArray(elements_array, element_json);
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    if (cJSON_GetArraySize(elements_array) > 0) {
        return elements_array;
    } else {
        cJSON_Delete(elements_array);
        return NULL;
    }
}

// ==================== DEFAULT STYLES ====================

// These functions remain the same as your original version
// but I'll include them for completeness...

 cJSON* create_default_form_input_styles(cJSON *input_json) {
    cJSON *styles = cJSON_CreateObject();
    cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    cJSON_AddStringToObject(styles, "padding", "6px 8px");
    cJSON_AddStringToObject(styles, "border-radius", "4px");
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    cJSON_AddStringToObject(styles, "color", "#333333");
    cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    cJSON_AddStringToObject(styles, "width", "200px");
    cJSON_AddStringToObject(styles, "height", "32px");
    cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    
    cJSON *input_type = cJSON_GetObjectItem(input_json, "input_type");
    if (input_type && cJSON_IsString(input_type)) {
        const char *type = input_type->valuestring;
        if (strcasecmp(type, "submit") == 0 || strcasecmp(type, "button") == 0) {
            cJSON_ReplaceItemInObject(styles, "background-color", cJSON_CreateString("#007bff"));
            cJSON_ReplaceItemInObject(styles, "color", cJSON_CreateString("#ffffff"));
            cJSON_ReplaceItemInObject(styles, "border", cJSON_CreateString("1px solid #007bff"));
            cJSON_AddStringToObject(styles, "cursor", "pointer");
            cJSON_AddStringToObject(styles, "font-weight", "bold");
        } else if (strcasecmp(type, "checkbox") == 0 || strcasecmp(type, "radio") == 0) {
            cJSON_ReplaceItemInObject(styles, "width", cJSON_CreateString("16px"));
            cJSON_ReplaceItemInObject(styles, "height", cJSON_CreateString("16px"));
        }
    }
    
    return styles;
}

 cJSON* create_default_textarea_styles() {
    cJSON *styles = cJSON_CreateObject();
    cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    cJSON_AddStringToObject(styles, "padding", "8px");
    cJSON_AddStringToObject(styles, "border-radius", "4px");
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    cJSON_AddStringToObject(styles, "color", "#333333");
    cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    cJSON_AddStringToObject(styles, "width", "300px");
    cJSON_AddStringToObject(styles, "height", "100px");
    cJSON_AddStringToObject(styles, "resize", "vertical");
    return styles;
}

 cJSON* create_default_select_styles() {
    cJSON *styles = cJSON_CreateObject();
    cJSON_AddStringToObject(styles, "border", "1px solid #ccc");
    cJSON_AddStringToObject(styles, "padding", "6px 8px");
    cJSON_AddStringToObject(styles, "border-radius", "4px");
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    cJSON_AddStringToObject(styles, "color", "#333333");
    cJSON_AddStringToObject(styles, "background-color", "#ffffff");
    cJSON_AddStringToObject(styles, "width", "200px");
    cJSON_AddStringToObject(styles, "height", "32px");
    cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    cJSON_AddStringToObject(styles, "cursor", "pointer");
    return styles;
}

cJSON* create_default_button_styles(cJSON *button_json) {
    cJSON *styles = cJSON_CreateObject();
    cJSON_AddStringToObject(styles, "background-color", "#007bff");
    cJSON_AddStringToObject(styles, "color", "#ffffff");
    cJSON_AddStringToObject(styles, "border", "1px solid #007bff");
    cJSON_AddStringToObject(styles, "padding", "10px 20px");
    cJSON_AddStringToObject(styles, "border-radius", "4px");
    cJSON_AddStringToObject(styles, "font-size", "14px");
    cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    cJSON_AddStringToObject(styles, "font-weight", "bold");
    cJSON_AddStringToObject(styles, "cursor", "pointer");
    cJSON_AddStringToObject(styles, "text-align", "center");
    
    cJSON *button_type = cJSON_GetObjectItem(button_json, "button_type");
    if (button_type && cJSON_IsString(button_type) && 
        strcasecmp(button_type->valuestring, "submit") == 0) {
        cJSON_AddStringToObject(styles, "width", "auto");
    } else {
        cJSON_AddStringToObject(styles, "width", "120px");
    }
    
    cJSON_AddStringToObject(styles, "height", "36px");
    cJSON_AddStringToObject(styles, "box-sizing", "border-box");
    
    return styles;
}

// ==================== INPUT ELEMENT EXTRACTION ====================

cJSON* extract_input_element(lxb_dom_element_t *input_elem, int element_index) {
    cJSON *input_json = cJSON_CreateObject();
    cJSON_AddStringToObject(input_json, "type", "input");
    cJSON_AddNumberToObject(input_json, "index", element_index);
    
    // Get input type
    size_t len;
    const lxb_char_t *input_type = lxb_dom_element_get_attribute(
        input_elem, (lxb_char_t*)"type", 4, &len);
    
    char *type_str = NULL;
    if (input_type && len > 0) {
        type_str = malloc(len + 1);
        if (type_str) {
            memcpy(type_str, input_type, len);
            type_str[len] = '\0';
            cJSON_AddStringToObject(input_json, "input_type", type_str);
        } else {
            cJSON_AddStringToObject(input_json, "input_type", "text");
        }
    } else {
        cJSON_AddStringToObject(input_json, "input_type", "text");
    }
    
    // Get common attributes
    const lxb_char_t *name = lxb_dom_element_get_attribute(
        input_elem, (lxb_char_t*)"name", 4, &len);
    if (name && len > 0) {
        char *name_str = malloc(len + 1);
        if (name_str) {
            memcpy(name_str, name, len);
            name_str[len] = '\0';
            cJSON_AddStringToObject(input_json, "name", name_str);
            free(name_str);
        }
    }
    
    const lxb_char_t *value = lxb_dom_element_get_attribute(
        input_elem, (lxb_char_t*)"value", 5, &len);
    if (value && len > 0) {
        char *value_str = malloc(len + 1);
        if (value_str) {
            memcpy(value_str, value, len);
            value_str[len] = '\0';
            cJSON_AddStringToObject(input_json, "value", value_str);
            free(value_str);
        }
    }
    
    const lxb_char_t *placeholder = lxb_dom_element_get_attribute(
        input_elem, (lxb_char_t*)"placeholder", 11, &len);
    if (placeholder && len > 0) {
        char *placeholder_str = malloc(len + 1);
        if (placeholder_str) {
            memcpy(placeholder_str, placeholder, len);
            placeholder_str[len] = '\0';
            cJSON_AddStringToObject(input_json, "placeholder", placeholder_str);
            free(placeholder_str);
        }
    }
    
    // Get ID and class
    const lxb_char_t *id = lxb_dom_element_id(input_elem, &len);
    if (id && len > 0) {
        char *id_str = malloc(len + 1);
        if (id_str) {
            memcpy(id_str, id, len);
            id_str[len] = '\0';
            cJSON_AddStringToObject(input_json, "id", id_str);
            free(id_str);
        }
    }
    
    // Get specific attributes based on input type
    if (type_str) {
        // ===== FIXED: Use lxb_dom_element_attr_by_name for boolean attributes =====
        // Check for boolean attributes CORRECTLY
        lxb_dom_attr_t *checked_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"checked", 7);
        if (checked_attr) {
            cJSON_AddBoolToObject(input_json, "checked", true);
        }
        
        lxb_dom_attr_t *required_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"required", 8);
        if (required_attr) {
            cJSON_AddBoolToObject(input_json, "required", true);
        }
        
        lxb_dom_attr_t *disabled_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"disabled", 8);
        if (disabled_attr) {
            cJSON_AddBoolToObject(input_json, "disabled", true);
        }
        
        lxb_dom_attr_t *readonly_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"readonly", 8);
        if (readonly_attr) {
            cJSON_AddBoolToObject(input_json, "readonly", true);
        }
        
        lxb_dom_attr_t *autofocus_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"autofocus", 9);
        if (autofocus_attr) {
            cJSON_AddBoolToObject(input_json, "autofocus", true);
        }
        
        lxb_dom_attr_t *multiple_attr = lxb_dom_element_attr_by_name(
            input_elem, (lxb_char_t*)"multiple", 8);
        if (multiple_attr) {
            cJSON_AddBoolToObject(input_json, "multiple", true);
        }
        // ===== END FIX =====
        
        // Type-specific attributes
        if (strcasecmp(type_str, "number") == 0 ||
            strcasecmp(type_str, "range") == 0) {
            
            const lxb_char_t *min = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"min", 3, &len);
            if (min && len > 0) {
                char *min_str = malloc(len + 1);
                if (min_str) {
                    memcpy(min_str, min, len);
                    min_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "min", min_str);
                    free(min_str);
                }
            }
            
            const lxb_char_t *max = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"max", 3, &len);
            if (max && len > 0) {
                char *max_str = malloc(len + 1);
                if (max_str) {
                    memcpy(max_str, max, len);
                    max_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "max", max_str);
                    free(max_str);
                }
            }
            
            const lxb_char_t *step = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"step", 4, &len);
            if (step && len > 0) {
                char *step_str = malloc(len + 1);
                if (step_str) {
                    memcpy(step_str, step, len);
                    step_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "step", step_str);
                    free(step_str);
                }
            }
        }
        
        if (strcasecmp(type_str, "password") == 0 ||
            strcasecmp(type_str, "text") == 0 ||
            strcasecmp(type_str, "email") == 0) {
            
            const lxb_char_t *maxlength = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"maxlength", 9, &len);
            if (maxlength && len > 0) {
                char *maxlength_str = malloc(len + 1);
                if (maxlength_str) {
                    memcpy(maxlength_str, maxlength, len);
                    maxlength_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "maxlength", maxlength_str);
                    free(maxlength_str);
                }
            }
            
            const lxb_char_t *minlength = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"minlength", 9, &len);
            if (minlength && len > 0) {
                char *minlength_str = malloc(len + 1);
                if (minlength_str) {
                    memcpy(minlength_str, minlength, len);
                    minlength_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "minlength", minlength_str);
                    free(minlength_str);
                }
            }
            
            const lxb_char_t *pattern = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"pattern", 7, &len);
            if (pattern && len > 0) {
                char *pattern_str = malloc(len + 1);
                if (pattern_str) {
                    memcpy(pattern_str, pattern, len);
                    pattern_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "pattern", pattern_str);
                    free(pattern_str);
                }
            }
        }
        
        if (strcasecmp(type_str, "file") == 0) {
            const lxb_char_t *accept = lxb_dom_element_get_attribute(
                input_elem, (lxb_char_t*)"accept", 6, &len);
            if (accept && len > 0) {
                char *accept_str = malloc(len + 1);
                if (accept_str) {
                    memcpy(accept_str, accept, len);
                    accept_str[len] = '\0';
                    cJSON_AddStringToObject(input_json, "accept", accept_str);
                    free(accept_str);
                }
            }
        }
        
        free(type_str);
    }
    
    // Add default styles (your existing code)
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(input_elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            add_default_form_input_styles(styles, input_json);
            cJSON_AddItemToObject(input_json, "style", styles);
        } else {
            cJSON *styles = create_default_form_input_styles(input_json);
            cJSON_AddItemToObject(input_json, "style", styles);
        }
    } else {
        cJSON *styles = create_default_form_input_styles(input_json);
        cJSON_AddItemToObject(input_json, "style", styles);
    }
    
    return input_json;
}

// ==================== TEXTAREA ELEMENT EXTRACTION ====================

cJSON* extract_textarea_element(lxb_dom_element_t *textarea_elem, int element_index) {
    cJSON *textarea_json = cJSON_CreateObject();
    if (!textarea_json) return NULL;
    
    cJSON_AddStringToObject(textarea_json, "type", "textarea");
    cJSON_AddNumberToObject(textarea_json, "index", element_index);
    
    // Get attributes
    const char *attrs[] = {"name", "id", "class", "placeholder", 
                          "rows", "cols", "wrap", "maxlength", 
                          "minlength", "dirname", NULL};
    
    for (int i = 0; attrs[i] != NULL; i++) {
        char *value = get_attribute_value(textarea_elem, attrs[i]);
        if (value) {
            // Parse rows/cols as numbers
            if (strcmp(attrs[i], "rows") == 0 || strcmp(attrs[i], "cols") == 0 ||
                strcmp(attrs[i], "maxlength") == 0 || strcmp(attrs[i], "minlength") == 0) {
                char *endptr;
                long num = strtol(value, &endptr, 10);
                if (endptr != value) {
                    cJSON_AddNumberToObject(textarea_json, attrs[i], num);
                } else {
                    cJSON_AddStringToObject(textarea_json, attrs[i], value);
                }
            } else {
                cJSON_AddStringToObject(textarea_json, attrs[i], value);
            }
            free(value);
        }
    }
    
    // Boolean attributes
    if (has_boolean_attribute(textarea_elem, "required")) {
        cJSON_AddBoolToObject(textarea_json, "required", true);
    }
    if (has_boolean_attribute(textarea_elem, "disabled")) {
        cJSON_AddBoolToObject(textarea_json, "disabled", true);
    }
    if (has_boolean_attribute(textarea_elem, "readonly")) {
        cJSON_AddBoolToObject(textarea_json, "readonly", true);
    }
    if (has_boolean_attribute(textarea_elem, "autofocus")) {
        cJSON_AddBoolToObject(textarea_json, "autofocus", true);
    }
    
    // Get textarea content
    char *text = get_element_text_simple(textarea_elem);
    if (text && strlen(text) > 0) {
        cJSON_AddStringToObject(textarea_json, "value", text);
    }
    if (text) free(text);
    
    // Add default styles
    cJSON *styles = create_default_textarea_styles();
    cJSON_AddItemToObject(textarea_json, "style", styles);
    
    return textarea_json;
}

// ==================== SELECT ELEMENT EXTRACTION ====================

cJSON* extract_select_element(lxb_dom_element_t *select_elem, int element_index) {
    cJSON *select_json = cJSON_CreateObject();
    if (!select_json) return NULL;
    
    cJSON_AddStringToObject(select_json, "type", "select");
    cJSON_AddNumberToObject(select_json, "index", element_index);
    
    // Get select attributes
    char *name = get_attribute_value(select_elem, "name");
    if (name) {
        cJSON_AddStringToObject(select_json, "name", name);
        free(name);
    }
    
    char *id = get_attribute_value(select_elem, "id");
    if (id) {
        cJSON_AddStringToObject(select_json, "id", id);
        free(id);
    }
    
    char *size_attr = get_attribute_value(select_elem, "size");
    if (size_attr) {
        char *endptr;
        long size = strtol(size_attr, &endptr, 10);
        if (endptr != size_attr) {
            cJSON_AddNumberToObject(select_json, "size", size);
        }
        free(size_attr);
    }
    
    // Boolean attributes
    if (has_boolean_attribute(select_elem, "required")) {
        cJSON_AddBoolToObject(select_json, "required", true);
    }
    if (has_boolean_attribute(select_elem, "disabled")) {
        cJSON_AddBoolToObject(select_json, "disabled", true);
    }
    if (has_boolean_attribute(select_elem, "multiple")) {
        cJSON_AddBoolToObject(select_json, "multiple", true);
    }
    if (has_boolean_attribute(select_elem, "autofocus")) {
        cJSON_AddBoolToObject(select_json, "autofocus", true);
    }
    
    // Extract options and optgroups
    cJSON *options_array = cJSON_CreateArray();
    int option_count = 0;
    
    lxb_dom_node_t *select_node = lxb_dom_interface_node(select_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(select_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "option") == 0) {
                    cJSON *option_json = cJSON_CreateObject();
                    
                    char *value = get_attribute_value(child_elem, "value");
                    if (value) {
                        cJSON_AddStringToObject(option_json, "value", value);
                        free(value);
                    }
                    
                    char *label = get_attribute_value(child_elem, "label");
                    if (label) {
                        cJSON_AddStringToObject(option_json, "label", label);
                        free(label);
                    }
                    
                    if (has_boolean_attribute(child_elem, "selected")) {
                        cJSON_AddBoolToObject(option_json, "selected", true);
                    }
                    if (has_boolean_attribute(child_elem, "disabled")) {
                        cJSON_AddBoolToObject(option_json, "disabled", true);
                    }
                    
                    // Option text
                    char *option_text = get_element_text_simple(child_elem);
                    if (option_text && strlen(option_text) > 0) {
                        char *start = option_text;
                        while (*start && isspace(*start)) start++;
                        char *end = start + strlen(start) - 1;
                        while (end > start && isspace(*end)) *end-- = '\0';
                        
                        if (strlen(start) > 0) {
                            cJSON_AddStringToObject(option_json, "text", start);
                        }
                    }
                    if (option_text) free(option_text);
                    
                    cJSON_AddItemToArray(options_array, option_json);
                    option_count++;
                } else if (strcasecmp(tag, "optgroup") == 0) {
                    cJSON *optgroup_json = extract_optgroup_element(child_elem, option_count);
                    if (optgroup_json) {
                        cJSON_AddItemToArray(options_array, optgroup_json);
                        cJSON *optgroup_options = cJSON_GetObjectItem(optgroup_json, "options");
                        if (optgroup_options) {
                            option_count += cJSON_GetArraySize(optgroup_options);
                        }
                    }
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    cJSON_AddItemToObject(select_json, "options", options_array);
    cJSON_AddNumberToObject(select_json, "option_count", option_count);
    
    // Add default styles
    cJSON *styles = create_default_select_styles();
    cJSON_AddItemToObject(select_json, "style", styles);
    
    return select_json;
}

// ==================== BUTTON ELEMENT EXTRACTION ====================

cJSON* extract_button_element(lxb_dom_element_t *button_elem, int element_index) {
    cJSON *button_json = cJSON_CreateObject();
    if (!button_json) return NULL;
    
    cJSON_AddStringToObject(button_json, "type", "button");
    cJSON_AddNumberToObject(button_json, "index", element_index);
    
    // Get button type (default to "submit")
    char *button_type = get_attribute_value(button_elem, "type");
    if (button_type) {
        cJSON_AddStringToObject(button_json, "button_type", button_type);
        free(button_type);
    } else {
        cJSON_AddStringToObject(button_json, "button_type", "submit");
    }
    
    // Get attributes
    char *name = get_attribute_value(button_elem, "name");
    if (name) {
        cJSON_AddStringToObject(button_json, "name", name);
        free(name);
    }
    
    char *value = get_attribute_value(button_elem, "value");
    if (value) {
        cJSON_AddStringToObject(button_json, "value", value);
        free(value);
    }
    
    char *id = get_attribute_value(button_elem, "id");
    if (id) {
        cJSON_AddStringToObject(button_json, "id", id);
        free(id);
    }
    
    // Boolean attributes
    if (has_boolean_attribute(button_elem, "disabled")) {
        cJSON_AddBoolToObject(button_json, "disabled", true);
    }
    if (has_boolean_attribute(button_elem, "autofocus")) {
        cJSON_AddBoolToObject(button_json, "autofocus", true);
    }
    
    // Form attributes (for formaction, formmethod, etc.)
    char *formaction = get_attribute_value(button_elem, "formaction");
    if (formaction) {
        cJSON_AddStringToObject(button_json, "formaction", formaction);
        free(formaction);
    }
    
    char *formmethod = get_attribute_value(button_elem, "formmethod");
    if (formmethod) {
        cJSON_AddStringToObject(button_json, "formmethod", formmethod);
        free(formmethod);
    }
    
    // Get button text
    char *text = get_element_text_simple(button_elem);
    if (text && strlen(text) > 0) {
        char *start = text;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(button_json, "text", start);
        }
    }
    if (text) free(text);
    
    // Add default styles
    cJSON *styles = create_default_button_styles(button_json);
    cJSON_AddItemToObject(button_json, "style", styles);
    
    return button_json;
}

// ==================== LABEL ELEMENT EXTRACTION ====================

cJSON* extract_label_element(lxb_dom_element_t *label_elem, int element_index) {
    cJSON *label_json = cJSON_CreateObject();
    if (!label_json) return NULL;
    
    cJSON_AddStringToObject(label_json, "type", "label");
    cJSON_AddNumberToObject(label_json, "index", element_index);
    
    // Get "for" attribute
    char *for_attr = get_attribute_value(label_elem, "for");
    if (for_attr) {
        cJSON_AddStringToObject(label_json, "for", for_attr);
        free(for_attr);
    }
    
    // Get label text
    char *label_text = get_element_text_simple(label_elem);
    if (label_text && strlen(label_text) > 0) {
        char *start = label_text;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(label_json, "text", start);
        }
    }
    if (label_text) free(label_text);
    
    return label_json;
}

// ==================== OTHER FORM ELEMENTS ====================

cJSON* extract_fieldset_element(lxb_dom_element_t *fieldset_elem, int element_index) {
    cJSON *fieldset_json = cJSON_CreateObject();
    if (!fieldset_json) return NULL;
    
    cJSON_AddStringToObject(fieldset_json, "type", "fieldset");
    cJSON_AddNumberToObject(fieldset_json, "index", element_index);
    
    // Get attributes
    char *id = get_attribute_value(fieldset_elem, "id");
    if (id) {
        cJSON_AddStringToObject(fieldset_json, "id", id);
        free(id);
    }
    
    char *name = get_attribute_value(fieldset_elem, "name");
    if (name) {
        cJSON_AddStringToObject(fieldset_json, "name", name);
        free(name);
    }
    
    if (has_boolean_attribute(fieldset_elem, "disabled")) {
        cJSON_AddBoolToObject(fieldset_json, "disabled", true);
    }
    
    // Fieldset can contain form elements
    cJSON *children = extract_form_elements(fieldset_elem);
    if (children) {
        cJSON_AddItemToObject(fieldset_json, "children", children);
    }
    
    return fieldset_json;
}

cJSON* extract_legend_element(lxb_dom_element_t *legend_elem, int element_index) {
    cJSON *legend_json = cJSON_CreateObject();
    if (!legend_json) return NULL;
    
    cJSON_AddStringToObject(legend_json, "type", "legend");
    cJSON_AddNumberToObject(legend_json, "index", element_index);
    
    // Get legend text
    char *legend_text = get_element_text_simple(legend_elem);
    if (legend_text && strlen(legend_text) > 0) {
        char *start = legend_text;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(legend_json, "text", start);
        }
    }
    if (legend_text) free(legend_text);
    
    return legend_json;
}

cJSON* extract_datalist_element(lxb_dom_element_t *datalist_elem, int element_index) {
    cJSON *datalist_json = cJSON_CreateObject();
    if (!datalist_json) return NULL;
    
    cJSON_AddStringToObject(datalist_json, "type", "datalist");
    cJSON_AddNumberToObject(datalist_json, "index", element_index);
    
    // Get ID (required for datalist)
    char *id = get_attribute_value(datalist_elem, "id");
    if (id) {
        cJSON_AddStringToObject(datalist_json, "id", id);
        free(id);
    }
    
    // Extract options
    cJSON *options_array = cJSON_CreateArray();
    
    lxb_dom_node_t *datalist_node = lxb_dom_interface_node(datalist_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(datalist_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            
            char *option_value = get_attribute_value(child_elem, "value");
            if (option_value) {
                cJSON *option_json = cJSON_CreateObject();
                cJSON_AddStringToObject(option_json, "value", option_value);
                free(option_value);
                
                // Option label
                char *label = get_attribute_value(child_elem, "label");
                if (label) {
                    cJSON_AddStringToObject(option_json, "label", label);
                    free(label);
                }
                
                cJSON_AddItemToArray(options_array, option_json);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    cJSON_AddItemToObject(datalist_json, "options", options_array);
    
    return datalist_json;
}

cJSON* extract_output_element(lxb_dom_element_t *output_elem, int element_index) {
    cJSON *output_json = cJSON_CreateObject();
    if (!output_json) return NULL;
    
    cJSON_AddStringToObject(output_json, "type", "output");
    cJSON_AddNumberToObject(output_json, "index", element_index);
    
    // Get attributes
    char *name = get_attribute_value(output_elem, "name");
    if (name) {
        cJSON_AddStringToObject(output_json, "name", name);
        free(name);
    }
    
    char *id = get_attribute_value(output_elem, "id");
    if (id) {
        cJSON_AddStringToObject(output_json, "id", id);
        free(id);
    }
    
    char *for_attr = get_attribute_value(output_elem, "for");
    if (for_attr) {
        // Can be space-separated list of element IDs
        cJSON *for_array = cJSON_CreateArray();
        char *token = strtok(for_attr, " ");
        while (token) {
            cJSON_AddItemToArray(for_array, cJSON_CreateString(token));
            token = strtok(NULL, " ");
        }
        cJSON_AddItemToObject(output_json, "for", for_array);
        free(for_attr);
    }
    
    // Get output text
    char *output_text = get_element_text_simple(output_elem);
    if (output_text && strlen(output_text) > 0) {
        char *start = output_text;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(output_json, "value", start);
        }
    }
    if (output_text) free(output_text);
    
    return output_json;
}

cJSON* extract_optgroup_element(lxb_dom_element_t *optgroup_elem, int element_index) {
    cJSON *optgroup_json = cJSON_CreateObject();
    if (!optgroup_json) return NULL;
    
    cJSON_AddStringToObject(optgroup_json, "type", "optgroup");
    cJSON_AddNumberToObject(optgroup_json, "index", element_index);
    
    // Get attributes
    char *label = get_attribute_value(optgroup_elem, "label");
    if (label) {
        cJSON_AddStringToObject(optgroup_json, "label", label);
        free(label);
    }
    
    if (has_boolean_attribute(optgroup_elem, "disabled")) {
        cJSON_AddBoolToObject(optgroup_json, "disabled", true);
    }
    
    // Extract options within optgroup
    cJSON *options_array = cJSON_CreateArray();
    
    lxb_dom_node_t *optgroup_node = lxb_dom_interface_node(optgroup_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(optgroup_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "option") == 0) {
                    cJSON *option_json = cJSON_CreateObject();
                    
                    char *value = get_attribute_value(child_elem, "value");
                    if (value) {
                        cJSON_AddStringToObject(option_json, "value", value);
                        free(value);
                    }
                    
                    if (has_boolean_attribute(child_elem, "selected")) {
                        cJSON_AddBoolToObject(option_json, "selected", true);
                    }
                    if (has_boolean_attribute(child_elem, "disabled")) {
                        cJSON_AddBoolToObject(option_json, "disabled", true);
                    }
                    
                    // Option text
                    char *option_text = get_element_text_simple(child_elem);
                    if (option_text && strlen(option_text) > 0) {
                        char *start = option_text;
                        while (*start && isspace(*start)) start++;
                        char *end = start + strlen(start) - 1;
                        while (end > start && isspace(*end)) *end-- = '\0';
                        
                        if (strlen(start) > 0) {
                            cJSON_AddStringToObject(option_json, "text", start);
                        }
                    }
                    if (option_text) free(option_text);
                    
                    cJSON_AddItemToArray(options_array, option_json);
                }
                
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    cJSON_AddItemToObject(optgroup_json, "options", options_array);
    
    return optgroup_json;
}

// ==================== FORM DIMENSIONS ====================

void calculate_form_dimensions(cJSON *form_json) {
    if (!form_json) return;
    
    int estimated_width = 400; // Default form width
    int estimated_height = 0;
    int element_spacing = 10; // Keep this
    
    cJSON *elements = cJSON_GetObjectItem(form_json, "elements");
    if (elements && cJSON_IsArray(elements)) {
        int element_count = cJSON_GetArraySize(elements);
        
        // ===== USE element_spacing IN CALCULATION =====
        // Base height: element_count * 30px + top/bottom padding
        estimated_height = element_count * 30 + 20;
        
        // Add spacing between elements (except last one)
        if (element_count > 1) {
            estimated_height += (element_count - 1) * element_spacing;
        }
        // ===== END USING element_spacing =====
        
        // Adjust based on element types
        cJSON *element;
        cJSON_ArrayForEach(element, elements) {
            cJSON *type = cJSON_GetObjectItem(element, "type");
            if (type && cJSON_IsString(type)) {
                if (strcmp(type->valuestring, "textarea") == 0) {
                    estimated_height += 70; // Textareas are taller
                } else if (strcmp(type->valuestring, "select") == 0) {
                    cJSON *option_count = cJSON_GetObjectItem(element, "option_count");
                    if (option_count && cJSON_IsNumber(option_count)) {
                        estimated_height += option_count->valueint * 5;
                    }
                } else if (strcmp(type->valuestring, "fieldset") == 0) {
                    // Fieldset has border and legend
                    estimated_height += 40;
                }
            }
        }
    }
    
    cJSON_AddNumberToObject(form_json, "estimated_width", estimated_width);
    cJSON_AddNumberToObject(form_json, "estimated_height", estimated_height);
    
    // ===== ALSO STORE THE SPACING VALUE FOR REFERENCE =====
    cJSON_AddNumberToObject(form_json, "element_spacing", element_spacing);
}

// ==================== CLEANUP ====================

void cleanup_forms_storage(void) {
    for (int i = 0; i < form_count; i++) {
        if (forms_to_extract[i].filename) {
            free(forms_to_extract[i].filename);
        }
    }
    free(forms_to_extract);
    forms_to_extract = NULL;
    form_count = form_capacity = 0;
}



cJSON* get_all_forms_json(void) {
    cJSON *forms_array = cJSON_CreateArray();
    
    for (int i = 0; i < form_count; i++) {
        cJSON *form_json = extract_form_structure(forms_to_extract[i].form_elem);
        if (form_json) {
            cJSON_AddNumberToObject(form_json, "form_index", i + 1);
            cJSON_AddStringToObject(form_json, "source_file", forms_to_extract[i].filename);
            cJSON_AddItemToArray(forms_array, form_json);
        }
    }
    
    return forms_array;
}

const char* get_form_element_type(lxb_dom_element_t *elem) {
    if (!elem) return "unknown";
    
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &len);
    if (!tag_name || len == 0) return "unknown";
    
    char *tag = malloc(len + 1);
    memcpy(tag, tag_name, len);
    tag[len] = '\0';
    
    const char *type = "unknown";
    if (strcasecmp(tag, "input") == 0) {
        char *input_type = get_attribute_value(elem, "type");
        if (input_type) {
            static char type_buffer[32];
            strncpy(type_buffer, input_type, sizeof(type_buffer)-1);
            type_buffer[sizeof(type_buffer)-1] = '\0';
            free(input_type);
            type = type_buffer;
        } else {
            type = "text";
        }
    } else if (strcasecmp(tag, "textarea") == 0) {
        type = "textarea";
    } else if (strcasecmp(tag, "select") == 0) {
        type = "select";
    } else if (strcasecmp(tag, "button") == 0) {
        type = "button";
    } else if (strcasecmp(tag, "label") == 0) {
        type = "label";
    } else if (strcasecmp(tag, "fieldset") == 0) {
        type = "fieldset";
    } else if (strcasecmp(tag, "legend") == 0) {
        type = "legend";
    }
    
    free(tag);
    return type;
}

cJSON* get_form_element_attributes(lxb_dom_element_t *elem) {
    cJSON *attrs = cJSON_CreateObject();
    if (!elem) return attrs;
    
    size_t len;
    
    // Get ID
    const lxb_char_t *id = lxb_dom_element_id(elem, &len);
    if (id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, id, len);
        id_str[len] = '\0';
        cJSON_AddStringToObject(attrs, "id", id_str);
        free(id_str);
    }
    
    // Get name
    const lxb_char_t *name = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"name", 4, &len);
    if (name && len > 0) {
        char *name_str = malloc(len + 1);
        memcpy(name_str, name, len);
        name_str[len] = '\0';
        cJSON_AddStringToObject(attrs, "name", name_str);
        free(name_str);
    }
    
    // Get value
    const lxb_char_t *value = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"value", 5, &len);
    if (value && len > 0) {
        char *value_str = malloc(len + 1);
        memcpy(value_str, value, len);
        value_str[len] = '\0';
        cJSON_AddStringToObject(attrs, "value", value_str);
        free(value_str);
    }
    
    return attrs;
}