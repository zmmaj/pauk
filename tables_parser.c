#include "tables_parser.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ==================== GLOBAL TABLE STORAGE ====================

// Complete struct definition (matches forward declaration in .h)
struct TableToExtract {
    lxb_dom_element_t *table_elem;
    char *filename;
    int table_index;
};

static TableToExtract *tables_to_extract = NULL;
static int table_count = 0;
static int tables_capacity = 0;

// ==================== HELPER FUNCTIONS ====================

static char* get_attribute_value(lxb_dom_element_t *elem, const char *attr_name) {
    if (!elem || !attr_name) return NULL;
    
    size_t len;
    const lxb_char_t *attr = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)attr_name, strlen(attr_name), &len);
    
    if (attr && len > 0) {
        char *value = malloc(len + 1);
        if (value) {
            memcpy(value, attr, len);
            value[len] = '\0';
            return value;
        }
    }
    return NULL;
}

static int has_boolean_attribute(lxb_dom_element_t *elem, const char *attr_name) {
    if (!elem || !attr_name) return 0;
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)attr_name, strlen(attr_name));
    return attr != NULL;
}

// ==================== PUBLIC GETTER FUNCTIONS ====================

int get_table_count(void) {
    return table_count;
}

const char* get_table_filename(int index) {
    if (index < 0 || index >= table_count) {
        return NULL;
    }
    return tables_to_extract[index].filename;
}

lxb_dom_element_t* get_table_element(int index) {
    if (index < 0 || index >= table_count) {
        return NULL;
    }
    return tables_to_extract[index].table_elem;
}

// ==================== TABLE STORAGE ====================

void store_table_for_extraction(lxb_dom_element_t *table_elem, const char *filename) {
    if (!table_elem || !filename) return;
    
    // Resize array if needed
    if (table_count >= tables_capacity) {
        int new_capacity = tables_capacity == 0 ? 4 : tables_capacity * 2;
        TableToExtract *new_array = realloc(tables_to_extract, 
                                          new_capacity * sizeof(TableToExtract));
        if (!new_array) return;
        
        tables_to_extract = new_array;
        tables_capacity = new_capacity;
    }
    
    // Store table
    tables_to_extract[table_count].table_elem = table_elem;
    tables_to_extract[table_count].filename = strdup(filename);
    tables_to_extract[table_count].table_index = table_count + 1;
    table_count++;
    
    printf("Stored table for extraction: %s (index: %d)\n", 
           filename, table_count);
}


// ==================== TABLE EXTRACTION FUNCTIONS ====================

