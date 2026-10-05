#include "tables_parser.h"
#include "cjson.h"          // ← za cJSON tipove
#include "main.h"           // ← za pauk_ui_t
#include "render_func.h"    // ← za html_font_t
#include "layout_engine.h"
#include <lexbor/html/html.h>   // ← za lxb_dom_element_t
#include <lexbor/dom/dom.h>     // ← za lxb_dom_node_t
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ===== 1. POMOĆNA FUNKCIJA ZA ČITANJE ATRIBUTA =====
int get_attr_int(lxb_dom_element_t *elem, const char *attr_name, int default_value) {
    size_t len;
    const lxb_char_t *value = lxb_dom_element_get_attribute(elem, (lxb_char_t*)attr_name, 
                                                            strlen(attr_name), &len);
    if (value && len > 0) {
        char buf[32];
        if (len < sizeof(buf)) {
            memcpy(buf, value, len);
            buf[len] = '\0';
            return atoi(buf);
        }
    }
    return default_value;
}

// ===== 1.1 POMOĆNA FUNKCIJA ZA ČITANJE STRING ATRIBUTA =====
char* get_attr_str(lxb_dom_element_t *elem, const char *attr_name) {
    if (!elem || !attr_name) return NULL;
    size_t len;
    const lxb_char_t *value = lxb_dom_element_get_attribute(elem, (lxb_char_t*)attr_name, 
                                                            strlen(attr_name), &len);
    if (value && len > 0) {
        char *str = malloc(len + 1);
        if (str) {
            memcpy(str, value, len);
            str[len] = '\0';
            return str;
        }
    }
    return NULL;
}

// ===== 2. PRIKUPLJANJE REDOVA I ĆELIJA (1. PROLAZ) =====
void collect_table_rows(lxb_dom_element_t *table_elem, cJSON *rows_array) {
    lxb_dom_node_t *node = lxb_dom_node_first_child(lxb_dom_interface_node(table_elem));
    
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(node);
            size_t tag_len;
            const lxb_char_t *tag = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag && tag_len > 0) {
                char tag_name[32];
                size_t copy_len = tag_len < 31 ? tag_len : 31;
                memcpy(tag_name, tag, copy_len);
                tag_name[copy_len] = '\0';
                
                if (strcasecmp(tag_name, "thead") == 0 ||
                    strcasecmp(tag_name, "tbody") == 0 ||
                    strcasecmp(tag_name, "tfoot") == 0) {
                    // Rekurzivno obradi grupu
                    collect_table_rows(child_elem, rows_array);
                } else if (strcasecmp(tag_name, "tr") == 0) {
                    cJSON *row_json = parse_table_row(child_elem);
                    if (row_json) {
                        cJSON_AddItemToArray(rows_array, row_json);
                    }
                }
            }
        }
        node = lxb_dom_node_next(node);
    }
}

// ===== 3. PARSIRANJE JEDNOG REDA =====
cJSON* parse_table_row(lxb_dom_element_t *tr_elem) {
    if (!tr_elem) return NULL;
    
    cJSON *row_json = cJSON_CreateObject();
    set_json_string(row_json, "tag", "tr");
    set_json_string(row_json, "type", "table-row");
    set_json_bool(row_json, "is_table_row", 1);
    
    cJSON *cells_array = cJSON_CreateArray();
    
    lxb_dom_node_t *node = lxb_dom_node_first_child(lxb_dom_interface_node(tr_elem));
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *td_elem = lxb_dom_interface_element(node);
            size_t tag_len;
            const lxb_char_t *tag = lxb_dom_element_qualified_name(td_elem, &tag_len);
            
            if (tag && tag_len > 0) {
                char tag_name[16];
                size_t copy_len = tag_len < 15 ? tag_len : 15;
                memcpy(tag_name, tag, copy_len);
                tag_name[copy_len] = '\0';
                
                if (strcasecmp(tag_name, "td") == 0 || strcasecmp(tag_name, "th") == 0) {
                    cJSON *cell_json = parse_table_cell(td_elem);
                    if (cell_json) {
                        cJSON_AddItemToArray(cells_array, cell_json);
                    }
                }
            }
        }
        node = lxb_dom_node_next(node);
    }
    
    cJSON_AddItemToObject(row_json, "cells", cells_array);
    set_json_number(row_json, "cell_count", cJSON_GetArraySize(cells_array));
    
    return row_json;
}

// ===== 4. PARSIRANJE JEDNE ĆELIJE =====
cJSON* parse_table_cell(lxb_dom_element_t *td_elem) {
    if (!td_elem) return NULL;
    
    extern int g_next_element_id;
    int cell_id = g_next_element_id++;
    
    cJSON *cell_json = cJSON_CreateObject();
    set_json_number(cell_json, "element_id", cell_id);
    set_json_number(cell_json, "id", cell_id);
    set_json_string(cell_json, "tag", "td");
    set_json_string(cell_json, "type", "table-cell");
    set_json_bool(cell_json, "is_table_cell", 1);
    set_json_string(cell_json, "display", "block");
    
    int colspan = get_attr_int(td_elem, "colspan", 1);
    int rowspan = get_attr_int(td_elem, "rowspan", 1);
    set_json_number(cell_json, "colspan", colspan);
    set_json_number(cell_json, "rowspan", rowspan);
    
    char *id_str = get_attr_str(td_elem, "id");
    if (id_str) {
        set_json_string(cell_json, "dom_id", id_str);
        free(id_str);
    }
    
    char *bg_color = get_attr_str(td_elem, "bgcolor");
    if (!bg_color) {
        bg_color = get_attr_str(td_elem, "bg_color");
    }
    if (bg_color) {
        set_json_string(cell_json, "bg_color", bg_color);
        free(bg_color);
    }
    
    cJSON *children_array = cJSON_CreateArray();
    lxb_dom_node_t *node = lxb_dom_node_first_child(lxb_dom_interface_node(td_elem));
    while (node) {
        cJSON *child = procesuiraj_elemente(node, 0, cell_json);
        if (child) {
            cJSON_AddItemToArray(children_array, child);
        }
        node = lxb_dom_node_next(node);
    }
    
    if (cJSON_GetArraySize(children_array) > 0) {
        cJSON_AddItemToObject(cell_json, "children", children_array);
    } else {
        cJSON_Delete(children_array);
    }
    
    return cell_json;
}

// ===== 5. IZGRADNJA MATRICE (2. PROLAZ) =====
cJSON* build_table_matrix(cJSON *rows_raw) {
    int row_count = cJSON_GetArraySize(rows_raw);
    if (row_count == 0) return NULL;
    
    // Prvo izračunaj maksimalan broj kolona sabirajući colspane po redu
    int max_cols = 0;
    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(rows_raw, r);
        cJSON *cells = cJSON_GetObjectItem(row, "cells");
        if (!cells || !cJSON_IsArray(cells)) continue;
        int cell_count = cJSON_GetArraySize(cells);
        int cols_in_row = 0;
        for (int c = 0; c < cell_count; c++) {
            cJSON *cell = cJSON_GetArrayItem(cells, c);
            cols_in_row += get_json_number(cell, "colspan", 1);
        }
        if (cols_in_row > max_cols) max_cols = cols_in_row;
    }
    if (max_cols == 0) max_cols = 1;
    
    // Kreiramo matricu
    int **grid = calloc(row_count, sizeof(int*));
    cJSON ***cells = calloc(row_count, sizeof(cJSON**));
    for (int r = 0; r < row_count; r++) {
        grid[r] = calloc(max_cols, sizeof(int));
        cells[r] = calloc(max_cols, sizeof(cJSON*));
    }
    
    // Postavljamo ćelije u matricu
    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(rows_raw, r);
        cJSON *cells_array = cJSON_GetObjectItem(row, "cells");
        if (!cells_array || !cJSON_IsArray(cells_array)) continue;
        int cell_index = 0;
        
        for (int c = 0; c < max_cols && cell_index < cJSON_GetArraySize(cells_array); c++) {
            if (grid[r][c] == 0) {  // slobodno mesto
                cJSON *cell = cJSON_GetArrayItem(cells_array, cell_index);
                int colspan = get_json_number(cell, "colspan", 1);
                int rowspan = get_json_number(cell, "rowspan", 1);
                
                // Zauzmi mesto u matrici
                for (int dr = 0; dr < rowspan && r + dr < row_count; dr++) {
                    for (int dc = 0; dc < colspan && c + dc < max_cols; dc++) {
                        grid[r + dr][c + dc] = 1;
                        if (dr == 0 && dc == 0) {
                            cells[r + dr][c + dc] = cell;
                        }
                    }
                }
                
                // Dodaj poziciju u ćeliju (koristimo sigurne funkcije)
                set_json_number(cell, "matrix_row", r);
                set_json_number(cell, "matrix_col", c);
                set_json_number(cell, "matrix_colspan", colspan);
                set_json_number(cell, "matrix_rowspan", rowspan);
                
                cell_index++;
            }
        }
    }
    
    // Kreiramo JSON sa matricom
    cJSON *matrix_json = cJSON_CreateArray();
    for (int r = 0; r < row_count; r++) {
        cJSON *row_json = cJSON_CreateArray();
        for (int c = 0; c < max_cols; c++) {
            cJSON *cell = cells[r][c];
            if (cell) {
                const char *cell_id = get_json_string(cell, "id", "unknown");
                cJSON_AddItemToArray(row_json, cJSON_CreateString(cell_id));
            } else {
                cJSON_AddItemToArray(row_json, cJSON_CreateString("__empty__"));
            }
        }
        cJSON_AddItemToArray(matrix_json, row_json);
    }
    
    // Oslobodi matricu
    for (int r = 0; r < row_count; r++) {
        free(grid[r]);
        free(cells[r]);
    }
    free(grid);
    free(cells);
    
    return matrix_json;
}

