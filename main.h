#ifndef MAIN_H
#define MAIN_H

#include "lexbor/html/html.h"
#include "lexbor/dom/dom.h"
#include "lexbor/css/css.h"

#include "cjson.h"
#include "cjson_utils.h"
#include "css_parser.h" 
#include "js_executor_quickjs.h"
#include "event_handler.h" 
#include <time.h>

#define LAYOUT_DEBUG 0
#define KOPIRAJ 1


typedef struct {
    void** blocks;
    size_t count;
    size_t capacity;
} MemoryPool;

void memory_pool_init(MemoryPool* pool);
void* memory_pool_alloc(MemoryPool* pool, size_t size);
void memory_pool_free_all(MemoryPool* pool);

typedef enum {
    ERR_SUCCESS = 0,
    ERR_LEXBOR_PARSE_FAILED,
    ERR_QUICKJS_INIT_FAILED,
    ERR_MEMORY_ALLOCATION,
    ERR_FILE_IO,
    ERR_INVALID_HTML
} BrowserErrorCode;

const char* browser_error_string(BrowserErrorCode code);

typedef struct {
    size_t max_memory_mb;
    int enable_javascript;
    int enable_css;
    int max_dom_depth;
    const char* user_agent;
} BrowserConfig;

BrowserConfig* browser_config_default(void);

typedef struct {
    lxb_dom_element_t* element;
    char* computed_html;
    size_t hash;
    time_t last_accessed;
} DOMCacheEntry;

void signal_handler(int sig);
// Function prototypes
void write_json_to_file(const char *filename, cJSON *json);
char* lexbor_to_cstr(const lxb_char_t *lb_str, size_t len);
char* get_element_text(lxb_dom_element_t *elem);
cJSON* element_to_json(lxb_dom_element_t *elem);
cJSON* traverse_node(lxb_dom_node_t *node);
int parse_html_file(const char *html_file, const char *json_file);
void run_lua_bridge(const char *json_file);
cJSON* process_element_simple(lxb_dom_element_t *elem, void *css_proc, cJSON *global_stylesheets);


cJSON* parse_inline_styles_with_lexbor(const lxb_char_t *css_text);


cJSON* build_dom_tree(lxb_dom_node_t *node, cJSON *parent_json);

lxb_html_document_t* parse_html_document(const char *html_file, cJSON **output_json);

//events
void process_all_events(lxb_html_document_t *doc);
void add_events_to_json(cJSON *root_json, lxb_html_document_t *doc);
cJSON* build_dom_with_events(lxb_dom_node_t *node);
cJSON* process_element_with_events(lxb_dom_element_t *elem);
cJSON* process_node_with_children(lxb_dom_node_t *node, int depth, cJSON *global_stylesheets);
cJSON* get_element_events_json(lxb_dom_element_t *elem);

cJSON* extract_classes_from_element(lxb_dom_element_t *elem);

//detektuj google fontove
void detect_google_fonts(lxb_dom_element_t *link_elem, cJSON *link_json);

void debug_find_style_brute_force(lxb_html_document_t *doc);
char* clean_css_text(const char *css);

void merge_layout_with_element(cJSON *elem_json, cJSON *layout_data);
cJSON* process_element_basic(lxb_dom_element_t *elem, void *css_proc, cJSON *global_stylesheets);
cJSON* get_global_computed_layout(void);
void set_global_computed_layout(cJSON *layout);
void clear_global_computed_layout(void);

cJSON* element_to_rendering_json(lxb_dom_element_t *elem, int is_inline);
char* get_element_text_simple(lxb_dom_element_t *elem);
cJSON* parse_inline_styles_simple(lxb_dom_attr_t *style_attr);
cJSON* process_node_for_rendering(lxb_dom_node_t *node, int depth);
int generate_rendering_output(const char *html_file, const char *output_file);
cJSON* process_element_for_rendering(lxb_dom_node_t *node, int depth);

// TABELE
void store_table_for_extraction(lxb_dom_element_t *table_elem, const char *filename);
cJSON* extract_table_structure(lxb_dom_element_t *table_elem);
cJSON* extract_table_rows_direct(lxb_dom_element_t *table_elem);
cJSON* extract_table_row(lxb_dom_element_t *row_elem, int row_index);
cJSON* extract_table_cell(lxb_dom_element_t *cell_elem, int row_index, int cell_index);
void calculate_table_dimensions(cJSON *table_json);

cJSON* extract_table_caption(lxb_dom_element_t *table_elem);
cJSON* extract_table_header(lxb_dom_element_t *table_elem);
cJSON* extract_table_body(lxb_dom_element_t *table_elem);
cJSON* extract_table_footer(lxb_dom_element_t *table_elem);
cJSON* extract_table_rows_direct(lxb_dom_element_t *table_elem);

cJSON* get_global_computed_layout(void);
void clear_global_computed_layout(void);
void set_global_computed_layout(cJSON *layout);

int kopiraj_fajl(const char *src_file);

#endif // MAIN_H