cJSON* extract_table_structure(lxb_dom_element_t *table_elem) {
    if (!table_elem) {
        printf("ERROR: extract_table_structure called with NULL element\n");
        return NULL;
    }
    
    printf("Extracting table structure...\n");
    
    cJSON *table_json = cJSON_CreateObject();
    if (!table_json) {
        printf("ERROR: Failed to create JSON object for table\n");
        return NULL;
    }
    
    cJSON_AddStringToObject(table_json, "type", "table");
    
    // ========== GET TABLE ATTRIBUTES ==========
    
    // Get table ID
    size_t len;
    const lxb_char_t *id = lxb_dom_element_id(table_elem, &len);
    if (id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, id, len);
        id_str[len] = '\0';
        cJSON_AddStringToObject(table_json, "id", id_str);
        printf("  Table ID: %s\n", id_str);
        free(id_str);
    }
    
    // Get table class
    const lxb_char_t *cls = lxb_dom_element_class(table_elem, &len);
    if (cls && len > 0) {
        char *cls_str = malloc(len + 1);
        memcpy(cls_str, cls, len);
        cls_str[len] = '\0';
        cJSON_AddStringToObject(table_json, "class", cls_str);
        printf("  Table class: %s\n", cls_str);
        free(cls_str);
    }
    
    // Get other attributes
    const char *table_attrs[] = {
        "summary", "width", "height", "border", 
        "cellspacing", "cellpadding", "frame", "rules",
        "align", "bgcolor", NULL
    };
    
    for (int i = 0; table_attrs[i] != NULL; i++) {
        char *value = get_attribute_value(table_elem, table_attrs[i]);
        if (value) {
            cJSON_AddStringToObject(table_json, table_attrs[i], value);
            printf("  %s: %s\n", table_attrs[i], value);
            free(value);
        }
    }
    
    // Boolean attributes
    if (has_boolean_attribute(table_elem, "sortable")) {
        cJSON_AddBoolToObject(table_json, "sortable", true);
        printf("  sortable: true\n");
    }
    
    // ========== EXTRACT TABLE SECTIONS ==========
    
    printf("  Extracting table sections...\n");
    
    // Extract caption
    cJSON *caption_json = extract_table_caption(table_elem);
    if (caption_json) {
        cJSON_AddItemToObject(table_json, "caption", caption_json);
        printf("  Found caption\n");
    }
    
    // Extract header (thead)
    cJSON *thead_json = extract_table_header(table_elem);
    if (thead_json) {
        cJSON_AddItemToObject(table_json, "thead", thead_json);
        cJSON *thead_rows = cJSON_GetObjectItem(thead_json, "rows");
        if (thead_rows) {
            printf("  Found thead with %d rows\n", cJSON_GetArraySize(thead_rows));
        }
    }
    
    // Extract body (tbody)
    cJSON *tbody_json = extract_table_body(table_elem);
    if (tbody_json) {
        cJSON_AddItemToObject(table_json, "tbody", tbody_json);
        cJSON *tbody_rows = cJSON_GetObjectItem(tbody_json, "rows");
        if (tbody_rows) {
            printf("  Found tbody with %d rows\n", cJSON_GetArraySize(tbody_rows));
        }
    }
    
    // Extract footer (tfoot)
    cJSON *tfoot_json = extract_table_footer(table_elem);
    if (tfoot_json) {
        cJSON_AddItemToObject(table_json, "tfoot", tfoot_json);
        cJSON *tfoot_rows = cJSON_GetObjectItem(tfoot_json, "rows");
        if (tfoot_rows) {
            printf("  Found tfoot with %d rows\n", cJSON_GetArraySize(tfoot_rows));
        }
    }
    
    // If no structured sections found, extract rows directly
    if (!caption_json && !thead_json && !tbody_json && !tfoot_json) {
        printf("  No structured sections, extracting rows directly...\n");
        cJSON *rows_json = extract_table_rows_direct(table_elem);
        if (rows_json) {
            cJSON_AddItemToObject(table_json, "rows", rows_json);
            printf("  Found %d direct rows\n", cJSON_GetArraySize(rows_json));
        }
    }
    
    // ========== CALCULATE DIMENSIONS ==========
    
    printf("  Calculating table dimensions...\n");
    calculate_table_dimensions(table_json);
    
    cJSON *est_width = cJSON_GetObjectItem(table_json, "estimated_width");
    cJSON *est_height = cJSON_GetObjectItem(table_json, "estimated_height");
    if (est_width && est_height) {
        printf("  Estimated dimensions: %dx%d\n", 
               est_width->valueint, est_height->valueint);
    }
    
    printf("Table extraction complete.\n");
    return table_json;
}

// ==================== TABLE CAPTION ====================