// ===== 6. GLAVNA FUNKCIJA ZA TABELU =====
cJSON* parse_table_complete(lxb_dom_element_t *table_elem) {
    if (!table_elem) return NULL;
    
    // 1. Skupljamo sve redove (1. prolaz)
    cJSON *rows_raw = cJSON_CreateArray();
    collect_table_rows(table_elem, rows_raw);
    
    if (cJSON_GetArraySize(rows_raw) == 0) {
        cJSON_Delete(rows_raw);
        return NULL;
    }
    
    // 2. Kreiramo JSON za tabelu
    cJSON *table_json = cJSON_CreateObject();
    set_json_string(table_json, "tag", "table");
    set_json_string(table_json, "type", "block");
    set_json_string(table_json, "display", "block");
    set_json_bool(table_json, "is_table", 1);
    
    // Atributi
    int border = get_attr_int(table_elem, "border", 0);
    int cellspacing = get_attr_int(table_elem, "cellspacing", 2);
    int cellpadding = get_attr_int(table_elem, "cellpadding", 1);
    set_json_number(table_json, "border", border);
    set_json_number(table_json, "cellspacing", cellspacing);
    set_json_number(table_json, "cellpadding", cellpadding);
    
    // 3. Izgradi matricu (2. prolaz)
    cJSON *matrix = build_table_matrix(rows_raw);
    if (matrix) {
        cJSON_AddItemToObject(table_json, "matrix", matrix);
        if (cJSON_GetArraySize(matrix) > 0) {
            set_json_number(table_json, "col_count", cJSON_GetArraySize(cJSON_GetArrayItem(matrix, 0)));
        }
    }
    
    // 4. Dodaj redove
    cJSON_AddItemToObject(table_json, "rows", rows_raw);
    set_json_number(table_json, "row_count", cJSON_GetArraySize(rows_raw));
    
    return table_json;
}

// ===== 7. POMOĆNA FUNKCIJA ZA IZRAČUNAVANJE ŠIRINE SADRŽAJA ĆELIJE =====
int calculate_cell_content_width(cJSON *cell, int max_width) {
    if (!cell) return 60;

    int explicit_width = get_json_number(cell, "explicit_width", 0);
    if (explicit_width > 0) return explicit_width;

    int max_line_width = 0;
    int current_line_width = 0;

    cJSON *children = cJSON_GetObjectItem(cell, "children");
    if (children && cJSON_IsArray(children)) {
        int count = cJSON_GetArraySize(children);
        for (int i = 0; i < count; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            if (!child) continue;

            const char *tag = get_json_string(child, "tag", "");
            const char *display = get_json_string(child, "display", "inline");
            const char *input_type = get_json_string(child, "input_type", "");

            if (strcmp(input_type, "hidden") == 0 || strcmp(display, "none") == 0) {
                continue;
            }

            if (strcmp(tag, "br") == 0 || strcmp(display, "block") == 0) {
                if (current_line_width > max_line_width) {
                    max_line_width = current_line_width;
                }
                current_line_width = 0;
                if (strcmp(tag, "br") == 0) {
                    continue;
                }
            }

            int w = get_json_number(child, "width", 0);

            if (w <= 0) {
                if (strcmp(tag, "input") == 0 || strcmp(tag, "button") == 0) {
                    int is_btn = get_json_bool(child, "is_button", 0) ||
                                 strcmp(input_type, "submit") == 0 ||
                                 strcmp(input_type, "button") == 0;
                    if (is_btn) {
                        const char *val = get_json_string(child, "value", "");
                        if (!val || !val[0]) val = get_json_string(child, "input_value", "");
                        if (!val || !val[0]) val = "Submit";
                        w = estimate_text_width(val, 14, "normal", "normal") + 32;
                        if (w < 100) w = 100;
                    } else if (get_json_bool(child, "is_search_input", 0) || strcmp(input_type, "text") == 0) {
                        w = 260;
                    } else {
                        w = 120;
                    }
                } else if (strcmp(tag, "text") == 0) {
                    const char *txt = get_json_string(child, "text", "");
                    if (!txt || !txt[0]) txt = get_json_string(child, "content", "");
                    if (txt && txt[0]) {
                        int font_size = get_json_number(child, "font_size", 16);
                        w = estimate_text_width(txt, font_size, "normal", "normal") + 4;
                    } else {
                        w = 0;
                    }
                } else if (strcmp(tag, "a") == 0) {
                    const char *txt = get_json_string(child, "text", "");
                    if (!txt || !txt[0]) txt = get_json_string(child, "full_text", "");
                    if (!txt || !txt[0]) txt = get_json_string(child, "content", "");
                    if (txt && txt[0]) {
                        int font_size = get_json_number(child, "font_size", 16);
                        w = estimate_text_width(txt, font_size, "normal", "normal") + 6;
                    } else {
                        w = calculate_cell_content_width(child, max_width);
                    }
                } else {
                    cJSON *sub_children = cJSON_GetObjectItem(child, "children");
                    if (sub_children && cJSON_IsArray(sub_children)) {
                        w = calculate_cell_content_width(child, max_width);
                    } else {
                        const char *txt = get_json_string(child, "text", "");
                        if (!txt || !txt[0]) txt = get_json_string(child, "content", "");
                        if (txt && txt[0]) {
                            int font_size = get_json_number(child, "font_size", 16);
                            w = estimate_text_width(txt, font_size, "normal", "normal") + 4;
                        }
                    }
                }
            }

            int margin_left = get_json_number(child, "margin_left", 0);
            int margin_right = get_json_number(child, "margin_right", 0);
            current_line_width += w + margin_left + margin_right + 4;
        }
    }

    if (current_line_width > max_line_width) {
        max_line_width = current_line_width;
    }

    const char *direct_text = get_json_string(cell, "text", "");
    if (!direct_text || !direct_text[0]) direct_text = get_json_string(cell, "content", "");
    if (direct_text && direct_text[0]) {
        int direct_width = estimate_text_width(direct_text, 16, "normal", "normal") + 20;
        if (direct_width > max_line_width) {
            max_line_width = direct_width;
        }
    }

    if (max_line_width < 60) {
        max_line_width = 60;
    }

    if (max_width > 0 && max_line_width > max_width) {
        return max_width;
    }

    return max_line_width;
}


