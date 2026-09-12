#ifndef MAIN_H
#define MAIN_H

#include "lexbor/html/html.h"
#include "lexbor/dom/dom.h"
#include "lexbor/css/css.h"

#include "cjson.h"
#include "gui.h"
#include "cjson_utils.h"
#include "css_parser.h" 
#include "js_executor_quickjs.h"
#include "event_handler.h" 
#include <ui/ui.h>
#include <ui/pbutton.h>
#include <time.h>

#define LAYOUT_DEBUG 0
#define INFO_MESSAGES 1
#define INFO_MESSAGES_JS 1
#define KOPIRAJ 1

#define HAVE_MBEDTLS 1

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


// In your CSS parsing function, store rules like this:
typedef struct {
    char *selector;      // ".green-box" or "#div1"
    char *property;      // "background-color"
    char *value;         // "green"
} CSSRule;

extern CSSRule *css_rules;
extern int css_rule_count;
extern char *g_current_base_url;
extern int hover_timer_running;
extern int g_site_complexity_score;
extern int g_max_events_processed;
// Function prototypes
void write_json_to_file(const char *filename, cJSON *json);
char* lexbor_to_cstr(const lxb_char_t *lb_str, size_t len);
char* get_element_text(lxb_dom_element_t *elem);
cJSON* element_to_json(lxb_dom_element_t *elem);
cJSON* traverse_node(lxb_dom_node_t *node);
int parse_html_file(const char *html_file, const char *json_file);
//void run_lua_bridge(const char *json_file);
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

void merge_layout_with_element(cJSON *elem_json, cJSON *layout_data);
cJSON* process_element_basic(lxb_dom_element_t *elem, void *css_proc, cJSON *global_stylesheets);


cJSON* element_to_rendering_json(lxb_dom_element_t *elem, int is_inline);
char* get_element_text_simple(lxb_dom_element_t *elem);
char* get_element_text_recursive(lxb_dom_element_t *elem);
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



int kopiraj_fajl(const char *src_file);

void assign_element_ids(cJSON *element, int parent_id, int *id_counter);
cJSON* find_parent_json(cJSON* root, int parent_id); 
cJSON* build_hierarchy_with_ids(lxb_dom_node_t* root_node, cJSON* parent_container) ;
cJSON* build_document_hierarchy(lxb_html_document_t* document);

//IFRAME
void process_iframe_attributes(lxb_dom_element_t *elem, cJSON *elem_json);
void calculate_iframe_dimensions(cJSON *elem_json);

//ELEMENT INITIALIZER
char* get_element_tag(lxb_dom_element_t *elem);
cJSON* initialize_element(lxb_dom_element_t *elem, const char *tag, cJSON *parent_json, int depth);
int is_element_visible(lxb_dom_element_t *elem);
void process_child_elements(lxb_dom_element_t *parent_elem, cJSON *parent_json, 
                           int depth, int *element_counter);

void process_common_attributes(lxb_dom_element_t *elem, cJSON *elem_json, const char *tag);
cJSON* process_html_to_json(lxb_html_document_t *document, cJSON *all_stylesheets);
void extract_and_parse_css_styles(lxb_html_document_t *document, cJSON *output_json);
void apply_css_rule_to_element(cJSON *rule, cJSON *elem_json);
void add_css_rule(const char *selector, const char *property, const char *value);
void apply_all_css_rules_to_element(cJSON *element);
void apply_css_to_tree(cJSON *element);
cJSON* procesuiraj_elemente(lxb_dom_node_t *node, int depth, cJSON *parent_json);
 void dodaj_css( cJSON *output_json);
 void apply_css_to_element(cJSON *element);

 void add_attribute_to_children(cJSON *root, const char *parent_tag, 
    const char *attr_name, const char *attr_value, 
    int mode);
void mark_all_descendants(cJSON *element, const char *attr_name, 
        const char *attr_value, int mode);
        void load_and_render_page(const char *filename, const char *base_url) ;
void adjust_child_positions(cJSON *children, int y_offset);
void fix_media_link_alignment(cJSON *root);
void cleanup_previous_page(pauk_ui_t *pauk_ui);

const char* get_current_base_url(void);
void set_current_base_url(const char *url);

void apply_js_modifications_to_dom(cJSON *root, cJSON *modifications);
void refresh_page_after_js(pauk_ui_t *pauk_ui);
char* get_directory_path(const char *filepath);
#endif // MAIN_H
