#ifndef TABLES_PARSER_H
#define TABLES_PARSER_H

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include "cjson.h"
#include "main.h"         
#include "render_func.h"   
#include "layout_engine.h" 

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// STRUKTURE
// ============================================================================

// Matrica za praćenje zauzetih ćelija (colspan/rowspan)
typedef struct {
    int rows;
    int cols;
    int **grid;        // 2D matrica: 0 = slobodno, 1 = zauzeto
    cJSON ***cells;    // 2D matrica: pokazivač na cJSON ćeliju
} table_layout_t;

// Struktura za praćenje ćelija koje se prostiru preko više kolona (colspan)
typedef struct {
    int start_col;     // Početna kolona
    int colspan;       // Broj kolona koje zauzima
    int total_width;   // Ukupna širina (sabrane širine svih kolona)
} SpanCell;
// ============================================================================
// GLAVNE FUNKCIJE ZA PARSIRANJE
// ============================================================================

// Parsira kompletnu tabelu i vraća cJSON sa matricom i redovima
cJSON* parse_table_complete(lxb_dom_element_t *table_elem);

// Parsira jedan red (<tr>) i vraća cJSON
cJSON* parse_table_row(lxb_dom_element_t *tr_elem);

// Parsira jednu ćeliju (<td> ili <th>) i vraća cJSON
cJSON* parse_table_cell(lxb_dom_element_t *td_elem);

// ============================================================================
// FUNKCIJE ZA MATRICU
// ============================================================================

// Prikuplja sve redove iz tabele (1. prolaz)
void collect_table_rows(lxb_dom_element_t *table_elem, cJSON *rows_array);

// Izgrađuje matricu na osnovu prikupljenih redova (2. prolaz)
cJSON* build_table_matrix(cJSON *rows_raw);

// ============================================================================
// POMOĆNE FUNKCIJE
// ============================================================================

// Čita integer atribut iz HTML elementa
int get_attr_int(lxb_dom_element_t *elem, const char *attr_name, int default_value);

// Čita string atribut iz HTML elementa (vraća novi string, mora se osloboditi)
char* get_attr_str(lxb_dom_element_t *elem, const char *attr_name);

// ============================================================================
// FUNKCIJE ZA LAYOUT (pozicioniranje tabela)
// ============================================================================

// Izračunava širine kolona na osnovu sadržaja
int* calculate_column_widths(cJSON *table_json, int max_width);

// Pozicionira sve ćelije u tabeli
void layout_table(cJSON *table_json, int x, int y, int max_width);

// ============================================================================
// FUNKCIJE ZA RENDER (crtanje tabela)
// ============================================================================

// Renderuje tabelu na pixelmap
void render_table(pauk_ui_t *pauk_ui, cJSON *table_json, int x, int y, html_font_t *font);

// Renderuje jedan red
void render_table_row(pauk_ui_t *pauk_ui, cJSON *row_json, int x, int y, html_font_t *font);

// Renderuje jednu ćeliju
void render_table_cell(pauk_ui_t *pauk_ui, cJSON *cell_json, int x, int y, int width, int height, html_font_t *font);

// ============================================================================
// ČIŠĆENJE
// ============================================================================


void layout_table_element(cJSON *element, LayoutContext *ctx);
// Oslobađa matricu (grid i cells)
void cleanup_table_layout(LayoutContext *ctx);

int calculate_cell_content_width(cJSON *cell, int max_width);
int* calculate_table_column_widths(cJSON *table_json, int max_width, int *out_col_count);

void layout_table_row(cJSON *row_json, LayoutContext *ctx);
void layout_table_cells_natural(cJSON *table_element, int table_x, int table_y, LayoutContext *ctx);
void calculate_table_borders_from_cells(cJSON *table_element);
void layout_table_section(cJSON *element, LayoutContext *ctx);
int layout_table_cell(cJSON *element, int x, int y, int cell_width, LayoutContext *ctx);
void render_table_from_data(pauk_ui_t* pauk_ui, cJSON* table_data, 
    int table_x, int table_y, 
    int offset_x, int offset_y, int scroll_y);

#ifdef __cplusplus
}
#endif

#endif // TABLES_PARSER_H