// ===== 8. DINAMIČKO RAČUNANJE ŠIRINA KOLONA TABELE =====
int* calculate_table_column_widths(cJSON *table_json, int max_width, int *out_col_count) {
    if (!table_json || !out_col_count) return NULL;
    
    int cellspacing = get_json_number(table_json, "cellspacing", 2);
    
    // Pronađi redove (podržava direktan rows niz ili sekcije thead/tbody/tfoot)
    cJSON *all_rows = NULL;
    cJSON *rows_array = NULL;
    int row_count = 0;
    
    cJSON *rows = cJSON_GetObjectItem(table_json, "rows");
    if (rows && cJSON_IsArray(rows)) {
        all_rows = rows;
        row_count = cJSON_GetArraySize(rows);
    } else {
        const char *sections[] = {"thead", "tbody", "tfoot"};
        for (int s = 0; s < 3; s++) {
            cJSON *section = cJSON_GetObjectItem(table_json, sections[s]);
            if (!section) continue;
            cJSON *section_rows = cJSON_GetObjectItem(section, "rows");
            if (section_rows && cJSON_IsArray(section_rows)) {
                if (!rows_array) rows_array = cJSON_CreateArray();
                for (int i = 0; i < cJSON_GetArraySize(section_rows); i++) {
                    cJSON *row = cJSON_GetArrayItem(section_rows, i);
                    cJSON_AddItemToArray(rows_array, cJSON_Duplicate(row, 1));
                    row_count++;
                }
            }
        }
        all_rows = rows_array;
    }
    
    if (row_count == 0 || !all_rows) {
        if (rows_array) cJSON_Delete(rows_array);
        *out_col_count = 0;
        return NULL;
    }
    
    int col_count = get_json_number(table_json, "col_count", 0);
    if (col_count == 0) {
        cJSON *matrix = cJSON_GetObjectItem(table_json, "matrix");
        if (matrix && cJSON_IsArray(matrix) && cJSON_GetArraySize(matrix) > 0) {
            col_count = cJSON_GetArraySize(cJSON_GetArrayItem(matrix, 0));
        }
    }
    if (col_count == 0) {
        for (int r = 0; r < row_count; r++) {
            cJSON *row = cJSON_GetArrayItem(all_rows, r);
            cJSON *cells = cJSON_GetObjectItem(row, "cells");
            if (cells && cJSON_IsArray(cells)) {
                int r_cols = 0;
                for (int c = 0; c < cJSON_GetArraySize(cells); c++) {
                    cJSON *cell = cJSON_GetArrayItem(cells, c);
                    r_cols += get_json_number(cell, "colspan", 1);
                }
                if (r_cols > col_count) col_count = r_cols;
            }
        }
    }
    *out_col_count = col_count;
    
    if (col_count == 0) {
        if (rows_array) cJSON_Delete(rows_array);
        return NULL;
    }
    
    int *col_widths = calloc(col_count, sizeof(int));
    if (!col_widths) {
        if (rows_array) cJSON_Delete(rows_array);
        return NULL;
    }
    
    int **occupied = calloc(row_count, sizeof(int*));
    for (int r = 0; r < row_count; r++) {
        occupied[r] = calloc(col_count, sizeof(int));
    }
    
    SpanCell *span_cells = NULL;
    int span_count = 0;
    
    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(all_rows, r);
        cJSON *cells = cJSON_GetObjectItem(row, "cells");
        if (!cells || !cJSON_IsArray(cells)) continue;
        
        int cell_index = 0;
        int cell_count = cJSON_GetArraySize(cells);
        for (int c = 0; c < col_count && cell_index < cell_count; c++) {
            if (occupied[r][c] == 1) continue;
            
            cJSON *cell = cJSON_GetArrayItem(cells, cell_index);
            int colspan = get_json_number(cell, "colspan", 1);
            int rowspan = get_json_number(cell, "rowspan", 1);
            
            for (int dr = 0; dr < rowspan && r + dr < row_count; dr++) {
                for (int dc = 0; dc < colspan && c + dc < col_count; dc++) {
                    occupied[r + dr][c + dc] = 1;
                }
            }
            
            int cell_width = calculate_cell_content_width(cell, max_width);
            
            if (colspan == 1) {
                if (cell_width > col_widths[c]) {
                    col_widths[c] = cell_width;
                }
            } else {
                span_cells = realloc(span_cells, (span_count + 1) * sizeof(SpanCell));
                span_cells[span_count].start_col = c;
                span_cells[span_count].colspan = colspan;
                span_cells[span_count].total_width = cell_width;
                span_count++;
            }
            
            cell_index++;
        }
    }
    
    // Raspodeli širine za colspan ćelije
    for (int i = 0; i < span_count; i++) {
        SpanCell *sc = &span_cells[i];
        int total_available = 0;
        for (int c = sc->start_col; c < sc->start_col + sc->colspan && c < col_count; c++) {
            total_available += col_widths[c];
        }
        
        if (sc->colspan > 1) {
            total_available += (sc->colspan - 1) * cellspacing;
        }
        
        if (total_available < sc->total_width) {
            int extra = sc->total_width - total_available;
            int per_col = extra / sc->colspan;
            int remainder = extra % sc->colspan;
            
            for (int c = sc->start_col; c < sc->start_col + sc->colspan && c < col_count; c++) {
                col_widths[c] += per_col;
                if (remainder > 0) {
                    col_widths[c]++;
                    remainder--;
                }
            }
        }
    }
    
    // Minimalna širina kolone (baseline osigurač)
    for (int c = 0; c < col_count; c++) {
        if (col_widths[c] < 60) {
            col_widths[c] = 60;
        }
    }
    
    // Upisivanje geometrijski izračunatih širina nazad u cJSON
    for (int r = 0; r < row_count; r++) {
        for (int c = 0; c < col_count; c++) {
            occupied[r][c] = 0;
        }
    }
    
    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(all_rows, r);
        cJSON *cells = cJSON_GetObjectItem(row, "cells");
        if (!cells || !cJSON_IsArray(cells)) continue;
        
        int cell_index = 0;
        int cell_count = cJSON_GetArraySize(cells);
        for (int c = 0; c < col_count && cell_index < cell_count; c++) {
            if (occupied[r][c] == 1) continue;
            
            cJSON *cell = cJSON_GetArrayItem(cells, cell_index);
            int colspan = get_json_number(cell, "colspan", 1);
            int rowspan = get_json_number(cell, "rowspan", 1);
            
            for (int dr = 0; dr < rowspan && r + dr < row_count; dr++) {
                for (int dc = 0; dc < colspan && c + dc < col_count; dc++) {
                    occupied[r + dr][c + dc] = 1;
                }
            }
            
            int geometrijska_sirina = 0;
            for (int j = 0; j < colspan && (c + j) < col_count; j++) {
                geometrijska_sirina += col_widths[c + j];
            }
            if (colspan > 1) {
                geometrijska_sirina += (colspan - 1) * cellspacing;
            }
            
            set_json_number(cell, "width", geometrijska_sirina);
            cell_index++;
        }
    }
    
    for (int r = 0; r < row_count; r++) {
        free(occupied[r]);
    }
    free(occupied);
    if (span_cells) free(span_cells);
    if (rows_array) cJSON_Delete(rows_array);
    
    return col_widths;
}