cJSON* extract_table_caption(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    lxb_dom_node_t *table_node = lxb_dom_interface_node(table_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(table_node);
    
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
                
                if (strcasecmp(tag, "caption") == 0) {
                    cJSON *caption_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(caption_json, "type", "caption");
                    
                    // Get caption attributes
                    char *caption_id = get_attribute_value(child_elem, "id");
                    if (caption_id) {
                        cJSON_AddStringToObject(caption_json, "id", caption_id);
                        free(caption_id);
                    }
                    
                    char *caption_class = get_attribute_value(child_elem, "class");
                    if (caption_class) {
                        cJSON_AddStringToObject(caption_json, "class", caption_class);
                        free(caption_class);
                    }
                    
                    // Get caption text
                    char *caption_text = get_element_text_simple(child_elem);
                    if (caption_text && strlen(caption_text) > 0) {
                        // Clean text
                        char *start = caption_text;
                        while (*start && isspace(*start)) start++;
                        char *end = start + strlen(start) - 1;
                        while (end > start && isspace(*end)) *end-- = '\0';
                        
                        if (strlen(start) > 0) {
                            cJSON_AddStringToObject(caption_json, "text", start);
                        }
                    }
                    if (caption_text) free(caption_text);
                    
                    free(tag);
                    return caption_json;
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    return NULL;
}

// ==================== TABLE HEADER (THEAD) ====================

cJSON* extract_table_header(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    lxb_dom_node_t *table_node = lxb_dom_interface_node(table_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(table_node);
    
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
                
                if (strcasecmp(tag, "thead") == 0) {
                    cJSON *thead_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(thead_json, "type", "thead");
                    
                    // Get thead attributes
                    char *thead_id = get_attribute_value(child_elem, "id");
                    if (thead_id) {
                        cJSON_AddStringToObject(thead_json, "id", thead_id);
                        free(thead_id);
                    }
                    
                    char *thead_class = get_attribute_value(child_elem, "class");
                    if (thead_class) {
                        cJSON_AddStringToObject(thead_json, "class", thead_class);
                        free(thead_class);
                    }
                    
                    // Extract rows from thead
                    cJSON *rows_json = extract_table_rows_direct(child_elem);
                    if (rows_json) {
                        cJSON_AddItemToObject(thead_json, "rows", rows_json);
                        cJSON_AddNumberToObject(thead_json, "row_count", cJSON_GetArraySize(rows_json));
                    }
                    
                    free(tag);
                    return thead_json;
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    return NULL;
}

// ==================== TABLE BODY (TBODY) ====================

cJSON* extract_table_body(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    lxb_dom_node_t *table_node = lxb_dom_interface_node(table_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(table_node);
    
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
                
                if (strcasecmp(tag, "tbody") == 0) {
                    cJSON *tbody_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(tbody_json, "type", "tbody");
                    
                    // Get tbody attributes
                    char *tbody_id = get_attribute_value(child_elem, "id");
                    if (tbody_id) {
                        cJSON_AddStringToObject(tbody_json, "id", tbody_id);
                        free(tbody_id);
                    }
                    
                    char *tbody_class = get_attribute_value(child_elem, "class");
                    if (tbody_class) {
                        cJSON_AddStringToObject(tbody_json, "class", tbody_class);
                        free(tbody_class);
                    }
                    
                    // Extract rows from tbody
                    cJSON *rows_json = extract_table_rows_direct(child_elem);
                    if (rows_json) {
                        cJSON_AddItemToObject(tbody_json, "rows", rows_json);
                        cJSON_AddNumberToObject(tbody_json, "row_count", cJSON_GetArraySize(rows_json));
                    }
                    
                    free(tag);
                    return tbody_json;
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    return NULL;
}

// ==================== TABLE FOOTER (TFOOT) ====================

cJSON* extract_table_footer(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    lxb_dom_node_t *table_node = lxb_dom_interface_node(table_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(table_node);
    
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
                
                if (strcasecmp(tag, "tfoot") == 0) {
                    cJSON *tfoot_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(tfoot_json, "type", "tfoot");
                    
                    // Get tfoot attributes
                    char *tfoot_id = get_attribute_value(child_elem, "id");
                    if (tfoot_id) {
                        cJSON_AddStringToObject(tfoot_json, "id", tfoot_id);
                        free(tfoot_id);
                    }
                    
                    char *tfoot_class = get_attribute_value(child_elem, "class");
                    if (tfoot_class) {
                        cJSON_AddStringToObject(tfoot_json, "class", tfoot_class);
                        free(tfoot_class);
                    }
                    
                    // Extract rows from tfoot
                    cJSON *rows_json = extract_table_rows_direct(child_elem);
                    if (rows_json) {
                        cJSON_AddItemToObject(tfoot_json, "rows", rows_json);
                        cJSON_AddNumberToObject(tfoot_json, "row_count", cJSON_GetArraySize(rows_json));
                    }
                    
                    free(tag);
                    return tfoot_json;
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    return NULL;
}

// ==================== DIRECT ROWS EXTRACTION ====================

cJSON* extract_table_rows_direct(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    cJSON *rows_array = cJSON_CreateArray();
    if (!rows_array) return NULL;
    
    lxb_dom_node_t *table_node = lxb_dom_interface_node(table_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(table_node);
    int row_index = 0;
    
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
                
                if (strcasecmp(tag, "tr") == 0) {
                    cJSON *row_json = extract_table_row(child_elem, row_index++);
                    if (row_json) {
                        cJSON_AddItemToArray(rows_array, row_json);
                    }
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    if (cJSON_GetArraySize(rows_array) > 0) {
        return rows_array;
    } else {
        cJSON_Delete(rows_array);
        return NULL;
    }
}

// ==================== ROW EXTRACTION ====================

cJSON* extract_table_row(lxb_dom_element_t *row_elem, int row_index) {
    if (!row_elem) return NULL;
    
    cJSON *row_json = cJSON_CreateObject();
    if (!row_json) return NULL;
    
    cJSON_AddStringToObject(row_json, "type", "tr");
    cJSON_AddNumberToObject(row_json, "row_index", row_index);
    
    // Get row attributes
    char *row_id = get_attribute_value(row_elem, "id");
    if (row_id) {
        cJSON_AddStringToObject(row_json, "id", row_id);
        free(row_id);
    }
    
    char *row_class = get_attribute_value(row_elem, "class");
    if (row_class) {
        cJSON_AddStringToObject(row_json, "class", row_class);
        free(row_class);
    }
    
    // Get row bgcolor
    char *bgcolor = get_attribute_value(row_elem, "bgcolor");
    if (bgcolor) {
        cJSON_AddStringToObject(row_json, "bgcolor", bgcolor);
        free(bgcolor);
    }
    
    // Get row align
    char *align = get_attribute_value(row_elem, "align");
    if (align) {
        cJSON_AddStringToObject(row_json, "align", align);
        free(align);
    }
    
    // Get row valign
    char *valign = get_attribute_value(row_elem, "valign");
    if (valign) {
        cJSON_AddStringToObject(row_json, "valign", valign);
        free(valign);
    }
    
    // Extract cells
    cJSON *cells_array = cJSON_CreateArray();
    if (!cells_array) {
        cJSON_Delete(row_json);
        return NULL;
    }
    
    lxb_dom_node_t *row_node = lxb_dom_interface_node(row_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(row_node);
    int cell_index = 0;
    
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
                
                if (strcasecmp(tag, "th") == 0 || strcasecmp(tag, "td") == 0) {
                    cJSON *cell_json = extract_table_cell(child_elem, row_index, cell_index++);
                    if (cell_json) {
                        cJSON_AddItemToArray(cells_array, cell_json);
                    }
                }
                free(tag);
            }
        }
        child = lxb_dom_node_next(child);
    }
    
    cJSON_AddItemToObject(row_json, "cells", cells_array);
    cJSON_AddNumberToObject(row_json, "cell_count", cell_index);
    
    return row_json;
}

// ==================== CELL EXTRACTION ====================

cJSON* extract_table_cell(lxb_dom_element_t *cell_elem, int row_index, int cell_index) {
    if (!cell_elem) return NULL;
    
    cJSON *cell_json = cJSON_CreateObject();
    if (!cell_json) return NULL;
    
    // Determine cell type
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(cell_elem, &tag_len);
    char *tag = NULL;
    
    if (tag_name && tag_len > 0) {
        tag = malloc(tag_len + 1);
        if (tag) {
            memcpy(tag, tag_name, tag_len);
            tag[tag_len] = '\0';
            
            if (strcasecmp(tag, "th") == 0) {
                cJSON_AddStringToObject(cell_json, "type", "th");
                cJSON_AddBoolToObject(cell_json, "is_header", true);
            } else {
                cJSON_AddStringToObject(cell_json, "type", "td");
                cJSON_AddBoolToObject(cell_json, "is_header", false);
            }
            
            free(tag);
        }
    }
    
    cJSON_AddNumberToObject(cell_json, "row_index", row_index);
    cJSON_AddNumberToObject(cell_json, "cell_index", cell_index);
    
    // Get cell attributes
    char *cell_id = get_attribute_value(cell_elem, "id");
    if (cell_id) {
        cJSON_AddStringToObject(cell_json, "id", cell_id);
        free(cell_id);
    }
    
    char *cell_class = get_attribute_value(cell_elem, "class");
    if (cell_class) {
        cJSON_AddStringToObject(cell_json, "class", cell_class);
        free(cell_class);
    }
    
    // Colspan
    char *colspan = get_attribute_value(cell_elem, "colspan");
    if (colspan) {
        char *endptr;
        long span = strtol(colspan, &endptr, 10);
        if (endptr != colspan && span > 0) {
            cJSON_AddNumberToObject(cell_json, "colspan", span);
        }
        free(colspan);
    }
    
    // Rowspan
    char *rowspan = get_attribute_value(cell_elem, "rowspan");
    if (rowspan) {
        char *endptr;
        long span = strtol(rowspan, &endptr, 10);
        if (endptr != rowspan && span > 0) {
            cJSON_AddNumberToObject(cell_json, "rowspan", span);
        }
        free(rowspan);
    }
    
    // Other cell attributes
    char *width = get_attribute_value(cell_elem, "width");
    if (width) {
        cJSON_AddStringToObject(cell_json, "width", width);
        free(width);
    }
    
    char *height = get_attribute_value(cell_elem, "height");
    if (height) {
        cJSON_AddStringToObject(cell_json, "height", height);
        free(height);
    }
    
    char *bgcolor = get_attribute_value(cell_elem, "bgcolor");
    if (bgcolor) {
        cJSON_AddStringToObject(cell_json, "bgcolor", bgcolor);
        free(bgcolor);
    }
    
    char *align = get_attribute_value(cell_elem, "align");
    if (align) {
        cJSON_AddStringToObject(cell_json, "align", align);
        free(align);
    }
    
    char *valign = get_attribute_value(cell_elem, "valign");
    if (valign) {
        cJSON_AddStringToObject(cell_json, "valign", valign);
        free(valign);
    }
    
    // Get cell content
    char *cell_text = get_element_text_simple(cell_elem);
    if (cell_text && strlen(cell_text) > 0) {
        // Clean the text
        char *start = cell_text;
        while (*start && isspace(*start)) start++;
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        
        if (strlen(start) > 0) {
            cJSON_AddStringToObject(cell_json, "text", start);
            cJSON_AddNumberToObject(cell_json, "text_length", strlen(start));
        }
    }
    if (cell_text) free(cell_text);
    
    return cell_json;
}

// ==================== TABLE DIMENSIONS CALCULATION ====================

void calculate_table_dimensions(cJSON *table_json) {
    if (!table_json) return;
    
    int estimated_width = 600;  // Default table width
    int estimated_height = 200; // Default table height
    int total_rows = 0;
    int max_cells_in_row = 0;
    int total_rowspan_extra = 0; // Track extra height from rowspan
    
    // Helper function to count rows, cells, and account for rowspan/colspan
    void count_rows_and_cells(cJSON *rows_array) {
        if (!rows_array || !cJSON_IsArray(rows_array)) return;
        
        cJSON *row;
        cJSON_ArrayForEach(row, rows_array) {
            total_rows++;
            cJSON *cells = cJSON_GetObjectItem(row, "cells");
            if (cells && cJSON_IsArray(cells)) {
                int cell_count = cJSON_GetArraySize(cells);
                if (cell_count > max_cells_in_row) {
                    max_cells_in_row = cell_count;
                }
                
                // Check each cell for rowspan/colspan
                cJSON *cell;
                cJSON_ArrayForEach(cell, cells) {
                    // Account for rowspan in height calculation
                    cJSON *rowspan = cJSON_GetObjectItem(cell, "rowspan");
                    if (rowspan && cJSON_IsNumber(rowspan) && rowspan->valueint > 1) {
                        // Add extra height for rowspan (minus 1 for the base row already counted)
                        total_rowspan_extra += (rowspan->valueint - 1) * 40; // 40px per extra row
                    }
                    
                    // Note: colspan is already accounted for in max_cells_in_row
                    // since cells with colspan still count as single cells in HTML parsing
                }
            }
        }
    }
    
    // Check all table sections
    cJSON *thead = cJSON_GetObjectItem(table_json, "thead");
    if (thead) {
        cJSON *thead_rows = cJSON_GetObjectItem(thead, "rows");
        count_rows_and_cells(thead_rows);
    }
    
    cJSON *tbody = cJSON_GetObjectItem(table_json, "tbody");
    if (tbody) {
        cJSON *tbody_rows = cJSON_GetObjectItem(tbody, "rows");
        count_rows_and_cells(tbody_rows);
    }
    
    cJSON *tfoot = cJSON_GetObjectItem(table_json, "tfoot");
    if (tfoot) {
        cJSON *tfoot_rows = cJSON_GetObjectItem(tfoot, "rows");
        count_rows_and_cells(tfoot_rows);
    }
    
    cJSON *direct_rows = cJSON_GetObjectItem(table_json, "rows");
    if (direct_rows) {
        count_rows_and_cells(direct_rows);
    }
    
    // ========== CALCULATE FINAL DIMENSIONS ==========
    
    if (total_rows == 0) {
        // ===== FIX 1: EMPTY TABLE =====
        // Empty table - minimal dimensions
        estimated_width = 100;
        estimated_height = 50;
        printf("  Empty table - using minimal dimensions: %dx%d\n", 
               estimated_width, estimated_height);
    } else {
        // ===== NORMAL TABLE CALCULATION =====
        
        // Calculate width based on max cells per row
        if (max_cells_in_row > 0) {
            estimated_width = max_cells_in_row * 120; // ~120px per cell
            printf("  Width based on %d cells/row: %dpx\n", 
                   max_cells_in_row, estimated_width);
        }
        
        // Calculate base height based on rows
        if (total_rows > 0) {
            estimated_height = total_rows * 40; // ~40px per row
            printf("  Base height for %d rows: %dpx\n", 
                   total_rows, estimated_height);
        }
        
        // ===== FIX 2: ADD ROWSPAN EXTRA HEIGHT =====
        if (total_rowspan_extra > 0) {
            estimated_height += total_rowspan_extra;
            printf("  Added %dpx for rowspan\n", total_rowspan_extra);
        }
        
        // Add padding and borders
        estimated_width += 40;  // Left/right padding + borders
        estimated_height += 40; // Top/bottom padding + borders
        
        printf("  Final dimensions with padding: %dx%d\n", 
               estimated_width, estimated_height);
    }
    
    // ========== STORE CALCULATED DIMENSIONS ==========
    
    // Remove existing dimension fields if they exist (for replacement)
    cJSON *existing_width = cJSON_GetObjectItem(table_json, "estimated_width");
    cJSON *existing_height = cJSON_GetObjectItem(table_json, "estimated_height");
    
    if (existing_width) {
        cJSON_SetNumberValue(existing_width, estimated_width);
    } else {
        cJSON_AddNumberToObject(table_json, "estimated_width", estimated_width);
    }
    
    if (existing_height) {
        cJSON_SetNumberValue(existing_height, estimated_height);
    } else {
        cJSON_AddNumberToObject(table_json, "estimated_height", estimated_height);
    }
    
    // Update row/cell counts
    cJSON *existing_total_rows = cJSON_GetObjectItem(table_json, "total_rows");
    if (existing_total_rows) {
        cJSON_SetNumberValue(existing_total_rows, total_rows);
    } else {
        cJSON_AddNumberToObject(table_json, "total_rows", total_rows);
    }
    
    cJSON *existing_max_cells = cJSON_GetObjectItem(table_json, "max_cells_per_row");
    if (existing_max_cells) {
        cJSON_SetNumberValue(existing_max_cells, max_cells_in_row);
    } else {
        cJSON_AddNumberToObject(table_json, "max_cells_per_row", max_cells_in_row);
    }
    
    // Add rowspan info for debugging
    if (total_rowspan_extra > 0) {
        cJSON_AddNumberToObject(table_json, "rowspan_extra_height", total_rowspan_extra);
    }
}

// ==================== CLEANUP ====================

void cleanup_tables_storage(void) {
    printf("Cleaning up table storage (%d tables)\n", table_count);
    
    for (int i = 0; i < table_count; i++) {
        if (tables_to_extract[i].filename) {
            free(tables_to_extract[i].filename);
        }
    }
    
    free(tables_to_extract);
    tables_to_extract = NULL;
    table_count = 0;
    tables_capacity = 0;
}