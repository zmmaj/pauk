#ifndef LAYOUT_ENGINE_H
#define LAYOUT_ENGINE_H

#include "cjson.h"
#include "gui.h"
#include "font_manager.h"

typedef struct {
    int x;              // Current X position
    int y_top;          // Topmost point on this line
    int y_bottom;       // Bottommost point on this line
    int has_baseline;   // Whether we have a baseline set
    int baseline_y;     // The baseline Y position (for text)
} LineState;

// ========== CONTEXT STRUCTURE ==========
typedef struct {
    int parent_x;
    int parent_y;
    int current_x;
    int current_y;
    int line_height;
    int container_width;
    int available_width;
    cJSON *parent_element;
    cJSON *previous_block;
    font_manager_t *font_manager;
    pauk_ui_t *pauk_ui; 

    int *col_widths;  
    int col_count;   
    
    int nav_parent_x;    // X position of containing nav
    int nav_parent_y;    // Y position of containing nav
    int in_nav;  
    int nav_padding_top;  

    int is_ul_in_nav;  
    int nav_y;         
    int nav_padding;

    cJSON *root_element;
    //menu
    const char *current_menu_orientation;

    LineState current_line;
    LineState next_line;
    int line_baseline; 

    // Float tracking
    int has_floated_left;
    int floated_left_x;
    int floated_left_width;
    int floated_y;
    int floated_height; 

    //sidebar
    int has_sidebar;
    int sidebar_x;
    int sidebar_width;
    int sidebar_y;
    int sidebar_height;
    //margine
    int previous_margin_bottom;

        // Polja za tabele (dodato)
        int **rowspan_grid;     // 2D matrica zauzetosti
        int rowspan_max_rows;
        int rowspan_max_cols;
        int *table_col_widths;   // Niz širina kolona
        int table_col_count;     // Broj kolona
        int current_row;  
        int row_heights[100];
} LayoutContext;

typedef enum {
    LAYOUT_BLOCK,
    LAYOUT_INLINE,
    LAYOUT_TEXT,
    LAYOUT_INLINE_BLOCK,
    LAYOUT_FLEX,
    LAYOUT_TABLE,
    LAYOUT_LIST_ITEM,
    LAYOUT_LINEBREAK
} LayoutType;

#define LAYOUT_DEBUG 0

// Document-level layout (starts layout from root)
extern void racunaj_pozicije(cJSON *element, int parent_x, int parent_y, int parent_width, LayoutContext *ctx);
extern void layout_document(cJSON *document, pauk_ui_t *pauk_ui, font_manager_t *font_mgr);
int get_container_width(cJSON *element, LayoutContext *ctx, pauk_ui_t *pauk_ui);

void layout_text_node(cJSON *element, LayoutContext *ctx);
void layout_inline_element(cJSON *element, LayoutContext *ctx);
void layout_inline_block_element(cJSON *element, LayoutContext *ctx);
void layout_block_element(cJSON *element, LayoutContext *ctx);

int get_json_number(cJSON *obj, const char *key, int default_value);
void set_json_number(cJSON *obj, const char *key, int val);
void set_json_bool(cJSON *obj, const char *key, int val);
int get_json_bool(cJSON *obj, const char *key, int default_value);
const char* get_json_string(cJSON *obj, const char *key, const char *default_value);
void set_json_string(cJSON *obj, const char *key, const char *val);
int estimate_text_width(const char *text, int font_size, 
    const char *font_weight, 
    const char *font_style);

void layout_list_item(cJSON *element, LayoutContext *ctx) ;
void layout_flex_element(cJSON *element, LayoutContext *ctx);
void layout_break_element(cJSON *element, LayoutContext *ctx);

int parse_css_length(const char *length_str, int reference_size);

//procesuiranje tabela: 
void layout_flex_container(cJSON *element, LayoutContext *ctx);


void layout_menu_element(cJSON *element, LayoutContext *ctx);
int has_ancestor_with_class(cJSON *elem, const char *target_class);
cJSON* find_element_by_id(cJSON *root, int target_id);
cJSON* find_element_by_string_id(cJSON *root, const char *target_id);
cJSON* find_parent_element_by_id(cJSON *root, int target_id);

int find_document_bottom(cJSON *element);
void update_scrollbar_ratio(pauk_ui_t *pauk_ui);
int find_document_right(cJSON *root, int viewport_width);

int is_sidebar_candidate(cJSON *element, LayoutContext *ctx);
void layout_sidebar_element(cJSON *element, LayoutContext *ctx) ;

void collect_text_recursive(cJSON *node, char **buffer);

void update_parent_dimensions(cJSON *root, cJSON *element);

void fix_all_heights(cJSON *root) ;
void update_element_height_from_children(cJSON *element);
void update_scrollbar_ratio_preserve(pauk_ui_t *pauk_ui);

#endif