void layout_table_element(cJSON *element, LayoutContext *ctx) {
    if (!element) return;
    
    cJSON *table_data = cJSON_GetObjectItem(element, "table_data");
    if (!table_data) return;
    
    int table_x = ctx->parent_x;
    int table_y = (ctx->current_y >= 20) ? (ctx->current_y - 20) : ctx->current_y;
    
    cJSON *caption = cJSON_GetObjectItem(table_data, "caption");
    int caption_height = 0;
    
    if (caption) {
        const char *text = get_json_string(caption, "text", "");
        int font_size = 16;
        int text_width = estimate_text_width(text, font_size, "normal", "normal");
        int caption_width = text_width + 20;
        caption_height = font_size + 10;
        
        set_json_number(caption, "width", caption_width);
        set_json_number(caption, "height", caption_height);
        set_json_number(caption, "x", table_x);
        set_json_number(caption, "y", table_y - caption_height - 2);
        set_json_string(caption, "text_align", "center");
    }
    
    if (get_json_bool(element, "use_direct_recursion", 0)) {
        ctx->current_x = table_x + get_json_number(element, "cellspacing", 2);
        ctx->current_y = table_y + get_json_number(element, "cellspacing", 2);
    } else {
        layout_table_cells_natural(element, table_x, table_y, ctx);
    }
    
    int total_width = get_json_number(element, "width", 0);
    int total_height = get_json_number(element, "height", 0);
    
    if (caption) {
        int caption_width = get_json_number(caption, "width", 0);
        int caption_x = table_x + (total_width - caption_width) / 2;
        set_json_number(caption, "x", caption_x);
    }
    
    int parent_id = get_json_number(element, "parent_id", -1);
    if (parent_id != -1 && ctx->root_element) {
        cJSON *parent = find_element_by_id(ctx->root_element, parent_id);
        if (parent) {
            const char *parent_display = get_json_string(parent, "display", "");
            if (strcmp(parent_display, "block") == 0) {
                cJSON *parent_children = cJSON_GetObjectItem(parent, "children");
                int is_last_child = 0;
                
                if (parent_children && cJSON_IsArray(parent_children)) {
                    int child_count = cJSON_GetArraySize(parent_children);
                    if (child_count > 0) {
                        cJSON *last_child = cJSON_GetArrayItem(parent_children, child_count - 1);
                        if (last_child == element) {
                            is_last_child = 1;
                        }
                    }
                }
                
                if (is_last_child) {
                    int parent_y = get_json_number(parent, "y", 0);
                    int table_bottom = table_y + total_height + caption_height;
                    int needed_parent_height = table_bottom - parent_y;
                    set_json_number(parent, "height", needed_parent_height);
                }
            }
        }
    }
    
    int margin_bottom = get_json_number(element, "margin_bottom", 0);
    ctx->current_y = table_y + total_height + margin_bottom;
}


void layout_table_row(cJSON *row_json, LayoutContext *ctx) {
    if (!row_json || !ctx) return;
    
    // ===== ROWSPAN MATRICA (čuva se u kontekstu) =====
    if (!ctx->rowspan_grid) {
        ctx->rowspan_grid = calloc(100, sizeof(int*));
        for (int i = 0; i < 100; i++) {
            ctx->rowspan_grid[i] = calloc(64, sizeof(int));
        }
        ctx->rowspan_max_rows = 100;
        ctx->rowspan_max_cols = 64;
        ctx->current_row = 0;
        memset(ctx->row_heights, 0, sizeof(ctx->row_heights));  // ← DODAJ
    }
    int cellspacing = get_json_number(row_json, "cellspacing", 2);
    int x = ctx->current_x;
    int y = ctx->current_y;
    int current_row = ctx->current_row;
    
    int *col_widths = NULL;
    int col_count = 0;
    
    if (ctx->table_col_widths && ctx->table_col_count > 0) {
        col_widths = ctx->table_col_widths;
        col_count = ctx->table_col_count;
    }
    
    cJSON *cells = cJSON_GetObjectItem(row_json, "cells");
    if (!cells || !cJSON_IsArray(cells)) {
        ctx->current_row++;
        return;
    }
    
    int cell_x = x;
    int max_row_height = 0;
    int cell_index = 0;
    int cell_count = cJSON_GetArraySize(cells);
    
    // ===== PRVI PROLAZ: izračunaj max_row_height =====
    for (int i = 0; i < cell_count; i++) {
        cJSON *cell = cJSON_GetArrayItem(cells, i);
        
        while (cell_index < col_count && 
               ctx->rowspan_grid[current_row][cell_index] == 1) {
            if (col_widths && cell_index < col_count) {
                cell_x += col_widths[cell_index];
            }
            cell_index++;
        }
        
        int colspan = get_json_number(cell, "colspan", 1);
        int cell_width = 0;
        
        if (col_widths && col_count > 0) {
            for (int j = 0; j < colspan && (cell_index + j) < col_count; j++) {
                cell_width += col_widths[cell_index + j]* cellspacing;;
            }
            // 🚀 GEOMETRIJSKI SPREG: Dodajemo razmak unutrašnjih procepa da sprečimo manjak od 2px!
            if (colspan > 1) {
                cell_width += (colspan - 1) * cellspacing;
            }
        } else {
            cell_width = get_json_number(cell, "width", 100);
        }
        
        // Layout sadržaja ćelije
        cJSON *children = cJSON_GetObjectItem(cell, "children");
        int cell_height = 30;
        if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
            LayoutContext cell_ctx = *ctx;
            cell_ctx.current_x = cell_x + 5;
            cell_ctx.current_y = y + 5;
            cell_ctx.container_width = cell_width - 10;
            cell_ctx.table_col_widths = NULL;
            cell_ctx.table_col_count = 0;
            
            for (int j = 0; j < cJSON_GetArraySize(children); j++) {
                cJSON *child = cJSON_GetArrayItem(children, j);
                if (!child) continue;
    
                const char *tag = get_json_string(child, "tag", "");
                const char *inp_type = get_json_string(child, "input_type", "");
    
                // Sakriveni unosi dobijaju nulte dimenzije i ne utiču na kursor
                if (strcmp(tag, "input") == 0 && strcmp(inp_type, "hidden") == 0) {
                    set_json_number(child, "x", -99999);
                    set_json_number(child, "y", -99999);
                    set_json_number(child, "width", 0);
                    set_json_number(child, "height", 0);
                    continue;
                }
    
                // Pozicioniranje za vidljive elemente
                set_json_number(child, "x", cell_ctx.current_x);
                set_json_number(child, "y", cell_ctx.current_y);
    
                if (strcmp(tag, "text") == 0) {
                    cell_ctx.current_y += get_json_number(child, "height", 20);
                } else if (strcmp(tag, "img") == 0) {
                    cell_ctx.current_y += get_json_number(child, "height", 100);
                } else if (strcmp(tag, "br") == 0) {
                    cell_ctx.current_y += 20;
                } else if (strcmp(tag, "div") == 0 || strcmp(tag, "input") == 0) {
                    int child_height = get_json_number(child, "height", 0);
                    if (child_height <= 0) child_height = 30; 
                    cell_ctx.current_y += child_height;
                }
            }
            
            int content_height = cell_ctx.current_y - (y + 5);
            if (content_height > 0) {
                cell_height = content_height + 10;
            }
        }
        
        // Postavi poziciju i visinu
        set_json_number(cell, "x", cell_x);
        set_json_number(cell, "y", y);
        set_json_number(cell, "width", cell_width);
        set_json_number(cell, "height", cell_height);
        
        if (cell_height > max_row_height) {
            max_row_height = cell_height;
        }
        
        cell_x += cell_width;
        cell_index += colspan;
    }
    
    // ===== SAČUVAJ VISINU REDA =====
    ctx->row_heights[current_row] = max_row_height;
    
    // ===== DRUGI PROLAZ: postavi visine za rowspan ćelije =====
    cell_index = 0;
    cell_x = x;
    for (int i = 0; i < cell_count; i++) {
        cJSON *cell = cJSON_GetArrayItem(cells, i);
        
        while (cell_index < col_count && 
               ctx->rowspan_grid[current_row][cell_index] == 1) {
            if (col_widths && cell_index < col_count) {
                cell_x += col_widths[cell_index];
            }
            cell_index++;
        }
        
        int rowspan = get_json_number(cell, "rowspan", 1);
        if (rowspan > 1) {
            // Saberi visine svih redova koje ćelija zauzima
            int total_height = 0;
            for (int r = 0; r < rowspan && (current_row + r) < 100; r++) {
                total_height += ctx->row_heights[current_row + r];
            }
            total_height += (rowspan - 1) * 2; // cellspacing
            set_json_number(cell, "height", total_height);
        }
        
        int colspan = get_json_number(cell, "colspan", 1);
        int cell_width = 0;
        
        if (col_widths && col_count > 0) {
            for (int j = 0; j < colspan && (cell_index + j) < col_count; j++) {
                cell_width += col_widths[cell_index + j]* cellspacing;;
            }
            // 🚀 GEOMETRIJSKI SPREG: Ponovo osiguravamo razmak i kod drugog proračuna!
            if (colspan > 1) {
                cell_width += (colspan - 1) * cellspacing;
            }
        } else {
            cell_width = get_json_number(cell, "width", 100);
        }
        set_json_number(cell, "width", cell_width);
        
        cell_x += cell_width;
        cell_index += colspan;
    }
    
    // Postavi visinu svih ćelija u redu na maksimalnu (za one bez rowspan)
    for (int i = 0; i < cell_count; i++) {
        cJSON *cell = cJSON_GetArrayItem(cells, i);
        int rowspan = get_json_number(cell, "rowspan", 1);
        if (rowspan == 1) {
            set_json_number(cell, "height", max_row_height);
        }
    }
    
    // Ažuriraj Y poziciju za sledeći red
    ctx->current_y += max_row_height + 2;
    ctx->current_x = x;
    ctx->current_row++;
}

