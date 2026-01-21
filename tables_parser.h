#ifndef TABLES_PARSER_H
#define TABLES_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

// Forward declaration - NO typedef struct here
typedef struct TableToExtract TableToExtract;

// Table extraction functions
void store_table_for_extraction(lxb_dom_element_t *table_elem, const char *filename);
cJSON* extract_table_structure(lxb_dom_element_t *table_elem);
cJSON* extract_table_caption(lxb_dom_element_t *table_elem);
cJSON* extract_table_header(lxb_dom_element_t *table_elem);
cJSON* extract_table_body(lxb_dom_element_t *table_elem);
cJSON* extract_table_footer(lxb_dom_element_t *table_elem);
cJSON* extract_table_rows_direct(lxb_dom_element_t *table_elem);
cJSON* extract_table_row(lxb_dom_element_t *row_elem, int row_index);
cJSON* extract_table_cell(lxb_dom_element_t *cell_elem, int row_index, int cell_index);
void calculate_table_dimensions(cJSON *table_json);

// Getter functions 
int get_table_count(void);
const char* get_table_filename(int index);
lxb_dom_element_t* get_table_element(int index);
void cleanup_tables_storage(void);

#endif // TABLES_PARSER_H