void cleanup_table_layout(LayoutContext *ctx) {
    if (!ctx) return;
    
    if (ctx->rowspan_grid) {
        for (int i = 0; i < ctx->rowspan_max_rows; i++) {
            free(ctx->rowspan_grid[i]);
        }
        free(ctx->rowspan_grid);
        ctx->rowspan_grid = NULL;
        ctx->rowspan_max_rows = 0;
        ctx->rowspan_max_cols = 0;
        ctx->current_row = 0;
    }
}

void calculate_table_borders_from_cells(cJSON *table_element) {
    if (!table_element) return;
    return;
    int border = get_json_number(table_element, "border", 0);
    int cellspacing = get_json_number(table_element, "cellspacing", 2);
    
    cJSON *table_data = cJSON_GetObjectItem(table_element, "table_data");
    if (!table_data) return;
    
    int first_x = INT_MAX, first_y = INT_MAX;
    int cell_count = 0;
    
    int visina_celije = 30;  // Fiksna visina iz JSON-a
    int sirina_celije = 100; // Fiksna širina iz JSON-a
    
    // Prolazimo kroz children samo da pokupimo gornju levu tačku tabele (ulazni Y)
    cJSON *children = cJSON_GetObjectItem(table_element, "children");
    if (children && cJSON_IsArray(children)) {
        int child_size = cJSON_GetArraySize(children);
        for (int i = 0; i < child_size; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            
            const char *tag = get_json_string(child, "tag", "");
            int is_cell = (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0 || get_json_bool(child, "is_table_cell", 0));
            
            if (!is_cell) continue;
            
            int x = get_json_number(child, "x", 0);
            int y = get_json_number(child, "y", 0);
            
            cell_count++;
            if (x < first_x) first_x = x;
            if (y < first_y) first_y = y;
        }
    }
    
    // Ako nema layout-ovanih ćelija, uzimamo poziciju same tabele kao osnovu
    if (first_x == INT_MAX) first_x = get_json_number(table_element, "x", 0);
    if (first_y == INT_MAX) first_y = get_json_number(table_element, "y", 68);
    
    // Prisilno čitamo pravu strukturu matrice iz row_count (vraća 3)
    int broj_redova = get_json_number(table_data, "row_count", 3);
    int broj_kolona = 3; // Fiksirano na 3 kolone za našu test stranu jer je col_count 0
    
    if (broj_redova <= 0) broj_redova = 3;
    
    // ========== MATEMATIKA MATRICE (Bez ijednog -1 skraćenja) ==========
    int total_cell_height = (broj_redova * visina_celije) + ((broj_redova - 1) * cellspacing);
    int total_cell_width = (broj_kolona * sirina_celije) + ((broj_kolona - 1) * cellspacing);
    
    // ========== POSTAVLJANJE STRUKTURE KVADRATA ==========
    // Pomeramo gornji levi ćošak okvira za 3 piksela ulevo i nagore
    set_json_number(table_element, "x", first_x - border - cellspacing - 3);
    set_json_number(table_element, "y", first_y - border - cellspacing - 3);
    
    // Dodajemo čistih +12px na širinu i visinu da okvir obuhvati i poslednje ivice kako treba
    int final_width = total_cell_width + (2 * border) + (2 * cellspacing) + 12;
    int final_height = total_cell_height + (2 * border) + (2 * cellspacing) + 12;
    
    set_json_number(table_element, "width", final_width);
    set_json_number(table_element, "height", final_height);
}



// ==========================================================
// POMOĆNA FUNKCIJA: Rekurzivno renderovanje sadržaja ćelije
// ==========================================================
static void render_cell_content_recursive(pauk_ui_t *pauk_ui, cJSON *element,
    int cell_x, int cell_y,
    int cell_width, int cellpadding,
    html_font_t *font, int *current_y,
    int offset_x, int offset_y,
    int scroll_y) {
    if (!element) return;
    const char *tag = get_json_string(element, "tag", "");
    if (strcmp(tag, "input") == 0) {
        const char *input_type = get_json_string(element, "input_type", "text");
        if (strcmp(input_type, "hidden") == 0) return;
        int elem_x = get_json_number(element, "x", -99999);
        int elem_y = get_json_number(element, "y", -99999);
        int elem_h = get_json_number(element, "height", 35);
        if (elem_x != -99999) {
            elem_x += offset_x;
        } else {
            elem_x = cell_x + cellpadding;
        }
        if (elem_y != -99999) {
            elem_y += offset_y - scroll_y;
        } else {
            elem_y = *current_y - scroll_y;
        }
        extern void render_input_element(pauk_ui_t *pauk_ui, cJSON *element, 
                                          int x, int y, html_font_t *font);
        render_input_element(pauk_ui, element, elem_x, elem_y, font);
        *current_y += elem_h + 4;
        return;
    }
    if (strcmp(tag, "text") == 0) {
        const char *text = get_json_string(element, "text", "");
        if (!text || !text[0]) text = get_json_string(element, "content", "");
        if (text && text[0]) {
            int font_size = get_json_number(element, "font_size", 16);
            int elem_x = get_json_number(element, "x", -99999);
            int elem_y = get_json_number(element, "y", -99999);
            if (elem_x != -99999) {
                elem_x += offset_x;
            } else {
                elem_x = cell_x + cellpadding;
            }
            if (elem_y != -99999) {
                elem_y += offset_y - scroll_y;
            } else {
                elem_y = *current_y - scroll_y;
            }
    
            // ===== PROVERA ZA <br> U TEKSTU =====
            if (strstr(text, "<br>") != NULL || strchr(text, '\n') != NULL) {
                // Tekst sadrži prelom reda - renderuj liniju po liniju
                char *text_copy = strdup(text);
                if (text_copy) {
                    char *line = text_copy;
                    char *next;
                    while (line && *line) {
                        // Nađi sledeći <br> ili \n
                        next = strstr(line, "<br>");
                        if (!next) next = strchr(line, '\n');
    
                        if (next) {
                            *next = '\0';
                            next += (next == strstr(line, "<br>")) ? 4 : 1;
                        }
    
                        // Renderuj trenutnu liniju
                        if (*line) {
                            render_ttf_text_to_pixelmap(pauk_ui, line, elem_x, elem_y,
                                                        font, (float)font_size, 0xFF000000, 0, 0);
                        }
    
                        // Pomeri Y za sledeći red
                        elem_y += font_size + 4;
                        *current_y += font_size + 4;
    
                        line = next;
                    }
                    free(text_copy);
                }
            } else {
                // Nema <br> - renderuj kao jednu liniju
                render_ttf_text_to_pixelmap(pauk_ui, text, elem_x, elem_y,
                                            font, (float)font_size, 0xFF000000, 0, 0);
                *current_y += font_size + 4;
            }
        }
        return;
    }
    if (strcmp(tag, "a") == 0) {
        const char *text = get_json_string(element, "text", "");
        if (!text || !text[0]) text = get_json_string(element, "content", "");
        if (text && text[0]) {
            int font_size = get_json_number(element, "font_size", 16);
            int elem_x = get_json_number(element, "x", -99999);
            int elem_y = get_json_number(element, "y", -99999);
            if (elem_x != -99999) {
                elem_x += offset_x;
            } else {
                elem_x = cell_x + cellpadding;
            }
            if (elem_y != -99999) {
                elem_y += offset_y - scroll_y;
            } else {
                elem_y = *current_y - scroll_y;
            }
    
            // ===== PROVERA ZA <br> U TEKSTU =====
            if (strstr(text, "<br>") != NULL || strchr(text, '\n') != NULL) {
                char *text_copy = strdup(text);
                if (text_copy) {
                    char *line = text_copy;
                    char *next;
                    while (line && *line) {
                        next = strstr(line, "<br>");
                        if (!next) next = strchr(line, '\n');
    
                        if (next) {
                            *next = '\0';
                            next += (next == strstr(line, "<br>")) ? 4 : 1;
                        }
    
                        if (*line) {
                            render_ttf_text_to_pixelmap(pauk_ui, line, elem_x, elem_y,
                                                        font, (float)font_size, 0xFF0000FF, 1, 0);
                        }
    
                        elem_y += font_size + 4;
                        *current_y += font_size + 4;
    
                        line = next;
                    }
                    free(text_copy);
                }
            } else {
                render_ttf_text_to_pixelmap(pauk_ui, text, elem_x, elem_y,
                                            font, (float)font_size, 0xFF0000FF, 1, 0);
                *current_y += font_size + 4;
            }
        }
        return;
    }
    
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        int child_count = cJSON_GetArraySize(children);
        for (int i = 0; i < child_count; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            render_cell_content_recursive(pauk_ui, child, cell_x, cell_y,
                                          cell_width, cellpadding, font, 
                                          current_y, offset_x, offset_y, scroll_y);
        }
    }
}


// =========================================================================
// 🚀 STANDARD GLOBAL HELPER FUNCTIONS (OUTSIDE OF THE LAYOUT FUNCTION)
// =========================================================================

static void pronadji_koordinate_po_nazivu_rekurzivno(cJSON *izvor, int trazeni_id, 
    const char *trazeni_tag, const char *trazeno_ime, 
    int *nasao, int *ox, int *oy, int *ow, int *oh) {
    
    if (!izvor || *nasao) return;

    if (cJSON_IsArray(izvor)) {
        cJSON *item;
        cJSON_ArrayForEach(item, izvor) {
            pronadji_koordinate_po_nazivu_rekurzivno(item, trazeni_id, trazeni_tag, 
                                                     trazeno_ime, nasao, ox, oy, ow, oh);
        }
        return;
    }

    int struc_id = get_json_number(izvor, "element_id", -2);
    if (struc_id == trazeni_id && trazeni_id != -1) {
        *ox = get_json_number(izvor, "x", 0);
        *oy = get_json_number(izvor, "y", 0);
        *ow = get_json_number(izvor, "width", 0);
        *oh = get_json_number(izvor, "height", 0);
        *nasao = 1;
        return;
    }

    const char *struc_tag = get_json_string(izvor, "tag", "");
    if (strcmp(struc_tag, trazeni_tag) == 0 && strlen(trazeno_ime) > 0) {
        const char *struc_name = get_json_string(izvor, "input_name", "");
        if (strcmp(struc_name, trazeno_ime) == 0) {
            *ox = get_json_number(izvor, "x", 0);
            *oy = get_json_number(izvor, "y", 0);
            *ow = get_json_number(izvor, "width", 0);
            *oh = get_json_number(izvor, "height", 0);
            *nasao = 1;
            return;
        }
    }

    const char *kljucevi[] = {"children", "cells", "rows"};
    for (int i = 0; i < 3; i++) {
        cJSON *sub = cJSON_GetObjectItem(izvor, kljucevi[i]);
        if (sub) {
            pronadji_koordinate_po_nazivu_rekurzivno(sub, trazeni_id, trazeni_tag, 
                                                     trazeno_ime, nasao, ox, oy, ow, oh);
        }
    }
}

static void duboki_flat_sink_rekurzivno(cJSON *mrtvi_cvor, cJSON *zivo_stablo) {
    if (!mrtvi_cvor) return;

    if (cJSON_IsArray(mrtvi_cvor)) {
        cJSON *item;
        cJSON_ArrayForEach(item, mrtvi_cvor) {
            duboki_flat_sink_rekurzivno(item, zivo_stablo);
        }
        return;
    }

    int m_id = get_json_number(mrtvi_cvor, "element_id", -1);
    const char *m_tag = get_json_string(mrtvi_cvor, "tag", "");
    const char *m_name = get_json_string(mrtvi_cvor, "input_name", "");

    if (m_id != -1) {
        int nasao = 0;
        int ox = 0, oy = 0, ow = 0, oh = 0;
        pronadji_koordinate_po_nazivu_rekurzivno(zivo_stablo, m_id, m_tag, m_name, 
                                                  &nasao, &ox, &oy, &ow, &oh);

        if (nasao) {
            set_json_number(mrtvi_cvor, "x", ox);
            set_json_number(mrtvi_cvor, "y", oy);
            set_json_number(mrtvi_cvor, "width", ow);
            set_json_number(mrtvi_cvor, "height", oh);
            printf("### SINK OK: id=%d, tag=%s, x=%d, y=%d\n", m_id, m_tag, ox, oy);
        }
    }

    // 🚀 PROLAZI KROZ children, cells, rows
    const char *kljucevi[] = {"children", "cells", "rows"};
    for (int i = 0; i < 3; i++) {
        cJSON *sub = cJSON_GetObjectItem(mrtvi_cvor, kljucevi[i]);
        if (sub) {
            duboki_flat_sink_rekurzivno(sub, zivo_stablo);
        }
    }
}

// =========================================================================
// 🚀 MAIN LAYOUT FUNCTION
// =========================================================================

void layout_table_cells_natural(cJSON *table_element, int table_x, int table_y, LayoutContext *ctx) {
    cJSON *table_data = cJSON_GetObjectItem(table_element, "table_data");
    if (!table_data) table_data = table_element;
    if (!table_data) return;

    int border = get_json_number(table_element, "border", 1);
    int cellspacing = get_json_number(table_element, "cellspacing", 2);

    int col_count = 0;
    int max_avail_width = (ctx && ctx->container_width > 0) ? ctx->container_width : 0;
    int *col_widths = calculate_table_column_widths(table_data, max_avail_width, &col_count);

    cJSON *all_rows = NULL;
    cJSON *rows_array = NULL;
    int row_count = 0;

    cJSON *rows = cJSON_GetObjectItem(table_data, "rows");
    if (rows && cJSON_IsArray(rows)) {
        all_rows = rows;
        row_count = cJSON_GetArraySize(rows);
    } else {
        const char *sections[] = {"thead", "tbody", "tfoot"};
        for (int s = 0; s < 3; s++) {
            cJSON *section = cJSON_GetObjectItem(table_data, sections[s]);
            if (!section) continue;
            cJSON *section_rows = cJSON_GetObjectItem(section, "rows");
            if (section_rows && cJSON_IsArray(section_rows)) {
                if (!rows_array) rows_array = cJSON_CreateArray();
                for (int i = 0; i < cJSON_GetArraySize(section_rows); i++) {
                    cJSON *row = cJSON_GetArrayItem(section_rows, i);
                    cJSON_AddItemToArray(rows_array, cJSON_Duplicate(row, 1));
                    row_count++;
                }
            }
        }
        all_rows = rows_array;
    }

    if (row_count == 0 || !all_rows) {
        if (col_widths) free(col_widths);
        if (rows_array) cJSON_Delete(rows_array);
        return;
    }

    if (col_count == 0) col_count = 1;

    int **occupied = calloc(row_count, sizeof(int*));
    for (int r = 0; r < row_count; r++) {
        occupied[r] = calloc(col_count, sizeof(int));
    }

    int row_heights[100] = {0};
    int current_y = table_y + border + cellspacing;

    int min_cell_x = 0, min_cell_y = 0;
    int max_cell_right = 0, max_cell_bottom = 0;
    int first_cell = 1;

    typedef struct {
        cJSON *cell;
        int start_row;
        int start_col;
        int rowspan;
        int original_x;
        int original_y;
        int original_width;
    } RowSpanInfo;

    RowSpanInfo *rowspan_cells = NULL;
    int rowspan_count = 0;

    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(all_rows, r);
        if (!row) continue;

        cJSON *cells = cJSON_GetObjectItem(row, "cells");
        if (!cells || !cJSON_IsArray(cells)) continue;

        int current_x = table_x + border + cellspacing;
        int max_row_height = 0;
        int cell_index = 0;
        int cell_count = cJSON_GetArraySize(cells);

        for (int c = 0; c < col_count && cell_index < cell_count; c++) {
            if (occupied[r][c] == 1) {
                if (col_widths && c < col_count) {
                    current_x += col_widths[c] + cellspacing;
                }
                continue;
            }

            cJSON *cell = cJSON_GetArrayItem(cells, cell_index);
            int colspan = get_json_number(cell, "colspan", 1);
            int rowspan = get_json_number(cell, "rowspan", 1);

            for (int dr = 0; dr < rowspan && (r + dr) < row_count; dr++) {
                for (int dc = 0; dc < colspan && (c + dc) < col_count; dc++) {
                    occupied[r + dr][c + dc] = 1;
                }
            }

            int cell_width = 0;
            if (col_widths) {
                for (int j = 0; j < colspan && (c + j) < col_count; j++) {
                    cell_width += col_widths[c + j];
                }
                if (colspan > 1) {
                    cell_width += (colspan - 1) * cellspacing;
                }
            } else {
                cell_width = get_json_number(cell, "width", 100);
            }

            if (cell_width <= 0) cell_width = 80;

            set_json_number(cell, "width", cell_width);
            set_json_number(cell, "x", current_x);
            set_json_number(cell, "y", current_y);

            int cell_height = layout_table_cell(cell, current_x, current_y, cell_width, ctx);
            set_json_number(cell, "height", cell_height);

            int cell_right = current_x + cell_width;
            int cell_bottom = current_y + cell_height;

            if (first_cell) {
                min_cell_x = current_x;
                min_cell_y = current_y;
                max_cell_right = cell_right;
                max_cell_bottom = cell_bottom;
                first_cell = 0;
            } else {
                if (current_x < min_cell_x) min_cell_x = current_x;
                if (current_y < min_cell_y) min_cell_y = current_y;
                if (cell_right > max_cell_right) max_cell_right = cell_right;
                if (cell_bottom > max_cell_bottom) max_cell_bottom = cell_bottom;
            }

            if (rowspan == 1) {
                if (cell_height > max_row_height) {
                    max_row_height = cell_height;
                }
            }

            if (rowspan > 1) {
                RowSpanInfo info;
                info.cell = cell;
                info.start_row = r;
                info.start_col = c;
                info.rowspan = rowspan;
                info.original_x = current_x;
                info.original_y = current_y;
                info.original_width = cell_width;
                rowspan_cells = realloc(rowspan_cells, (rowspan_count + 1) * sizeof(RowSpanInfo));
                rowspan_cells[rowspan_count++] = info;
            }

            current_x += cell_width + cellspacing;
            cell_index++;
        }

        if (max_row_height < 24) max_row_height = 24;
        if (r < 100) {
            row_heights[r] = max_row_height;
        }

        current_y += max_row_height + cellspacing;
    }

    for (int i = 0; i < rowspan_count; i++) {
        RowSpanInfo *info = &rowspan_cells[i];
        int total_spanned_height = 0;
        for (int dr = 0; dr < info->rowspan && (info->start_row + dr) < row_count && (info->start_row + dr) < 100; dr++) {
            total_spanned_height += row_heights[info->start_row + dr];
        }

        if (info->rowspan > 1) {
            total_spanned_height += (info->rowspan - 1) * cellspacing;
        }
        set_json_number(info->cell, "height", total_spanned_height);

        int cell_bottom = info->original_y + total_spanned_height;
        if (cell_bottom > max_cell_bottom) {
            max_cell_bottom = cell_bottom;
        }
    }

    if (!first_cell) {
        int table_total_width = (max_cell_right - table_x) + border + cellspacing;
        int table_total_height = (max_cell_bottom - table_y) + border + cellspacing;

        set_json_number(table_element, "x", table_x);
        set_json_number(table_element, "y", table_y);
        set_json_number(table_element, "width", table_total_width);
        set_json_number(table_element, "height", table_total_height);
    }

    // 🚀 BEZUSLOVNI SINK SVIH INPUTA I DUGMADI
    cJSON *root_children = cJSON_GetObjectItem(table_data, "children");
    cJSON *zivi_rows = cJSON_GetObjectItem(table_data, "rows");
    
    printf("### SINK CHECK: table_element=%p, table_data=%p\n", table_element, table_data);
    printf("### SINK CHECK: root_children=%p, zivi_rows=%p\n", root_children, zivi_rows);
    
    if (root_children && cJSON_IsArray(root_children) && zivi_rows && cJSON_IsArray(zivi_rows)) {
        printf("### SINK START\n");
        duboki_flat_sink_rekurzivno(root_children, zivi_rows);
    } else {
        printf("### SINK SKIP\n");
    }

    if (root_children && cJSON_IsArray(root_children) && zivi_rows && cJSON_IsArray(zivi_rows)) {
        duboki_flat_sink_rekurzivno(root_children, zivi_rows);
    }

    for (int r = 0; r < row_count; r++) {
        free(occupied[r]);
    }
    free(occupied);

    if (col_widths) free(col_widths);
    if (rowspan_cells) free(rowspan_cells);
    if (rows_array) cJSON_Delete(rows_array);
}







// ==========================================================
// TABLE SECTION LAYOUT (thead, tbody, tfoot)
// ==========================================================
void layout_table_section(cJSON *element, LayoutContext *ctx) {
    if (!element) return;
    
    // Sections are containers for rows - they don't create their own visual box
    int x = ctx->parent_x;
    int y = ctx->current_y;

    set_json_number(element, "x", ctx->parent_x); 
    set_json_number(element, "y", ctx->parent_y); 
    // Sections don't have height of their own - height comes from rows
    // cJSON_ReplaceItemInObject(element, "width", cJSON_CreateNumber(ctx->container_width));
    
    // Process rows in this section
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        LayoutContext child_ctx = *ctx;
        child_ctx.current_x = x;
        child_ctx.current_y = y;
        
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            const char *child_tag = get_json_string(child, "tag", "");
            if (strcmp(child_tag, "tr") == 0) {
                // DON'T layout rows here - let racunaj_pozicije do it
                // Just process their children? No, remove this entirely
                
                // Instead, just update the context for the next row
                // The actual layout will happen when racunaj_pozicije processes this <tr>
            }
        }
        
        // Update section height based on where we ended
        int section_height = child_ctx.current_y - y;
       // cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(section_height));
        set_json_number(element, "height", section_height); 
        // Update context for next element
        ctx->current_y = child_ctx.current_y;
    }
}


static void popravi_koordinate_potomaka_rekurzivno(cJSON *child, int parent_x, int parent_y) {
    if (!child) return;

    int cur_x = get_json_number(child, "x", -99999);
    int cur_y = get_json_number(child, "y", -99999);

    if (cur_x == -99999) {
        cur_x = parent_x;
        set_json_number(child, "x", cur_x);
    } else if (cur_x >= 0 && cur_x < parent_x) {
        cur_x = parent_x + cur_x;
        set_json_number(child, "x", cur_x);
    }

    if (cur_y == -99999) {
        cur_y = parent_y;
        set_json_number(child, "y", cur_y);
    } else if (cur_y >= 0 && cur_y < parent_y) {
        cur_y = parent_y + cur_y;
        set_json_number(child, "y", cur_y);
    }

    const char *tag = get_json_string(child, "tag", "");
    if (strcmp(tag, "text") == 0) {
        int w = get_json_number(child, "width", 0);
        int h = get_json_number(child, "height", 0);
        const char *txt = get_json_string(child, "text", "");
        if (!txt || !txt[0]) txt = get_json_string(child, "content", "");
        if (txt && txt[0]) {
            int font_size = get_json_number(child, "font_size", 16);
            if (w <= 0) {
                w = estimate_text_width(txt, font_size, "normal", "normal");
                set_json_number(child, "width", w);
            }
            if (h <= 0) {
                h = font_size + 4;
                set_json_number(child, "height", h);
            }
        }
    }

    set_json_number(child, "layout_calculated", 1);

    cJSON *children = cJSON_GetObjectItem(child, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *sub_child;
        cJSON_ArrayForEach(sub_child, children) {
            popravi_koordinate_potomaka_rekurzivno(sub_child, cur_x, cur_y);
        }
    }
}

// ==========================================================
// TABLE CELL LAYOUT (td and th)
// ==========================================================
int layout_table_cell(cJSON *element, int x, int y, int cell_width, LayoutContext *ctx) {
    if (!element) return 20;

    int border = get_json_number(element, "border", 1);
    int cellpadding = get_json_number(element, "cellpadding", 1);

    const char *tag = get_json_string(element, "tag", "");
    if (strcmp(tag, "th") == 0) {
        set_json_string(element, "font_weight", "bold");
        set_json_string(element, "bg_color", "#f0f0f0");
    }

    int content_x = x + border + cellpadding;
    int content_y = y + border + cellpadding;
    int content_width = cell_width - (2 * (border + cellpadding));
    if (content_width < 0) content_width = 0;

    set_json_number(element, "x", x);
    set_json_number(element, "y", y);
    set_json_number(element, "width", cell_width);
    set_json_number(element, "layout_calculated", 1);

    int max_child_bottom = content_y;
    cJSON *children = cJSON_GetObjectItem(element, "children");

    if (children && cJSON_IsArray(children)) {
        LayoutContext cell_ctx;
        if (ctx) {
            cell_ctx = *ctx;
        } else {
            memset(&cell_ctx, 0, sizeof(LayoutContext));
        }
        cell_ctx.parent_element = element;
        cell_ctx.parent_x = content_x;
        cell_ctx.parent_y = content_y;
        cell_ctx.container_width = content_width;
        cell_ctx.available_width = content_width;
        cell_ctx.current_x = content_x;
        cell_ctx.current_y = content_y;
        cell_ctx.line_height = 0;

        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            const char *child_display = get_json_string(child, "display", "inline");

            int stvarni_pocetak_x = cell_ctx.current_x;
            int stvarni_pocetak_y = cell_ctx.current_y;

            if (strcmp(child_display, "block") == 0) {
                layout_block_element(child, &cell_ctx);
            } else {
                layout_inline_element(child, &cell_ctx);
            }

            int potvrdjen_x = get_json_number(child, "x", stvarni_pocetak_x);
            int potvrdjen_y = get_json_number(child, "y", stvarni_pocetak_y);

            if (potvrdjen_x == -99999) {
                potvrdjen_x = stvarni_pocetak_x;
                set_json_number(child, "x", potvrdjen_x);
            }
            if (potvrdjen_y == -99999) {
                potvrdjen_y = stvarni_pocetak_y;
                set_json_number(child, "y", potvrdjen_y);
            }

            set_json_number(child, "layout_calculated", 1);

            cJSON *sub_children = cJSON_GetObjectItem(child, "children");
            if (sub_children && cJSON_IsArray(sub_children)) {
                cJSON *sub_child;
                cJSON_ArrayForEach(sub_child, sub_children) {
                    popravi_koordinate_potomaka_rekurzivno(sub_child, potvrdjen_x, potvrdjen_y);
                }
            }

            int child_y = get_json_number(child, "y", stvarni_pocetak_y);
            int child_height = get_json_number(child, "height", 20);
            int child_bottom = child_y + child_height;
            if (child_bottom > max_child_bottom) {
                max_child_bottom = child_bottom;
            }

            if (sub_children && cJSON_IsArray(sub_children)) {
                cJSON *sub_child;
                cJSON_ArrayForEach(sub_child, sub_children) {
                    int sc_y = get_json_number(sub_child, "y", child_y);
                    int sc_h = get_json_number(sub_child, "height", 20);
                    if (sc_y + sc_h > max_child_bottom) {
                        max_child_bottom = sc_y + sc_h;
                    }
                }
            }
        }
    }

    int content_height = max_child_bottom - content_y;
    int cell_height = content_height + border + cellpadding;
    if (cell_height < 20) cell_height = 20;

    set_json_number(element, "height", cell_height);
    set_json_number(element, "layout_calculated", 1);

    return cell_height;
}





void render_table_from_data(pauk_ui_t* pauk_ui, cJSON* table_data, 
    int table_x, int table_y, 
    int offset_x, int offset_y, int scroll_y) {
    
    if (!table_data || !pauk_ui) return;
    
    int border = get_json_number(table_data, "border", 1);
    int cellpadding = get_json_number(table_data, "cellpadding", 1);
    
    int tbl_w = get_json_number(table_data, "width", 0);
    int tbl_h = get_json_number(table_data, "height", 0);
    
    const char *tbl_bg = get_json_string(table_data, "bg_color", NULL);
    if (!tbl_bg) tbl_bg = get_json_string(table_data, "bgcolor", NULL);
    if (tbl_bg && tbl_w > 0 && tbl_h > 0) {
        uint32_t bg_col = css_color_to_uint32(tbl_bg);
        draw_filled_box_to_pixelmap(pauk_ui, table_x, table_y, tbl_w, tbl_h, bg_col);
    }
    
    if (border > 0 && tbl_w > 0 && tbl_h > 0) {
        draw_box_border(pauk_ui, table_x, table_y, tbl_w, tbl_h, border, 0xFF000000, 0, "solid");
    }
    
    cJSON *all_rows = cJSON_GetObjectItem(table_data, "rows");
    if (!all_rows || !cJSON_IsArray(all_rows)) {
        all_rows = cJSON_GetObjectItem(table_data, "children");
    }
    if (!all_rows || !cJSON_IsArray(all_rows)) return;
    
    int row_count = cJSON_GetArraySize(all_rows);
    html_font_t *font = font_manager_get_font(&pauk_ui->font_manager, 
                                              pauk_ui->font_manager.default_font_index);
    
    for (int r = 0; r < row_count; r++) {
        cJSON *row = cJSON_GetArrayItem(all_rows, r);
        if (!row) continue;
        
        cJSON *cells = cJSON_GetObjectItem(row, "cells");
        if (!cells || !cJSON_IsArray(cells)) {
            cells = cJSON_GetObjectItem(row, "children");
        }
        if (!cells || !cJSON_IsArray(cells)) continue;
        
        int cell_count = cJSON_GetArraySize(cells);
        for (int c = 0; c < cell_count; c++) {
            cJSON *cell = cJSON_GetArrayItem(cells, c);
            if (!cell) continue;
            
            int cell_x = get_json_number(cell, "x", 0) + offset_x;
            int cell_y = get_json_number(cell, "y", 0) + offset_y - scroll_y;
            int cell_w = get_json_number(cell, "width", 0);
            int cell_h = get_json_number(cell, "height", 0);
            
            const char *cell_bg = get_json_string(cell, "bg_color", NULL);
            if (!cell_bg) cell_bg = get_json_string(cell, "bgcolor", NULL);
            if (cell_bg && cell_w > 0 && cell_h > 0) {
                uint32_t bg = css_color_to_uint32(cell_bg);
                draw_filled_box_to_pixelmap(pauk_ui, cell_x, cell_y, cell_w, cell_h, bg);
            }
            
            if (border > 0 && cell_w > 0 && cell_h > 0) {
                draw_box_border(pauk_ui, cell_x, cell_y, cell_w, cell_h, border, 0xFF808080, 0, "solid");
            }
            
            cJSON *children = cJSON_GetObjectItem(cell, "children");
            if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
                int text_cur_y = cell_y + cellpadding;
                int num_children = cJSON_GetArraySize(children);
                for (int ci = 0; ci < num_children; ci++) {
                    cJSON *child = cJSON_GetArrayItem(children, ci);
                    render_cell_content_recursive(pauk_ui, child, cell_x, cell_y,
                                                   cell_w, cellpadding, font,
                                                   &text_cur_y, offset_x, offset_y, scroll_y);
                }
            } else {
                const char *text = get_json_string(cell, "text", "");
                if (!text || !text[0]) text = get_json_string(cell, "content", "");
                if (text && text[0]) {
                    int font_size = get_json_number(cell, "font_size", 16);
                    render_ttf_text_to_pixelmap(pauk_ui, text, cell_x + cellpadding, 
                                                cell_y + cellpadding,
                                                font, (float)font_size, 0xFF000000, 0, 0);
                }
            }
        }
    }
}


