#include <stdio.h>
#include <unistd.h>  
#include <errno.h>
#include <str_error.h>
#include "cjson.h"

#include <ui/ui.h>
#include <ui/window.h>
#include <ui/tab.h>
#include <ui/tabset.h>
#include <ui/list.h>
#include <ui/scrollbar.h>
#include <ui/rbutton.h>
#include <io/pixelmap.h>

#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>
#include <gfx/color.h>
#include <gfx/font.h>
#include <gfx/typeface.h>
#include <gfx/cursor.h>
#include <gfx/coord.h>
#include <gfximage/tga.h>

#include "gui.h"
#include "font_manager.h"
#include "forms_parser.h"
#include "rendering_elements/defaults.h"
#include "render_func.h"
#include "change_size.h"
#include "layout_engine.h"
#include "bookmarks.h"
#include "pauk_tls.h"
#include "network.h"
#include "url_utils.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

pauk_ui_t *global_pauk_ui = NULL;

//definicije
static void handle_keyboard_event(ui_window_t *window, void *arg, kbd_event_t *event);

static errno_t create_color(uint16_t r, uint16_t g, uint16_t b, gfx_color_t **color) {
    return gfx_color_new_rgb_i16(r, g, b, color);
}

/**
 * @brief Parses a hex color string (#RGB, #RRGGBB).
 * @param str Hex string starting after '#'.
 * @param len Length of the hex part (3 or 6).
 * @return 32-bit ARGB color.
 */
uint32_t parse_hex_color(const char *str, size_t len) {
    uint32_t color = 0xFF000000;  // Default: opaque black
    
    if (len == 3) {
        // #RGB -> #RRGGBB (your implementation is perfect)
        for (int i = 0; i < 3; i++) {
            int d = hex_digit(str[i]);
            if (d == -1) return 0xFF000000;
            uint8_t byte = (d * 16) + d;
            // Shift: R=16, G=8, B=0
            color |= (uint32_t)byte << (16 - i * 8);
        }
    } else if (len == 6) {
        // #RRGGBB (your implementation is perfect)
        int d1 = hex_digit(str[0]);
        int d2 = hex_digit(str[1]);
        int d3 = hex_digit(str[2]);
        int d4 = hex_digit(str[3]);
        int d5 = hex_digit(str[4]);
        int d6 = hex_digit(str[5]);
        
        if (d1 == -1 || d2 == -1 || d3 == -1 || d4 == -1 || d5 == -1 || d6 == -1)
            return 0xFF000000;
        
        uint8_t r = (d1 * 16) + d2;
        uint8_t g = (d3 * 16) + d4;
        uint8_t b = (d5 * 16) + d6;
        
        color = 0xFF000000 | (r << 16) | (g << 8) | b;
    } else if (len == 8) {
        // #RRGGBBAA (ADDED: Support 8-digit hex with alpha)
        int d1 = hex_digit(str[0]);
        int d2 = hex_digit(str[1]);
        int d3 = hex_digit(str[2]);
        int d4 = hex_digit(str[3]);
        int d5 = hex_digit(str[4]);
        int d6 = hex_digit(str[5]);
        int d7 = hex_digit(str[6]);
        int d8 = hex_digit(str[7]);
        
        if (d1 == -1 || d2 == -1 || d3 == -1 || d4 == -1 || 
            d5 == -1 || d6 == -1 || d7 == -1 || d8 == -1)
            return 0xFF000000;
        
        uint8_t r = (d1 * 16) + d2;
        uint8_t g = (d3 * 16) + d4;
        uint8_t b = (d5 * 16) + d6;
        uint8_t a = (d7 * 16) + d8;  // Alpha channel
        
        // ARGB format: a << 24 | r << 16 | g << 8 | b
        color = (a << 24) | (r << 16) | (g << 8) | b;
    }
    // Note: len == 4 (#RGBA) could also be supported if needed
    
    return color;
}

/**
 * @brief Converts a hex character ('0'-'9', 'a'-'f', 'A'-'F') to its integer value.
 */
int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Start hover timer
void start_hover_timer(pauk_ui_t *pauk_ui, int interval_ms) {
    if (!pauk_ui->hover_timer) {
        // Create only once
        pauk_ui->hover_timer = fibril_timer_create(NULL);
    }
    
    // Clear and re-set the SAME timer
    fibril_timer_clear(pauk_ui->hover_timer);
    fibril_timer_set(pauk_ui->hover_timer, 
                    interval_ms * 1000,
                    hover_timer_callback, 
                    pauk_ui);
}

void hover_timer_callback(void *arg) {
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    if (!hover_timer_running) return;
    
    check_hover(pauk_ui);
    
    // Re-arm the SAME timer
    if (hover_timer_running) {
        fibril_timer_set(pauk_ui->hover_timer, 
                        pauk_ui->hover_interval,
                        hover_timer_callback, 
                        pauk_ui);
    }
}


// Stop hover timer
void stop_hover_timer(pauk_ui_t *pauk_ui) {
    if (pauk_ui->hover_timer) {
        fibril_timer_clear(pauk_ui->hover_timer);
        fibril_timer_destroy(pauk_ui->hover_timer); 
        pauk_ui->hover_timer = NULL;
    }
}


// tajmer poruka // Status timer callback - clears status bar message
void status_timer_callback(void *arg) {
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    if (pauk_ui && pauk_ui->status_label) {
        ui_label_set_text(pauk_ui->status_label, "");
        ui_label_paint(pauk_ui->status_label);
        gfx_update(pauk_ui->gc);
    }
}

void show_status_message(pauk_ui_t* pauk_ui, const char* message, int duration_ms) {
    if (!pauk_ui || !pauk_ui->status_label) return;

    // Stop existing timer
    if (pauk_ui->status_timer) {
        fibril_timer_clear(pauk_ui->status_timer);
    }
    
    // Show message
    ui_label_set_text(pauk_ui->status_label, message);
    ui_label_paint(pauk_ui->status_label);
    gfx_update(pauk_ui->gc);
    
    // Start new timer
    if (duration_ms > 0) {
        if (!pauk_ui->status_timer) {
            pauk_ui->status_timer = fibril_timer_create(NULL);
        }
        
        fibril_timer_set(pauk_ui->status_timer, 
                        duration_ms * 1000,  // Convert to microseconds
                        status_timer_callback, 
                        pauk_ui);
    }
}

void stop_status_timer(pauk_ui_t *pauk_ui) {
    if (pauk_ui->status_timer) {
        fibril_timer_clear(pauk_ui->status_timer);
        pauk_ui->status_timer = NULL;
    }
}

/** Scrollbar up button pressed (line up) */
static void scrollbar_up(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    // Get current position
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    
    // Move up by step
    gfx_coord_t new_pos = pos - pauk_ui->scroll_step;
    if (new_pos < 0) new_pos = 0;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    
    // Update scroll position
    pauk_ui->scroll_y = new_pos;
    pixelmap_to_bitmap_copy(pauk_ui);
        gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    // Force redraw
    gfx_update(global_pauk_ui->gc);
}

/** Scrollbar down button pressed (line down) */
static void scrollbar_down(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos + pauk_ui->scroll_step;
    
    // Get max scroll (content height - view height)
    gfx_coord_t max_scroll = pauk_ui->content_height - 
                             (pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y);
    
    if (new_pos > max_scroll) new_pos = max_scroll;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(global_pauk_ui->gc);
}

/** Page up */
static void scrollbar_page_up(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos - pauk_ui->page_step;
    if (new_pos < 0) new_pos = 0;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(global_pauk_ui->gc);
}

/** Page down */
static void scrollbar_page_down(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos + pauk_ui->page_step;
    
    gfx_coord_t max_scroll = pauk_ui->content_height - 
                             (pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y);
    
    if (new_pos > max_scroll) new_pos = max_scroll;
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(global_pauk_ui->gc);
}

/** Scrollbar thumb moved (dragging) */
static void scrollbar_moved(ui_scrollbar_t *scrollbar, void *arg, gfx_coord_t pos)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    // Convert scrollbar units to content pixels
    pauk_ui->scroll_y = (int)(pos * pauk_ui->pixels_per_scroll_unit);
    
    // Redraw at new position
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(global_pauk_ui->gc);
}

//--------------------------------------------
// Scrollbar callbacks structure
static ui_scrollbar_cb_t scrollbar_cb = {
    .up = scrollbar_up,
    .down = scrollbar_down,
    .page_up = scrollbar_page_up,
    .page_down = scrollbar_page_down,
    .moved = scrollbar_moved
    
};

void wnd_pos_event(ui_window_t *window, void *arg, pos_event_t *event) {
    ui_window_def_pos(window, event);

    // ========== SIMPLE BOUNDS CHECK ==========
    // Get tab content area
    int tab_x = global_pauk_ui->tab_rect_base.p0.x;
    int tab_y = global_pauk_ui->tab_rect_base.p0.y;
    int tab_w = global_pauk_ui->tab_rect_base.p1.x - tab_x - 25;
    int tab_h = global_pauk_ui->tab_rect_base.p1.y - tab_y;

    if (event->hpos < (sysarg_t)tab_x || event->hpos >= (sysarg_t)tab_x + tab_w ||
        event->vpos < (sysarg_t)tab_y || event->vpos >= (sysarg_t)tab_y + tab_h) {
        return;
    }

    if (!global_pauk_ui->cursor_get_pos) {
        global_pauk_ui->cursor_move = 0;
        global_pauk_ui->cursor_get_pos = 1;
    }
    global_pauk_ui->mouse_pos.x = event->hpos;
    global_pauk_ui->mouse_pos.y = event->vpos;

    if (event->type == POS_RELEASE) {
        int browser_x = global_pauk_ui->tab_rect_base.p0.x;
        int browser_y = global_pauk_ui->tab_rect_base.p0.y;
        
        int adjusted_x = event->hpos - browser_x - 27;
        int adjusted_y = event->vpos - browser_y - 42 + global_pauk_ui->scroll_y;
        
        if (global_pauk_ui->rendering_json) {
            cJSON* clicked = find_element_at_position(
                global_pauk_ui->rendering_json,
                adjusted_x, 
                adjusted_y
            );
            
            // ===== HANDLE FOCUS =====
            if (clicked) {
                cJSON* form_element = clicked;
                const char* tag = get_json_string(form_element, "tag", "");
                
                // If clicked on a text node, find its parent
                if (strcmp(tag, "text") == 0) {
                    int parent_id = get_json_number(form_element, "parent_id", -1);
                    cJSON* parent = find_element_by_id(global_pauk_ui->rendering_json, parent_id);
                    if (parent) {
                        form_element = parent;
                        tag = get_json_string(form_element, "tag", "");
                    }
                }
                
                int is_input = get_json_bool(form_element, "is_input", 0);
                int is_textarea = get_json_bool(form_element, "is_textarea", 0);
                
                if (is_input || is_textarea) {
                    // Store the actual form element (with correct x,y)
                    global_pauk_ui->focused_element = form_element;
                    global_pauk_ui->cursor_position = strlen(get_json_string(form_element, "value", ""));
                    printf("✅ Focus set on %s at x=%d, y=%d\n", 
                           tag,
                           get_json_number(form_element, "x", -1),
                           get_json_number(form_element, "y", -1));
                } else {
                    global_pauk_ui->focused_element = NULL;
                }
                
                handle_element_click(global_pauk_ui, clicked, event->btn_num);
            } else {
                // Click on empty area - clear focus
                global_pauk_ui->focused_element = NULL;
            }
            // =======================
        }
    }
}


static ui_window_cb_t window_cb = {
    .close = wnd_close,
    .kbd = handle_keyboard_event,
    .pos = wnd_pos_event
};


static void handle_keyboard_event(ui_window_t *window, void *arg, kbd_event_t *event)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;

    if (event->type == KEY_PRESS) {

      //  printf("⌨️ Key pressed: key=%d, mods=%d, c='%c'\n", event->key, event->mods, event->c);

        // ===== GLOBAL HOTKEYS (always work) =====
        if (event->key == KC_S && (event->mods & KM_ALT)) {
            printf("💾 ALT+S pressed - saving debug snapshot\n");
            save_debug_snapshot(pauk_ui);
            return;
        }
        
// ===== MOUSE WHEEL / ARROW KEYS FOR SCROLLING =====
if (event->key == KC_UP || event->key == 84) {  // Scroll up
    int view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
    int max_scroll = pauk_ui->content_height - view_height;
    
    // Use 3x larger step for keyboard/mouse wheel
    int step = pauk_ui->scroll_step * 3;
    if (step < 30) step = 30;  // Minimum 30px
    
    // Scroll up by step
    pauk_ui->scroll_y -= step;
    if (pauk_ui->scroll_y < 0) pauk_ui->scroll_y = 0;
    
    // Update scrollbar position
    if (pauk_ui->vscrollbar && max_scroll > 0) {
        float ratio = (float)pauk_ui->scroll_y / max_scroll;
        gfx_coord_t scrollbar_pos = (gfx_coord_t)(ratio * ui_scrollbar_move_length(pauk_ui->vscrollbar));
        ui_scrollbar_set_pos(pauk_ui->vscrollbar, scrollbar_pos);
    }
    
    // Update display
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(pauk_ui->gc);
    return;
}

if (event->key == KC_DOWN || event->key == 85) {  // Scroll down
    int view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
    int max_scroll = pauk_ui->content_height - view_height;
    
    // Use 3x larger step for keyboard/mouse wheel
    int step = pauk_ui->scroll_step * 3;
    if (step < 30) step = 30;  // Minimum 30px
    
    // Scroll down by step
    pauk_ui->scroll_y += step;
    if (pauk_ui->scroll_y > max_scroll) pauk_ui->scroll_y = max_scroll;
    
    // Update scrollbar position
    if (pauk_ui->vscrollbar && max_scroll > 0) {
        float ratio = (float)pauk_ui->scroll_y / max_scroll;
        gfx_coord_t scrollbar_pos = (gfx_coord_t)(ratio * ui_scrollbar_move_length(pauk_ui->vscrollbar));
        ui_scrollbar_set_pos(pauk_ui->vscrollbar, scrollbar_pos);
    }
    
    // Update display
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, &pauk_ui->list_rect, NULL);
    gfx_update(pauk_ui->gc);
    return;
}

        
        // ===== CHECK FOR FOCUSED FORM ELEMENT =====
        if (pauk_ui->focused_element) {
            int is_input = get_json_bool(pauk_ui->focused_element, "is_input", 0);
            int is_textarea = get_json_bool(pauk_ui->focused_element, "is_textarea", 0);
            
            if (is_input || is_textarea) {
                const char *current = get_json_string(pauk_ui->focused_element, "value", "");
                char *new_value = malloc(strlen(current) + 2);
                if (!new_value) return;
                
                strcpy(new_value, current);
                int modified = 0;
                
                // Get textarea dimensions for limits
             //   int textarea_width = 0;
                int textarea_rows = 0;
                if (is_textarea) {
                //    int textarea_width = 0;
                    cJSON* parent_elem = pauk_ui->focused_element;
                    const char* tag = get_json_string(parent_elem, "tag", "");
                    if (strcmp(tag, "text") == 0) {
                        int parent_id = get_json_number(parent_elem, "parent_id", -1);
                        parent_elem = find_element_by_id(pauk_ui->rendering_json, parent_id);
                    }
               //    int textarea_width = get_json_number(parent_elem, "width", 400);
                    textarea_rows = get_json_number(pauk_ui->focused_element, "textarea_rows", 4);
                }
                
                // Handle ENTER in textarea (BEFORE navigation switch)
                if ((event->key == KC_ENTER || event->key == KC_NENTER) && is_textarea) {
                    // Count current lines
                    int line_count = 1;
                    for (int i = 0; new_value[i]; i++) {
                        if (new_value[i] == '\n') line_count++;
                    }
                    
                    // Check if adding newline would exceed height
                    if (line_count + 1 > textarea_rows) {
                        printf("⚠️ Cannot add newline - textarea full (max %d lines)\n", textarea_rows);
                        modified = 0;
                    } else {
                        // Add newline
                        memmove(&new_value[pauk_ui->cursor_position + 1], 
                                &new_value[pauk_ui->cursor_position], 
                                strlen(new_value) - pauk_ui->cursor_position + 1);
                        new_value[pauk_ui->cursor_position] = '\n';
                        pauk_ui->cursor_position++;
                        modified = 1;
                        printf("📝 Enter pressed - added newline at position %d\n", pauk_ui->cursor_position);
                    }
                }
                // =========================================================================
                // 🚀 SUBMIT FORME NA ENTER ZA INPUT POLJA (USAGLAŠENO SA TVOJIM REŠENJEM)
                // Aktivira se samo ako smo u input polju i koristi find_parent_form
                // =========================================================================
                else if ((event->key == KC_ENTER || event->key == KC_NENTER) && is_input) {
                    printf("🔍 [Keyboard Router] Enter pritisnut u inputu! Tražim roditeljsku formu...\n");
                    
                    // Deklaracija i poziv tvoje funkcije iz render_func.c
                    extern cJSON* find_parent_form(cJSON* root, cJSON* element);
                    cJSON *roditeljska_forma = find_parent_form(pauk_ui->rendering_json, pauk_ui->focused_element);
                    
                    if (roditeljska_forma) {
                        printf("✅ [Keyboard Router] Forma pronađena. Pokrećem tvoj submit_form...\n");
                        
                        // Oslobađamo bafer pre prevremenog izlaza da sprečimo curenje memorije
                        free(new_value);
                        
                        // Deklaracija i aktivacija tvog submit_form procesora
                        extern void submit_form(pauk_ui_t* pauk_ui, cJSON* form);
                        submit_form(pauk_ui, roditeljska_forma);
                        
                        return; // Završeno, prekidamo dalju obradu ovog tastera!
                    } else {
                        printf("⚠️ [Keyboard Router] Ovaj input nema roditeljsku formu u JSON stablu.\n");
                    }
                }
                // Handle backspace
                else if (event->key == KC_BACKSPACE) {
                    if (pauk_ui->cursor_position > 0) {
                        memmove(&new_value[pauk_ui->cursor_position - 1], 
                                &new_value[pauk_ui->cursor_position], 
                                strlen(new_value) - pauk_ui->cursor_position + 1);
                        pauk_ui->cursor_position--;
                        modified = 1;
                    }
                }
                // Handle delete
                else if (event->key == KC_DELETE) {
                    if (pauk_ui->cursor_position < (int)strlen(current)) {
                        memmove(&new_value[pauk_ui->cursor_position], 
                                &new_value[pauk_ui->cursor_position + 1], 
                                strlen(new_value) - pauk_ui->cursor_position);
                        modified = 1;
                    }
                }
                // Handle regular characters
// Handle regular characters
else if (event->c >= 32 && event->c <= 126) {
    if (is_textarea) {
        // Get textarea width
        cJSON* parent_elem = pauk_ui->focused_element;
        const char* tag = get_json_string(parent_elem, "tag", "");
        if (strcmp(tag, "text") == 0) {
            int parent_id = get_json_number(parent_elem, "parent_id", -1);
            parent_elem = find_element_by_id(pauk_ui->rendering_json, parent_id);
        }
        int textarea_width = get_json_number(parent_elem, "width", 400);
        int textarea_rows = get_json_number(pauk_ui->focused_element, "textarea_rows", 4);
        
        // Calculate current line text
        int line_start = 0;
        for (int i = pauk_ui->cursor_position - 1; i >= 0; i--) {
            if (new_value[i] == '\n') {
                line_start = i + 1;
                break;
            }
        }
        
        char current_line[256];
        int line_len = pauk_ui->cursor_position - line_start;
        strncpy(current_line, new_value + line_start, line_len);
        current_line[line_len] = '\0';
        
        // Measure actual width with new character
        char test_line[512];
        snprintf(test_line, sizeof(test_line), "%s%c", current_line, event->c);
        int new_width = estimate_text_width(test_line, DEFAULT_FONT_SIZE, "normal", "normal");
        
        // Count current lines
        int line_count = 1;
        for (int i = 0; new_value[i]; i++) {
            if (new_value[i] == '\n') line_count++;
        }
        
        // Use 5px margin on each side (total 10px)
        int max_width = textarea_width - 10;
        
        if (new_width > max_width) {
            // Would exceed width - add newline
            if (line_count + 1 > textarea_rows) {
                printf("⚠️ Textarea full\n");
                modified = 0;
            } else {
                memmove(&new_value[pauk_ui->cursor_position + 1], 
                        &new_value[pauk_ui->cursor_position], 
                        strlen(new_value) - pauk_ui->cursor_position + 1);
                new_value[pauk_ui->cursor_position] = '\n';
                pauk_ui->cursor_position++;
                modified = 1;
            }
        } else {
            // Check height limit
            if (line_count > textarea_rows && pauk_ui->cursor_position == (int)strlen(current)) {
                printf("⚠️ Textarea full\n");
                modified = 0;
            } else {
                memmove(&new_value[pauk_ui->cursor_position + 1], 
                        &new_value[pauk_ui->cursor_position], 
                        strlen(new_value) - pauk_ui->cursor_position + 1);
                new_value[pauk_ui->cursor_position] = event->c;
                pauk_ui->cursor_position++;
                modified = 1;
            }
        }
    } else {
        // Input field
        memmove(&new_value[pauk_ui->cursor_position + 1], 
                &new_value[pauk_ui->cursor_position], 
                strlen(new_value) - pauk_ui->cursor_position + 1);
        new_value[pauk_ui->cursor_position] = event->c;
        pauk_ui->cursor_position++;
        modified = 1;
    }
}
                // Handle left arrow
                else if (event->key == KC_LEFT) {
                    if (pauk_ui->cursor_position > 0) {
                        pauk_ui->cursor_position--;
                        modified = 1;
                    }
                }
                // Handle right arrow
                else if (event->key == KC_RIGHT) {
                    if (pauk_ui->cursor_position < (int)strlen(current)) {
                        pauk_ui->cursor_position++;
                        modified = 1;
                    }
                }
                
                if (modified) {
                    // Save to JSON
                    set_json_string(pauk_ui->focused_element, "value", new_value);
                    set_json_string(pauk_ui->focused_element, "input_value", new_value);
                    
                    // Find parent element for coordinates
                    cJSON* render_element = pauk_ui->focused_element;
                    const char* tag = get_json_string(render_element, "tag", "");
                    if (strcmp(tag, "text") == 0) {
                        int parent_id = get_json_number(render_element, "parent_id", -1);
                        render_element = find_element_by_id(pauk_ui->rendering_json, parent_id);
                    }
                    
                    // Get coordinates
                    int abs_x = get_json_number(render_element, "x", 0) + 40;
                    int abs_y = get_json_number(render_element, "y", 0) + 40;
                    int width = get_json_number(render_element, "width", 0);
                    int height = get_json_number(render_element, "height", 0);
                    
                    // Get font
                    html_font_t *font = font_manager_get_font(&pauk_ui->font_manager, 
                                                               pauk_ui->font_manager.default_font_index);
                    
                    const char *new_text = get_json_string(pauk_ui->focused_element, 
                                                           is_textarea ? "value" : "input_value", "");
                    
                    // ===== ONLY REDRAW TEXT AREA (PRESERVE BORDER) =====
                    if (is_textarea) {
                        // Clear only the text area (inside the border)
                        draw_filled_box_to_pixelmap(pauk_ui, abs_x + 2, abs_y + 2, width - 4, height - 4, 0xFFFFFFFF);
                        
                        // Draw multi-line text
                        int line_height = DEFAULT_FONT_SIZE + 4;
                        int max_lines = get_json_number(pauk_ui->focused_element, "textarea_rows", 4);
                        int start_y = abs_y + (DEFAULT_FONT_SIZE / 2);
                        
                        char *text_copy = strdup(new_text);
                        if (text_copy) {
                            int current_y = start_y;
                            int line_count = 0;
                            char *line = strtok(text_copy, "\n");
                            
                            while (line && line_count < max_lines) {
                                render_ttf_text_to_pixelmap(pauk_ui, line, abs_x + 10, current_y, 
                                                            font, DEFAULT_FONT_SIZE, 0xFF000000, 0, 0);
                                current_y += line_height;
                                line = strtok(NULL, "\n");
                                line_count++;
                            }
                            free(text_copy);
                        }
                    } else if (is_input) {
                        // Clear only the text area
                        draw_filled_box_to_pixelmap(pauk_ui, abs_x + 2, abs_y + 2, width - 4, height - 4, 0xFFFFFFFF);
                        
                        // Draw single line text
                        int text_y = abs_y + (DEFAULT_FONT_SIZE/2);
                        render_ttf_text_to_pixelmap(pauk_ui, new_text, abs_x + 10, text_y, 
                                                    font, DEFAULT_FONT_SIZE, 0xFF000000, 0, 0);
                    }
                    
                    // Force screen update
                    pixelmap_to_bitmap_copy(pauk_ui);
                    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                                      &pauk_ui->list_rect, NULL);
                    gfx_update(pauk_ui->gc);
                    
                    free(new_value);
                    return;
                }
                
                free(new_value);
                return;
            }
        }
        
        // ===== NO FOCUSED FORM ELEMENT - handle browser navigation =====
        switch (event->key) {
            case KC_ENTER:
            case KC_NENTER:
                if (ui_entry_get_text(pauk_ui->search_entry) && 
                    str_length(ui_entry_get_text(pauk_ui->search_entry)) > 0) {
                    printf("Enter pressed in search field - triggering search\n");
                } else {
                    printf("Enter key pressed - triggering Go button\n");
                    go_button_clicked(pauk_ui->go_button, pauk_ui);
                }
                break;
                
            case KC_LEFT:
                printf("Left arrow pressed - going back\n");
                navigate_back(NULL, pauk_ui);
                break;
                
            case KC_RIGHT:
                printf("Right arrow pressed - going forward\n");
                break;
                
            case KC_R:
                if (event->mods & KM_CTRL) {
                    printf("Ctrl+R pressed - reloading page\n");
                    go_button_clicked(pauk_ui->go_button, pauk_ui);
                } else {
                    ui_window_def_kbd(window, event);
                }
                break;
                
            default:
                ui_window_def_kbd(window, event);
                break;
        }
    }
}


// Round function if not available
float roundf(float value) {
    return (float)(value < 0.0f ? (int)(value - 0.5f) : (int)(value + 0.5f));
}

// Callback for window close
void wnd_close(ui_window_t *window, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    ui_quit(pauk_ui->ui);
}

// Callback for Bookmark button
void bookmark_button_clicked(ui_pbutton_t *pbutton, void *arg)
{
return;

}

#include "url_utils.h"

void go_button_clicked(ui_pbutton_t *pbutton, void *arg) {
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;

    // Get URL from address bar
    const char *url_raw = ui_entry_get_text(pauk_ui->address_entry);

    printf("=== GO BUTTON CLICKED ===\n");
    printf("Raw address: %s\n", url_raw);

    if (!url_raw || url_raw[0] == '\0') {
        show_status_message(pauk_ui, "Please enter a URL", 3000);
        return;
    }
  
    // ===== DETECT LOCAL FILE =====
    int is_local_file = 0;
    char *url = NULL;
    
    // Check for localhost:// protocol
    if (strstr(url_raw, "localhost://") == url_raw) {
        is_local_file = 1;
        // Remove localhost:// prefix
        url = strdup(url_raw + 12);  // Skip "localhost://"
        printf("📁 Local file via localhost://: %s\n", url);
    }
    // Check if it's a local file path (starts with / or ./ or ../ or has .html/.htm extension without protocol)
    else if (url_raw[0] == '/' || 
             (url_raw[0] == '.' && (url_raw[1] == '/' || url_raw[1] == '.')) ||
             (strstr(url_raw, ".html") != NULL && strstr(url_raw, "://") == NULL) ||
             (strstr(url_raw, ".htm") != NULL && strstr(url_raw, "://") == NULL)) {
        is_local_file = 1;
        url = strdup(url_raw);
        printf("📁 Local file detected: %s\n", url);
    }
    // Check if it has a network protocol
    else if (strstr(url_raw, "http://") == url_raw || strstr(url_raw, "https://") == url_raw) {
        is_local_file = 0;
        url = strdup(url_raw);
        printf("🌐 Network URL: %s\n", url);
    }
    // No protocol - check if it looks like a local file or network address
    else if (strstr(url_raw, "://") == NULL) {
        // Check if it contains a dot (likely a domain like example.com) or is an IP
        if ((strchr(url_raw, '.') != NULL && strchr(url_raw, '/') == NULL) ||
            (url_raw[0] >= '0' && url_raw[0] <= '9')) {
            // Looks like a domain or IP - treat as network
            is_local_file = 0;
            asprintf(&url, "http://%s", url_raw);
            printf("🌐 Network URL (added http://): %s\n", url);
        } else {
            // Looks like a local file path - treat as local
            is_local_file = 1;
            url = strdup(url_raw);
            printf("📁 Local file (no protocol): %s\n", url);
        }
    }
    // Malformed protocol (http:/ or https:/)
    else if ((strstr(url_raw, "http:/") != NULL && strstr(url_raw, "http://") == NULL) ||
             (strstr(url_raw, "https:/") != NULL && strstr(url_raw, "https://") == NULL)) {
        is_local_file = 0;
        if (strstr(url_raw, "https:/") != NULL) {
            asprintf(&url, "https://%s", url_raw + 7);
        } else {
            asprintf(&url, "http://%s", url_raw + 6);
        }
        printf("🌐 Fixed malformed URL: %s\n", url);
    }
    else {
        // Default to network
        is_local_file = 0;
        url = strdup(url_raw);
    }
    
    if (!url) {
        show_status_message(pauk_ui, "Invalid URL", 3000);
        return;
    }
    
        // =========================================================================
    // 🚀 PAUK UI PAMETNI RUTER: ON-DEMAND SWITCH REFORMATIRANJE URL-A
    // Presreće adresu i menja je u memoriji pre slanja mrežnog zahteva
    // =========================================================================
    if (!is_local_file && url != NULL) {
        char temp_hostname[256] = "";
        const char *proto_end = strstr(url, "://");
        
        if (proto_end) {
            const char *host_start = proto_end + 3;
            const char *host_end = strchr(host_start, '/');
            int h_len = host_end ? (host_end - host_start) : (int)strlen(host_start);
            
            if (h_len > 255) h_len = 255;
            strncpy(temp_hostname, host_start, h_len);
            temp_hostname[h_len] = '\0';
            
            char *colon_ptr = strchr(temp_hostname, ':');
            if (colon_ptr) *colon_ptr = '\0';
        }

        char cisti_domen[256];
        strcpy(cisti_domen, temp_hostname);
        if (strncasecmp(temp_hostname, "www.", 4) == 0) {
            strcpy(cisti_domen, temp_hostname + 4);
        }
        
        // Dodela ID-ja za brzi switch ruter
        int ruter_sajt_id = 0;
        if (strstr(cisti_domen, "google.") != NULL) {
            if (strstr(url, "/search") != NULL || strstr(url, "?q=") != NULL) {
                ruter_sajt_id = 5;
            } else {
                ruter_sajt_id = 1;
            }
        } else if (strstr(cisti_domen, "yahoo.") != NULL) {
            ruter_sajt_id = 3;
        } else if (strstr(cisti_domen, "duckduckgo.") != NULL) {
            if (strchr(url, '?') != NULL) {
                ruter_sajt_id = 6;
            } else {
                ruter_sajt_id = 2;
            }
        } else if (strstr(cisti_domen, "bing.") != NULL) {
            ruter_sajt_id = 4;
        } else if (strstr(cisti_domen, "mojeek.") != NULL) {
            ruter_sajt_id = 7;  // Mojeek
        }
        
        char protocol[16] = "https";
        if (proto_end) {
            size_t p_len = proto_end - url;
            if (p_len < sizeof(protocol)) {
                memcpy(protocol, url, p_len);
                protocol[p_len] = '\0';
            }
        }
        
        const char *path_start = proto_end ? strchr(proto_end + 3, '/') : "/";
        if (!path_start) path_start = "/";

        char *novo_sklopljeni_url = NULL;

        switch (ruter_sajt_id) {
            case 1:
                // LISTA 1: Google i Yahoo MORAJU imati 'www.' ispred domena
                if (strncasecmp(temp_hostname, "www.", 4) != 0) {
                    printf("⚙️ [UI Router] Google/Yahoo detektovan bez www. Pokrećem auto-upis...\n");
                    asprintf(&novo_sklopljeni_url, "%s://www.%s%s", protocol, temp_hostname, path_start);
                }
                break;

            case 3:
                // LISTA 3: DuckDuckGo NE SME imati 'www.' ispred domena radi ikonica
                if (strncasecmp(temp_hostname, "www.", 4) == 0) {
                    printf("⚙️ [UI Router] DuckDuckGo detektovan sa www. Skidam višak radi učitavanja ikonica...\n");
                    asprintf(&novo_sklopljeni_url, "%s://%s%s", protocol, cisti_domen, path_start);
                }
                break;

                case 4:
                // LISTA 4 (Bing): Samo osiguravamo 'www.' na samom ulazu u UI
                if (strncasecmp(temp_hostname, "www.", 4) != 0) {
                    printf("⚙️ [UI Router] Bing detektovan bez www. Dodajem poddomen...\n");
                    asprintf(&novo_sklopljeni_url, "%s://www.%s%s", protocol, temp_hostname, path_start);
                }
                break;

            default:
                break;
        }

        if (novo_sklopljeni_url) {
            free(url);
            url = novo_sklopljeni_url; 
            printf("🌐 [UI Router] URL uspešno i konačno normalizovan na: %s\n", url);
        }
    }
    // =========================================================================

    // Update address bar with normalized URL
    ui_entry_set_text(pauk_ui->address_entry, url);
    pauk_ui->current_address = strdup(url);
    if (!pauk_ui->navigating) {
        add_to_history(pauk_ui, url);
    }
    // Update status
    show_status_message(pauk_ui, "Ucitavam stranu...", 1000);

    if (is_local_file) {
        // ========== LOCAL FILE ==========
        printf("📁 Loading local file: %s\n", url);
        
        // Check if file exists
        FILE *test = fopen(url, "r");
        if (test) {
            fclose(test);
            load_and_render_page(url,NULL);
            show_status_message(pauk_ui, "Strana je ucitana!", 2000);
        } else {
            char *msg = NULL;
            asprintf(&msg, "Fajl nije pronadjen: %s", url);
            if (msg) {
                show_status_message(pauk_ui, msg, 3000);
                free(msg);
            } else {
                show_status_message(pauk_ui, "Fajl nije pronadjen", 3000);
            }
        }
    } else {
        // ========== NETWORK PAGE ==========
        printf("🌐 Network URL detected: %s\n", url);
        
        // Parse URL to get host and port
        int is_https = (strstr(url, "https://") != NULL);
        uint16_t port = is_https ? 443 : 80;
        
        // Extract hostname
        char hostname[256];
        const char *host_start = strstr(url, "://");
        if (host_start) {
            host_start += 3;
            const char *host_end = strchr(host_start, '/');
            if (host_end) {
                int len = host_end - host_start;
                if (len > 255) len = 255;
                strncpy(hostname, host_start, len);
                hostname[len] = '\0';
            } else {
                strncpy(hostname, host_start, 255);
                hostname[255] = '\0';
            }

            // PROVERA I IZDVAJANJE PORTA IZ HOSTNAME-A (npr. :80)
            char *port_ptr = strchr(hostname, ':');
            if (port_ptr) {
                *port_ptr = '\0'; // Secemo hostname na dvotacki
                port = (uint16_t)strtoul(port_ptr + 1, NULL, 10);
                // Ponovo proveravamo is_https na osnovu novog porta
                is_https = (port == 443); 
                printf("⚠️ Port iz URL-a: Host=%s, Port=%d, HTTPS=%d\n", 
                       hostname, port, is_https);
            }

            printf("Host: %s, Port: %d\n", hostname, port);
        } else {
            show_status_message(pauk_ui, "Neispravan URL format", 3000);
            free(url);
            pauk_ui->current_address = NULL;
            return;
        }

        
        // Create TCP connection
        inet_addr_t addr;
        errno_t rc = resolve_host(hostname, &addr);
        if (rc != EOK) {
            char *msg = NULL;
            asprintf(&msg, "Failed to resolve: %s", hostname);
            if (msg) {
                show_status_message(pauk_ui, msg, 3000);
                free(msg);
            } else {
                show_status_message(pauk_ui, "Ne mogu da odredim Host", 3000);
            }
            pauk_ui->current_address = NULL;
            free(url);
            return;
        }
        
        tcp_t *tcp = NULL;
        tcp_conn_t *conn = NULL;
        rc = create_tcp_connection(addr, port, &tcp, &conn);
        if (rc != EOK) {
            show_status_message(pauk_ui, "Konekcija pukla", 3000);
            free(url);
            return;
        }
        
        char *content = NULL;
        size_t content_size = 0;
        
        if (is_https) {
            rc = fetch_https_content(url, conn, &content, &content_size, 0);
        } else {
            rc = fetch_http_content(url, conn, &content, &content_size, 0);
        }
        
        tcp_conn_destroy(conn);
        tcp_destroy(tcp);
        
        if (rc == EOK && content != NULL && content_size > 0) {
            // Save to temp file
            char temp_file[] = "/tmp/pauk_page.html";
         
            FILE *f = fopen(temp_file, "w");
            if (f) {
                fwrite(content, 1, content_size, f);
                fclose(f);
                kopiraj_fajl(temp_file);        
                // Load the page from temp file
                load_and_render_page(temp_file, url);
                unlink(temp_file);
                show_status_message(pauk_ui, "Strana je ucitana", 2000);
            } else {
                show_status_message(pauk_ui, "Ne mogu da sacuvam stranu", 3000);
            }
            free(content);
        }  else {
            // =========================================================================
            // 🚨 SIKURNOSNI UI ŠTIT ZA GREŠKE U KONEKCIJI (POPRAVLJENO)
            // Hvata bilo koji SSL/TLS pad (uključujući Bing) i ispisuje tvoju poruku!
            // =========================================================================
            printf("❌ [Network Error] Konekcija ili Handshake propao sa kodom: %d\n", rc);
            
            // Ispisujemo tvoju tačnu poruku na status bar pretraživača
            show_status_message(pauk_ui, "Strana NIJE podrzana!", 6000);
            
            // Čistimo memoriju adrese da se sistem ne bi zaglavio
            pauk_ui->current_address = NULL;
            if (url) {
                free(url);
                url = NULL;
            }
            return; // Prekidamo izvršavanje, prozor ostaje bezbedno otvoren i aktivan!
        }
    }
    
    free(url);
    
    // Force update
    gfx_update(pauk_ui->gc);
    ui_window_paint(pauk_ui->window);
}

// Bookmark click handler
void bookmark_clicked(ui_menu_entry_t *mentry, void *arg) {
    int index = (int)(intptr_t)arg;
    
    // Remove the NULL check for the fixed array
    if (index >= 0 && index < bookmark_count) {
        // Set the URL in the address bar
        ui_entry_set_text(global_pauk_ui->address_entry, bookmarks[index].url);
        
        // Trigger the Go button click
        go_button_clicked(global_pauk_ui->go_button, global_pauk_ui);
    }
}

void navigate_back(ui_pbutton_t *pbutton, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    if (pauk_ui->history_current > 0) {
        pauk_ui->navigating = 1; 
        pauk_ui->history_current--;
        const char *prev_url = pauk_ui->history[pauk_ui->history_current];
        ui_entry_set_text(pauk_ui->address_entry, prev_url);
        go_button_clicked(pauk_ui->go_button, pauk_ui);
    }
}

// Callback for Refresh button
void refresh_page(ui_pbutton_t *pbutton, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    printf("Refresh page requested\n");
    go_button_clicked(pauk_ui->go_button, pauk_ui);
}

// Callback for Forward button
void navigate_forward(ui_pbutton_t *pbutton, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    if (pauk_ui->history_current < pauk_ui->history_count - 1) {
        pauk_ui->history_current++;
        const char *next_url = pauk_ui->history[pauk_ui->history_current];
        ui_entry_set_text(pauk_ui->address_entry, next_url);
        go_button_clicked(pauk_ui->go_button, pauk_ui);
    }
}


void render_multiline_text(pauk_ui_t *pauk_ui, const char *text, int x, int y, 
    html_font_t *font, int font_size, uint32_t color,
    int line_height) {
if (!text || !font) return;

char *text_copy = strdup(text);
if (!text_copy) return;

int current_y = y;
char *line = strtok(text_copy, "\n");

while (line) {
// Render this line
render_ttf_text_to_pixelmap(pauk_ui, line, x, current_y, 
             font, font_size, color, 0, 0);

// Move to next line
current_y += line_height;
line = strtok(NULL, "\n");
}

free(text_copy);
}

// Save function
void save_debug_snapshot(pauk_ui_t *pauk_ui) {
    if (!pauk_ui->rendering_json) return;
    
    const char *filename = "debug_output.txt";
    
    FILE *f = fopen(filename, "wb");
    if (f) {
        char *json_str = cJSON_Print(pauk_ui->rendering_json);
        if (json_str) {
            fputs(json_str, f);
            free(json_str);
            printf("✅ Debug snapshot saved: %s\n", filename);
            

        } else {
            printf("❌ Failed to print output_debug.html file\n");
        }
        fclose(f);
                   // Optional: Copy to server
                   kopiraj_fajl(filename);
    } else {
        printf("❌ Failed to open %s for writing\n", filename);
    }
     
}

errno_t init_ui(pauk_ui_t *pauk_ui, const char *display_spec)
{
    
    ui_wnd_params_t params;
    ui_menu_entry_t *mexit;
    ui_menu_entry_t *mabout;
 
    pauk_ui->focused_element = NULL;
    pauk_ui->cursor_position = 0;

    gfx_rect_t rect;
errno_t rc;
if (DEB_INIT){
    printf("=== UI INIT START ===\n");
}

    //Deo vezan za promenu velicina objekata kad je full screen
pauk_ui->full_screen_status = 0;
pauk_ui->globX = 0;
pauk_ui->globY = 0;
   
    rc = ui_create(display_spec, &pauk_ui->ui);
    if (rc != EOK) {
        if (DEB_WARNING){
        printf("Error creating UI on display %s.\n", display_spec);
                 }
        return rc;
    }
   
    ui_wnd_params_init(&params);
    params.caption = "Pauk Web Pretrazivac";
    params.style |=  ui_wds_resizable | ui_wds_minimize_btn;
    params.min_size.x = 800;
    params.min_size.y = 600;

    // Set window size
    if (ui_is_textmode(pauk_ui->ui)) {
        params.rect.p0.x = 0;
        params.rect.p0.y = 0;
        params.rect.p1.x = 80;
        params.rect.p1.y = 25;
    } else {
        params.rect.p0.x = 0;
        params.rect.p0.y = 0;
        params.rect.p1.x = 1030;   // Width slightly bigger than list 1024px
        params.rect.p1.y = 600;
    }


    // Only allow making the window larger
    gfx_rect_dims(&params.rect, &params.min_size);

    rc = ui_window_create(pauk_ui->ui, &params, &pauk_ui->window);
    if (rc != EOK) {
        if (DEB_WARNING){  printf("Error creating window.\n");}
        return rc;
    }
    ui_window_get_app_rect(pauk_ui->window, &pauk_ui->win_rect_base);

   init_navigation_history(pauk_ui);


    ui_window_set_cb(pauk_ui->window, &window_cb, (void *)pauk_ui);

    rc = ui_fixed_create(&pauk_ui->fixed);
    if (rc != EOK) {
        if (DEB_WARNING){  printf("Error creating fixed layout.\n");}
        return rc;
    }
    pauk_ui->gc = ui_window_get_gc(pauk_ui->window);
    global_pauk_ui = pauk_ui;
    // Create menu bar
    rc = ui_menu_bar_create(pauk_ui->ui, pauk_ui->window, &pauk_ui->mbar);
    if (rc != EOK) {
        if (DEB_WARNING){ printf("Error creating menu bar.\n");}
        return rc;
    }

    // File menu
    rc = ui_menu_dd_create(pauk_ui->mbar, "~F~ile", NULL, &pauk_ui->mfile);
    if (rc != EOK) {
        if (DEB_WARNING){ printf("Error creating menu.\n");}
        return rc;
    }

    rc = ui_menu_entry_create(pauk_ui->mfile, "E~x~it", "Alt-F4", &mexit);
    if (rc != EOK) {
        if (DEB_WARNING){  printf("Error creating menu entry.\n");}
        return rc;
    }
    ui_menu_entry_set_cb(mexit, file_exit, (void *)pauk_ui);

    // Help menu
    rc = ui_menu_dd_create(pauk_ui->mbar, "~H~elp", NULL, &pauk_ui->mhelp);
    if (rc != EOK) {
        if (DEB_WARNING){  printf("Error creating menu.\n");}
        return rc;
    }

    rc = ui_menu_entry_create(pauk_ui->mhelp, "~A~bout", "F1", &mabout);
    if (rc != EOK) {
        if (DEB_WARNING){ printf("Error creating menu entry.\n");}
        return rc;
    }
    ui_menu_entry_set_cb(mabout, help_about, (void *)pauk_ui);


// Load bookmarks from file
 rc = load_bookmarks();
if (rc != EOK) {
    if (DEB_WARNING){  printf("Failed to load bookmarks: %s\n", str_error(rc));}
    // Continue without bookmarks
}

// Bookmarks menu
rc = ui_menu_dd_create(pauk_ui->mbar, "~B~ookmarks", NULL, &pauk_ui->mbookmarks);
if (rc != EOK) {
    if (DEB_INIT_MENU){printf("Error creating bookmarks menu.\n");}
    return rc;
}
//printf("Bookmarks menu created successfully at address: %p\n", (void*)pauk_ui->mbookmarks);


if (bookmark_count > 0) {
    if (DEB_INIT_MENU){ printf("Number of bookmarks: %d\n", bookmark_count);}
    
    // Add bookmark entries to the menu
    for (int i = 0; i < bookmark_count; i++) {
        if (DEB_INIT_MENU){ printf("Creating menu entry for: %s\n", bookmarks[i].name);}
        
        ui_menu_entry_t *entry;
        // Use empty string instead of NULL for accelerator
        rc = ui_menu_entry_create(pauk_ui->mbookmarks, bookmarks[i].name, "", &entry);
        if (rc != EOK) {
            if (DEB_WARNING){ printf("Error creating bookmark entry: %s\n", str_error(rc));}
            continue;
        }
        
        // Pass the index as the callback argument
        ui_menu_entry_set_cb(entry, bookmark_clicked, (void *)(intptr_t)i);
        if (DEB_INIT_MENU){ printf("Added bookmark: %s\n", bookmarks[i].name);}
    }
} else {
    if (DEB_WARNING){ printf("No bookmarks to display\n");}
    
    // Add a "No bookmarks" entry
    ui_menu_entry_t *entry;
    rc = ui_menu_entry_create(pauk_ui->mbookmarks, "No bookmarks", "", &entry);
    if (rc != EOK) {
        if (DEB_INIT_MENU){printf("Error creating 'No bookmarks' entry: %s\n", str_error(rc));}
    }
}

   // Position menu bar
if (ui_is_textmode(pauk_ui->ui)) {
    rect.p0.x = 1;
    rect.p0.y = 1 + SYSTEM_MENU_HEIGHT;  // Shift down
    rect.p1.x = 79;
    rect.p1.y = 2 + SYSTEM_MENU_HEIGHT;  // Shift down
} else {
    rect.p0.x = 0;
    rect.p0.y = 0 + SYSTEM_MENU_HEIGHT;  // Shift down
    rect.p1.x = 1030;
    rect.p1.y = 25 + SYSTEM_MENU_HEIGHT;  // Shift down
}
   ui_menu_bar_set_rect(pauk_ui->mbar, &rect);
    rc = ui_fixed_add(pauk_ui->fixed, ui_menu_bar_ctl(pauk_ui->mbar));
    if (rc != EOK) return rc;

    pauk_ui->current_y1 = SYSTEM_MENU_HEIGHT + ROW_SPACING+20;
   
    // NAVIGACIJA DUGMAD!
static ui_pbutton_cb_t resize_button_cb = { .clicked = change_size }; 
static ui_pbutton_cb_t back_button_cb = { .clicked = navigate_back };
static ui_pbutton_cb_t refresh_button_cb = { .clicked = refresh_page };
static ui_pbutton_cb_t forward_button_cb = { .clicked = navigate_forward };


// Resize  button (<-)
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "Velicina", &pauk_ui->resize_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->resize_button, &resize_button_cb, (void *)pauk_ui);



// Back button (<-)
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "<-", &pauk_ui->back_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->back_button, &back_button_cb, (void *)pauk_ui);

// Refresh button (@)
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "@", &pauk_ui->refresh_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->refresh_button, &refresh_button_cb, (void *)pauk_ui);

// Forward button (->)
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "->", &pauk_ui->forward_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->forward_button, &forward_button_cb, (void *)pauk_ui);


// Navigation buttons (Back, Refresh, Forward)
rect.p0.x = CONTENT_MARGIN-5;
rect.p0.y = pauk_ui->current_y1;
rect.p1.x = CONTENT_MARGIN + 33;  // 30px width per button
rect.p1.y = pauk_ui->current_y1 + 25;
ui_pbutton_set_rect(pauk_ui->back_button, &rect);
pauk_ui->back_button_base_rect =rect;
rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->back_button));
if (rc != EOK) return rc;

rect.p0.x += 33;  // 5px spacing between buttons
rect.p1.x += 33;
ui_pbutton_set_rect(pauk_ui->refresh_button, &rect);
pauk_ui->refresh_button_base_rect =rect;
rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->refresh_button));
if (rc != EOK) return rc;

rect.p0.x += 33;
rect.p1.x += 30;
ui_pbutton_set_rect(pauk_ui->forward_button, &rect);
pauk_ui->forward_button_base_rect =rect;
rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->forward_button));
if (rc != EOK) return rc;
// Resize 

pauk_ui->resize_button_rect.p0.x = 1030 -90;
pauk_ui->resize_button_rect.p0.y = pauk_ui->current_y1-30;
pauk_ui->resize_button_rect.p1.x = 1030 - 25; // 30px width per button
pauk_ui->resize_button_rect.p1.y = pauk_ui->current_y1-5;
ui_pbutton_set_rect(pauk_ui->resize_button, &pauk_ui->resize_button_rect);



rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->resize_button));
if (rc != EOK) return rc;
// Add navigation buttons to fixed layout_GOTOVA------------------------------

    // Address label
    ui_label_t *address_label;
    rc = ui_label_create(ui_window_get_res(pauk_ui->window), "Address:", &address_label);
    if (rc != EOK) return rc;

if (ui_is_textmode(pauk_ui->ui)) {
    rect.p0.x = 1; rect.p0.y = 3 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    rect.p1.x = 9; rect.p1.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    rect.p0.x = CONTENT_MARGIN + 100;
    rect.p0.y = pauk_ui->current_y1 + 4;
    rect.p1.x = CONTENT_MARGIN + 160 + pauk_ui->globX;  // <-- add globX
    rect.p1.y = pauk_ui->current_y1 + ENTRY_HEIGHT + 4;
}
ui_label_set_rect(address_label, &rect);
pauk_ui->address_label_base_rect = rect;
    rc = ui_fixed_add(pauk_ui->fixed, ui_label_ctl(address_label));
    if (rc != EOK) return rc;

    // Address entry
    rc = ui_entry_create(pauk_ui->window, "http://", &pauk_ui->address_entry);
    if (rc != EOK) return rc;

    // Address entry - shift up 20px
if (ui_is_textmode(pauk_ui->ui)) {
    pauk_ui->address_entry_rect.p0.x = 10; rect.p0.y = 3 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    pauk_ui->address_entry_rect.p1.x = 70; rect.p1.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    pauk_ui->address_entry_rect.p0.x = CONTENT_MARGIN + 170;
    pauk_ui->address_entry_rect.p0.y = pauk_ui->current_y1;
    pauk_ui->address_entry_rect.p1.x = 1030 - CONTENT_MARGIN - 160 + pauk_ui->globX; // <-- add globX
    pauk_ui->address_entry_rect.p1.y = pauk_ui->current_y1 + ENTRY_HEIGHT;
}
   ui_entry_set_rect(pauk_ui->address_entry, &pauk_ui->address_entry_rect);
    rc = ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->address_entry));
    if (rc != EOK) return rc;

    // Go button
    static ui_pbutton_cb_t pbutton_cb = { .clicked = go_button_clicked }; //go_button_clicked
    rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "Go", &pauk_ui->go_button);
    if (rc != EOK) return rc;

    ui_pbutton_set_cb(pauk_ui->go_button, &pbutton_cb, (void *)pauk_ui);

    if (ui_is_textmode(pauk_ui->ui)) {
        pauk_ui->go_button_rect.p0.x = 71; rect.p0.y = 3 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
        pauk_ui->go_button_rect.p1.x = 79; rect.p1.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    } else {
        pauk_ui->go_button_rect.p0.x = 1030 - CONTENT_MARGIN - 150; // <-- add globX
        pauk_ui->go_button_rect.p0.y = pauk_ui->current_y1;
        pauk_ui->go_button_rect.p1.x = 1030 - CONTENT_MARGIN - 70;  // <-- add globX
        pauk_ui->go_button_rect.p1.y = pauk_ui->current_y1 + ENTRY_HEIGHT;  
    }
    ui_pbutton_set_rect(pauk_ui->go_button, &pauk_ui->go_button_rect);
    rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->go_button));
    if (rc != EOK) return rc;

// Bookmark button - create and position
static ui_pbutton_cb_t bookmark_button_cb = { .clicked = bookmark_button_clicked}; //bookmark_button_clicked
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "Bookmark", &pauk_ui->bookmark_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->bookmark_button, &bookmark_button_cb, (void *)pauk_ui);

if (ui_is_textmode(pauk_ui->ui)) {
    pauk_ui->bookmark_button_rect.p0.x = 70; rect.p0.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    pauk_ui->bookmark_button_rect.p1.x = 78; rect.p1.y = 5 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    pauk_ui->bookmark_button_rect.p0.x = 1030 - CONTENT_MARGIN - 60 + pauk_ui->globX; // <-- add globX
    pauk_ui->bookmark_button_rect.p0.y = pauk_ui->current_y1;
    pauk_ui->bookmark_button_rect.p1.x = 1030 - CONTENT_MARGIN + pauk_ui->globX;     // <-- add globX
    pauk_ui->bookmark_button_rect.p1.y = pauk_ui->current_y1 + ENTRY_HEIGHT;
}
ui_pbutton_set_rect(pauk_ui->bookmark_button, &pauk_ui->bookmark_button_rect);
rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->bookmark_button));
if (rc != EOK) return rc;

pauk_ui->current_y2 =pauk_ui->current_y1 +  ENTRY_HEIGHT + ROW_SPACING+10;

    // SEARCH label
    ui_label_t *search_label;
    rc = ui_label_create(ui_window_get_res(pauk_ui->window), "Upisi za pretragu:", &search_label);
    if (rc != EOK) return rc;

if (ui_is_textmode(pauk_ui->ui)) {
    rect.p0.x = 1; rect.p0.y = 3 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    rect.p1.x = 9; rect.p1.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    rect.p0.x = CONTENT_MARGIN + 30;
    rect.p0.y = pauk_ui->current_y2 + 4;
    rect.p1.x = CONTENT_MARGIN + 90 + pauk_ui->globX;       // <-- add globX
    rect.p1.y = pauk_ui->current_y2 + ENTRY_HEIGHT + 4;
}
    ui_label_set_rect(search_label, &rect);
    rc = ui_fixed_add(pauk_ui->fixed, ui_label_ctl(search_label));
    if (rc != EOK) return rc;


// PRETRAGA
rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->search_entry);
if (rc != EOK) return rc;

if (ui_is_textmode(pauk_ui->ui)) {
    pauk_ui->search_entry_rect.p0.x = 10; rect.p0.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    pauk_ui->search_entry_rect.p1.x = 60; rect.p1.y = 5 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    pauk_ui->search_entry_rect.p0.x = CONTENT_MARGIN + 170;
    pauk_ui->search_entry_rect.p0.y = pauk_ui->current_y2;
    pauk_ui->search_entry_rect.p1.x = 1030 - CONTENT_MARGIN - 160 + pauk_ui->globX; // <-- add globX
    pauk_ui->search_entry_rect.p1.y = pauk_ui->current_y2 + ENTRY_HEIGHT;
}
ui_entry_set_rect(pauk_ui->search_entry, &pauk_ui->search_entry_rect);

rc = ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->search_entry));
if (rc != EOK) return rc;


    // Search button - create and position
static ui_pbutton_cb_t search_button_cb = { .clicked = search_button_clicked  };
rc = ui_pbutton_create(ui_window_get_res(pauk_ui->window), "Trazi!", &pauk_ui->search_button);
if (rc != EOK) return rc;
ui_pbutton_set_cb(pauk_ui->search_button, &search_button_cb, (void *)pauk_ui);

if (ui_is_textmode(pauk_ui->ui)) {
    pauk_ui->search_button_rect.p0.x = 61; rect.p0.y = 4 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
    pauk_ui->search_button_rect.p1.x = 69; rect.p1.y = 5 + SYSTEM_MENU_HEIGHT - 20;  // Shift up
} else {
    pauk_ui->search_button_rect.p0.x = 1030 - CONTENT_MARGIN - 150; // <-- add globX
    pauk_ui->search_button_rect.p0.y = pauk_ui->current_y2;
    pauk_ui->search_button_rect.p1.x = 1030 - CONTENT_MARGIN - 70;  // <-- add globX
    pauk_ui->search_button_rect.p1.y = pauk_ui->current_y2 + ENTRY_HEIGHT;
}
ui_pbutton_set_rect(pauk_ui->search_button, &pauk_ui->search_button_rect);
rc = ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->search_button));
if (rc != EOK) return rc;

pauk_ui->current_y3 = pauk_ui->current_y2 + ENTRY_HEIGHT + ROW_SPACING;

ui_resource_t *ui_res = ui_window_get_res(pauk_ui->window);


// ========== SEARCH ENGINE SELECTION (Radio Buttons) ==========
pauk_ui->current_y3 = pauk_ui->current_y2 + ENTRY_HEIGHT + ROW_SPACING + 5;

// Search engine label
ui_label_t *engine_label;
rc = ui_label_create(ui_res, "Engine:", &engine_label);
if (rc == EOK) {
    gfx_rect_t engine_rect = {
        CONTENT_MARGIN + 30,
        pauk_ui->current_y3 + 4,
        CONTENT_MARGIN + 90,
        pauk_ui->current_y3 + ENTRY_HEIGHT + 4
    };
    ui_label_set_rect(engine_label, &engine_rect);
    ui_fixed_add(pauk_ui->fixed, ui_label_ctl(engine_label));
}

// Initialize current search engine
str_cpy(pauk_ui->current_search_engine, sizeof(pauk_ui->current_search_engine), "google");

// Create radio button group
rc = ui_rbutton_group_create(ui_res, &pauk_ui->search_engine_group);
if (rc != EOK) {
    if (DEB_WARNING){ printf("Error creating radio button group: %s\n", str_error(rc));}
    // Continue without radio buttons
} else {
    // Radio button callback
    static ui_rbutton_group_cb_t rbutton_group_cb = {
        .selected = NULL  //search_engine_selected
    };
    ui_rbutton_group_set_cb(pauk_ui->search_engine_group, &rbutton_group_cb, (void *)pauk_ui);
    
    int radio_x = CONTENT_MARGIN + 100;
    int radio_y = pauk_ui->current_y3;
    int radio_spacing = 100;
    
    // Google radio button
    rc = ui_rbutton_create(pauk_ui->search_engine_group, "Google", (void*)"google", &pauk_ui->google_rbutton);
    if (rc == EOK) {
        gfx_rect_t radio_rect = { radio_x, radio_y, radio_x + 80, radio_y + 20 };
        ui_rbutton_set_rect(pauk_ui->google_rbutton, &radio_rect);
        ui_rbutton_select(pauk_ui->google_rbutton); // Select by default
        rc = ui_fixed_add(pauk_ui->fixed, ui_rbutton_ctl(pauk_ui->google_rbutton));
    }
    
    // Yahoo radio button
    rc = ui_rbutton_create(pauk_ui->search_engine_group, "Yahoo", (void*)"yahoo", &pauk_ui->yahoo_rbutton);
    if (rc == EOK) {
        gfx_rect_t radio_rect = { radio_x + radio_spacing, radio_y, radio_x + radio_spacing + 80, radio_y + 20 };
        ui_rbutton_set_rect(pauk_ui->yahoo_rbutton, &radio_rect);
        rc = ui_fixed_add(pauk_ui->fixed, ui_rbutton_ctl(pauk_ui->yahoo_rbutton));
    }
    
    // Bing radio button
    rc = ui_rbutton_create(pauk_ui->search_engine_group, "Bing", (void*)"bing", &pauk_ui->bing_rbutton);
    if (rc == EOK) {
        gfx_rect_t radio_rect = { radio_x + radio_spacing * 2, radio_y, radio_x + radio_spacing * 2 + 80, radio_y + 20 };
        ui_rbutton_set_rect(pauk_ui->bing_rbutton, &radio_rect);
        rc = ui_fixed_add(pauk_ui->fixed, ui_rbutton_ctl(pauk_ui->bing_rbutton));
    }
    
    // DuckDuckGo radio button
    rc = ui_rbutton_create(pauk_ui->search_engine_group, "DDG", (void*)"duckduckgo", &pauk_ui->duckduckgo_rbutton);
    if (rc == EOK) {
        gfx_rect_t radio_rect = { radio_x + radio_spacing * 3, radio_y, radio_x + radio_spacing * 3 + 60, radio_y + 20 };
        ui_rbutton_set_rect(pauk_ui->duckduckgo_rbutton, &radio_rect);
        rc = ui_fixed_add(pauk_ui->fixed, ui_rbutton_ctl(pauk_ui->duckduckgo_rbutton));
    }
    
    // Yandex radio button
    rc = ui_rbutton_create(pauk_ui->search_engine_group, "Yandex", (void*)"yandex", &pauk_ui->yandex_rbutton);
    if (rc == EOK) {
        gfx_rect_t radio_rect = { radio_x + radio_spacing * 4, radio_y, radio_x + radio_spacing * 4 + 80, radio_y + 20 };
        ui_rbutton_set_rect(pauk_ui->yandex_rbutton, &radio_rect);
        rc = ui_fixed_add(pauk_ui->fixed, ui_rbutton_ctl(pauk_ui->yandex_rbutton));
    }
}

// Update current_y3 for the next elements
pauk_ui->current_y3 += 25;


    // Get graphics context
    pauk_ui->gc = ui_window_get_gc(pauk_ui->window);


    pauk_ui->current_y4 = pauk_ui->current_y3 + 10;  // Extra 10px
   
// ========== CREATE TAB INTERFACE (one-time) ==========
rc = ui_tab_set_create(ui_res, &pauk_ui->tabset);
if (rc != EOK) {
    if (DEB_WARNING){ printf("Error creating tab set: %s\n", str_error(rc));}
    return rc;
}

// Position tab control below search bar

pauk_ui->tab_rect.p0.x = CONTENT_MARGIN;
pauk_ui->tab_rect.p0.y = pauk_ui->current_y4;
pauk_ui->tab_rect.p1.x = 1030 - CONTENT_MARGIN + pauk_ui->globX;
pauk_ui->tab_rect.p1.y = 585 - STATUS_HEIGHT + pauk_ui->globY;

// Pass pointer to function
ui_tab_set_set_rect(pauk_ui->tabset, &pauk_ui->tab_rect);
pauk_ui->tab_rect_base = pauk_ui->tab_rect;


// Add tabset control to fixed layout (window-level fixed)
ui_control_t *tabset_control = ui_tab_set_ctl(pauk_ui->tabset);
rc = ui_fixed_add(pauk_ui->fixed, tabset_control);
if (rc != EOK) return rc;

// Create tabs (Browser, Email, Settings)
rc = ui_tab_create(pauk_ui->tabset, "Browser", &pauk_ui->browser_tab);
if (rc != EOK) return rc;
rc = ui_tab_create(pauk_ui->tabset, "Forme", &pauk_ui->forms_tab);
if (rc != EOK) return rc;
rc = ui_tab_create(pauk_ui->tabset, "Email", &pauk_ui->email_tab);
if (rc != EOK) return rc;
rc = ui_tab_create(pauk_ui->tabset, "Podesavanja", &pauk_ui->settings_tab);
if (rc != EOK) return rc;

// ========== BROWSER TAB CONTENT ==========
ui_fixed_t *browser_fixed = NULL;
rc = ui_fixed_create(&browser_fixed);
if (rc != EOK) return rc;

// Store the browser content area for drawing
//pauk_ui->list_rect = browser_content_rect;
pauk_ui->list_rect.p0.x = pauk_ui->tab_rect.p0.x + 5;
pauk_ui->list_rect.p0.y = pauk_ui->tab_rect.p0.y + 25;
pauk_ui->list_rect.p1.x = pauk_ui->tab_rect.p1.x - 5 + pauk_ui->globX;
pauk_ui->list_rect.p1.y = pauk_ui->tab_rect.p1.y - 5 + pauk_ui->globY;
pauk_ui->list_rect_base = pauk_ui->list_rect;
pauk_ui->use_html_rendering = true;

ui_tab_add(pauk_ui->browser_tab, ui_fixed_ctl(browser_fixed));


rc = ui_scrollbar_create(pauk_ui->ui, pauk_ui->window, ui_sbd_vert, &pauk_ui->vscrollbar);
if (rc != EOK) {
    if (DEB_WARNING){ printf("Error creating scrollbar: %s\n", str_error(rc));}
    // Continue without scrollbar
} else {
    // Position scrollbar on the right side of the browser content
    gfx_rect_t scrollbar_rect = {
        .p0 = { 
            pauk_ui->list_rect.p1.x - 20,     // 20px from right edge
            pauk_ui->list_rect.p0.y           // Same top as content
        },
        .p1 = { 
            pauk_ui->list_rect.p1.x,          // Right edge
            pauk_ui->list_rect.p1.y           // Same bottom as content
        }
    };
    ui_scrollbar_set_rect(pauk_ui->vscrollbar, &scrollbar_rect);
    
    // Set callbacks
    ui_scrollbar_set_cb(pauk_ui->vscrollbar, &scrollbar_cb, pauk_ui);
    
    // Add to browser fixed layout
    ui_fixed_add(browser_fixed, ui_scrollbar_ctl(pauk_ui->vscrollbar));
    
    if (DEB_INIT_SCROLLBAR){ printf("Scrollbar created successfully\n");}
}

if (pauk_ui->vscrollbar) {
    // Set thumb size proportional to content
    gfx_coord_t view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
    
    // If we know content height, set thumb size accordingly
    // For now, use a reasonable default (1/3 of view height)
    ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, view_height / 3);
    
    ui_scrollbar_set_pos(pauk_ui->vscrollbar, 0);
    pauk_ui->scroll_y = 0;
}

// ========== FORMS TAB CONTENT =========

// Create fixed layout for forms tab
rc = ui_fixed_create(&pauk_ui->forms_fixed);
if (rc != EOK) return rc;

// Add forms tab content
ui_tab_add(pauk_ui->forms_tab, ui_fixed_ctl(pauk_ui->forms_fixed));

gfx_rect_t forms_rect = pauk_ui->tab_rect;
forms_rect.p0.y += 25;
forms_rect.p1.y -= 5;

int forms_current_y = forms_rect.p0.y + 10;

// Forms Title (centered at top)
rc = ui_label_create(ui_res, "Predefinisane Forme", &pauk_ui->forms_title);
gfx_rect_t forms_title_rect = { 
    forms_rect.p0.x + 10, 
    forms_current_y, 
    forms_rect.p1.x - 10, 
    forms_current_y + 25 
};
ui_label_set_rect(pauk_ui->forms_title, &forms_title_rect);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->forms_title));
forms_current_y += 30;

// Calculate column widths
int column_width = (forms_rect.p1.x - forms_rect.p0.x - 30) / 2; // Split with 30px gap
int left_col_x = forms_rect.p0.x + 10;
int right_col_x = left_col_x + column_width + 20; // 20px gap between columns

// === CONTACT FORM (LEFT COLUMN) ===
rc = ui_label_create(ui_res, "Kontakt Forma", &pauk_ui->contact_title);
gfx_rect_t contact_rect = { 
    left_col_x, 
    forms_current_y, 
    left_col_x + column_width, 
    forms_current_y + 20 
};
ui_label_set_rect(pauk_ui->contact_title, &contact_rect);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->contact_title));
int contact_current_y = forms_current_y + 25;

// Name field
rc = ui_label_create(ui_res, "Ime:", &pauk_ui->ui_label_name);
gfx_rect_t label_rect_name = { 
    left_col_x, 
    contact_current_y, 
    left_col_x + 80, 
    contact_current_y + 20 
};
ui_label_set_rect(pauk_ui->ui_label_name, &label_rect_name);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->ui_label_name));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->contact_name);
gfx_rect_t entry_rect_name = { 
    left_col_x + 90, 
    contact_current_y, 
    left_col_x + column_width, 
    contact_current_y + 25 
};
ui_entry_set_rect(pauk_ui->contact_name, &entry_rect_name);
ui_fixed_add(pauk_ui->forms_fixed, ui_entry_ctl(pauk_ui->contact_name));
contact_current_y += 30;

// Email field
rc = ui_label_create(ui_res, "Email:", &pauk_ui->ui_label_contact);
gfx_rect_t label_rect_contact = { 
    left_col_x, 
    contact_current_y, 
    left_col_x + 80, 
    contact_current_y + 20 
};
ui_label_set_rect(pauk_ui->ui_label_contact, &label_rect_contact);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->ui_label_contact));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->contact_email);
gfx_rect_t entry_rect_contact = { 
    left_col_x + 90, 
    contact_current_y, 
    left_col_x + column_width, 
    contact_current_y + 25 
};
ui_entry_set_rect(pauk_ui->contact_email, &entry_rect_contact);
ui_fixed_add(pauk_ui->forms_fixed, ui_entry_ctl(pauk_ui->contact_email));
contact_current_y += 30;

// Message field (half width as requested)
rc = ui_label_create(ui_res, "Poruka:", &pauk_ui->ui_label_message);
gfx_rect_t label_rect_message = { 
    left_col_x, 
    contact_current_y, 
    left_col_x + 80, 
    contact_current_y + 20 
};
ui_label_set_text(pauk_ui->ui_label_message, "Poruka:");
ui_label_set_rect(pauk_ui->ui_label_message, &label_rect_message);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->ui_label_message));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->contact_message);
gfx_rect_t entry_rect_message = { 
    left_col_x + 90, 
    contact_current_y, 
    left_col_x + column_width,  // Half width
    contact_current_y + 60  // Taller for message
};
ui_entry_set_rect(pauk_ui->contact_message, &entry_rect_message);
ui_fixed_add(pauk_ui->forms_fixed, ui_entry_ctl(pauk_ui->contact_message));
contact_current_y += 70;

// Contact Submit Button
static ui_pbutton_cb_t contact_cb = { .clicked = NULL }; //contact_form_submit
rc = ui_pbutton_create(ui_res, "Auto-Fill Kontakt", &pauk_ui->contact_submit);
gfx_rect_t btn_rect = { 
    left_col_x, 
    contact_current_y, 
    left_col_x + 150, 
    contact_current_y + 30 
};
ui_pbutton_set_rect(pauk_ui->contact_submit, &btn_rect);
ui_pbutton_set_cb(pauk_ui->contact_submit, &contact_cb, pauk_ui);
ui_fixed_add(pauk_ui->forms_fixed, ui_pbutton_ctl(pauk_ui->contact_submit));
contact_current_y += 40;

// === LOGIN FORM (RIGHT COLUMN) ===
rc = ui_label_create(ui_res, "Forma Logovanja", &pauk_ui->login_title);
gfx_rect_t login_title_rect = { 
    right_col_x, 
    forms_current_y, 
    right_col_x + column_width-20, 
    forms_current_y + 20 
};
ui_label_set_rect(pauk_ui->login_title, &login_title_rect);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->login_title));
int login_current_y = forms_current_y + 25;

// Username
rc = ui_label_create(ui_res, "Kor.Ime:", &pauk_ui->ui_label_user);
gfx_rect_t label_rect_username = { 
    right_col_x, 
    login_current_y, 
    right_col_x + 80, 
    login_current_y + 20 
};
ui_label_set_rect(pauk_ui->ui_label_user, &label_rect_username);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->ui_label_user));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->login_username);
gfx_rect_t entry_rect_username = { 
    right_col_x + 90, 
    login_current_y, 
    right_col_x + column_width-20, 
    login_current_y + 25 
};
ui_entry_set_rect(pauk_ui->login_username, &entry_rect_username);
ui_fixed_add(pauk_ui->forms_fixed, ui_entry_ctl(pauk_ui->login_username));
login_current_y += 30;

// Password
rc = ui_label_create(ui_res, "Lozinka:", &pauk_ui->ui_label_pass);
gfx_rect_t label_rect_pass = { 
    right_col_x, 
    login_current_y, 
    right_col_x + 80, 
    login_current_y + 20 
};
ui_label_set_rect(pauk_ui->ui_label_pass, &label_rect_pass);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->ui_label_pass));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->login_password);
gfx_rect_t entry_rect_pass = { 
    right_col_x + 90, 
    login_current_y, 
    right_col_x + column_width-20, 
    login_current_y + 25 
};
ui_entry_set_rect(pauk_ui->login_password, &entry_rect_pass);
ui_fixed_add(pauk_ui->forms_fixed, ui_entry_ctl(pauk_ui->login_password));
login_current_y += 35;

// Login Submit Button
static ui_pbutton_cb_t login_cb = { .clicked = NULL }; //login_form_submit
rc = ui_pbutton_create(ui_res, "Auto-Fill Logovanje", &pauk_ui->login_submit);
btn_rect = (gfx_rect_t){ 
    right_col_x, 
    login_current_y, 
    right_col_x + 150, 
    login_current_y + 30 
};
ui_pbutton_set_rect(pauk_ui->login_submit, &btn_rect);
ui_pbutton_set_cb(pauk_ui->login_submit, &login_cb, pauk_ui);
ui_fixed_add(pauk_ui->forms_fixed, ui_pbutton_ctl(pauk_ui->login_submit));
login_current_y += 40;

// Forms Status Label - positioned below both columns
int forms_status_y = (contact_current_y > login_current_y) ? contact_current_y : login_current_y;  // CHANGED: forms_status_y
forms_status_y += 20; // Add some spacing

rc = ui_label_create(ui_res, "Popuni formu/e ovde, zatim prebaci na Browser ", 
                     &pauk_ui->forms_status);
gfx_rect_t forms_status_rect = {  // CHANGED: forms_status_rect
    forms_rect.p0.x + 10, 
    forms_status_y,
    forms_rect.p1.x - 10, 
    forms_status_y + 20 
};
ui_label_set_rect(pauk_ui->forms_status, &forms_status_rect);
ui_fixed_add(pauk_ui->forms_fixed, ui_label_ctl(pauk_ui->forms_status));
// ========== EMAIL TAB CONTENT ==========

ui_fixed_t *email_fixed = NULL;
rc = ui_fixed_create(&email_fixed);
if (rc != EOK) return rc;

gfx_rect_t email_rect = pauk_ui->tab_rect;
email_rect.p0.y += 25;
email_rect.p1.y -= 5;

// Left column width for folders
int left_width = 150;
int right_start = email_rect.p0.x + left_width + 5;

// === LEFT COLUMN: FOLDERS ===
int current_y = email_rect.p0.y + 5;

// Folder buttons
static ui_pbutton_cb_t inbox_cb = { .clicked = NULL }; //show_inbox
rc = ui_pbutton_create(ui_res, "📥 Inbox", &pauk_ui->inbox_button);
gfx_rect_t folder_rect = { email_rect.p0.x + 5, current_y, email_rect.p0.x + left_width - 5, current_y + 25 };
ui_pbutton_set_rect(pauk_ui->inbox_button, &folder_rect);
ui_pbutton_set_cb(pauk_ui->inbox_button, &inbox_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->inbox_button));
current_y += 30;

static ui_pbutton_cb_t sent_cb = { .clicked = NULL }; //show_sent
rc = ui_pbutton_create(ui_res, "📤 Sent", &pauk_ui->sent_button);
folder_rect = (gfx_rect_t){ email_rect.p0.x + 5, current_y, email_rect.p0.x + left_width - 5, current_y + 25 };
ui_pbutton_set_rect(pauk_ui->sent_button, &folder_rect);
ui_pbutton_set_cb(pauk_ui->sent_button, &sent_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->sent_button));
current_y += 30;

static ui_pbutton_cb_t trash_cb = { .clicked = NULL }; //show_trash
rc = ui_pbutton_create(ui_res, "🗑️ Trash", &pauk_ui->trash_button);
folder_rect = (gfx_rect_t){ email_rect.p0.x + 5, current_y, email_rect.p0.x + left_width - 5, current_y + 25 };
ui_pbutton_set_rect(pauk_ui->trash_button, &folder_rect);
ui_pbutton_set_cb(pauk_ui->trash_button, &trash_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->trash_button));
current_y += 30;

static ui_pbutton_cb_t spam_cb = { .clicked = NULL }; //show_spam
rc = ui_pbutton_create(ui_res, "🚫 Spam", &pauk_ui->spam_button);
folder_rect = (gfx_rect_t){ email_rect.p0.x + 5, current_y, email_rect.p0.x + left_width - 5, current_y + 25 };
ui_pbutton_set_rect(pauk_ui->spam_button, &folder_rect);
ui_pbutton_set_cb(pauk_ui->spam_button, &spam_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->spam_button));
current_y += 30;

// Refresh button at bottom of folders
static ui_pbutton_cb_t refresh_cb = { .clicked = NULL }; //refresh_emails
rc = ui_pbutton_create(ui_res, "🔄 Refresh", &pauk_ui->refresh_emails_button);
folder_rect = (gfx_rect_t){ email_rect.p0.x + 5, email_rect.p1.y - 35, email_rect.p0.x + left_width - 5, email_rect.p1.y - 10 };
ui_pbutton_set_rect(pauk_ui->refresh_emails_button, &folder_rect);
ui_pbutton_set_cb(pauk_ui->refresh_emails_button, &refresh_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->refresh_emails_button));

// === RIGHT COLUMN: EMAIL LIST ===
rc = ui_list_create(pauk_ui->window, false, &pauk_ui->email_list);
gfx_rect_t list_rect = { right_start, email_rect.p0.y + 5, 
                        email_rect.p1.x - 5, email_rect.p1.y - 40 };
ui_list_set_rect(pauk_ui->email_list, &list_rect);
ui_fixed_add(email_fixed, ui_list_ctl(pauk_ui->email_list));

// === ACTION BUTTONS (bottom right) ===
static ui_pbutton_cb_t compose_cb = { .clicked = NULL }; //compose_email
rc = ui_pbutton_create(ui_res, "✏️ Compose", &pauk_ui->compose_email_button);
gfx_rect_t button_rect = { right_start, list_rect.p1.y + 5,
                          right_start + 100, list_rect.p1.y + 30 };
ui_pbutton_set_rect(pauk_ui->compose_email_button, &button_rect);
ui_pbutton_set_cb(pauk_ui->compose_email_button, &compose_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->compose_email_button));

static ui_pbutton_cb_t delete_cb = { .clicked = NULL }; //delete_email
rc = ui_pbutton_create(ui_res, "🗑️ Delete", &pauk_ui->delete_email_button);
button_rect = (gfx_rect_t){ right_start + 105, list_rect.p1.y + 5,
                           right_start + 185, list_rect.p1.y + 30 };
ui_pbutton_set_rect(pauk_ui->delete_email_button, &button_rect);
ui_pbutton_set_cb(pauk_ui->delete_email_button, &delete_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->delete_email_button));

static ui_pbutton_cb_t reply_cb = { .clicked = NULL }; //reply_email
rc = ui_pbutton_create(ui_res, "↩️ Reply", &pauk_ui->reply_email_button);
button_rect = (gfx_rect_t){ right_start + 190, list_rect.p1.y + 5,
                           right_start + 260, list_rect.p1.y + 30 };
ui_pbutton_set_rect(pauk_ui->reply_email_button, &button_rect);
ui_pbutton_set_cb(pauk_ui->reply_email_button, &reply_cb, pauk_ui);
ui_fixed_add(email_fixed, ui_pbutton_ctl(pauk_ui->reply_email_button));

// === STATUS LABEL (bottom) ===
rc = ui_label_create(ui_res, "Not connected", &pauk_ui->email_status_label);
gfx_rect_t status_rect = { right_start, button_rect.p1.y + 5,
                          email_rect.p1.x - 5, button_rect.p1.y + 25 };
ui_label_set_rect(pauk_ui->email_status_label, &status_rect);
ui_fixed_add(email_fixed, ui_label_ctl(pauk_ui->email_status_label));

ui_tab_add(pauk_ui->email_tab, ui_fixed_ctl(email_fixed));

// ========== SETTINGS TAB CONTENT ==========
ui_fixed_t *settings_fixed = NULL;
rc = ui_fixed_create(&settings_fixed);
if (rc != EOK) return rc;

gfx_rect_t settings_rect = pauk_ui->tab_rect;
settings_rect.p0.y += 25;
settings_rect.p1.y -= 5;

int col1_x = settings_rect.p0.x + 10;
int col2_x = settings_rect.p0.x + (settings_rect.p1.x - settings_rect.p0.x) / 2 + 10;
 current_y = settings_rect.p0.y + 10;
int entry_width = 150;  // Fixed width for entry fields

// === LEFT COLUMN: EMAIL SETTINGS ===
ui_label_t *email_label;
rc = ui_label_create(ui_res, "Email Podesavanja", &email_label);
gfx_rect_t label_rect = { col1_x, current_y, col1_x + 100, current_y + 20 };
ui_label_set_rect(email_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(email_label));
current_y += 25;

// SMTP Server
rc = ui_label_create(ui_res, "SMTP:", &pauk_ui->smtp_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->smtp_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->smtp_label));

rc = ui_entry_create(pauk_ui->window, "smtp.gmail.com", &pauk_ui->smtp_server_entry);
gfx_rect_t entry_rect = { col1_x + 55, current_y, col1_x + 55 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->smtp_server_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->smtp_server_entry));
current_y += 25;

// SMTP Port
rc = ui_label_create(ui_res, "Port:", &pauk_ui->smtp_port_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->smtp_port_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->smtp_port_label));

rc = ui_entry_create(pauk_ui->window, "587", &pauk_ui->smtp_port_entry);
entry_rect = (gfx_rect_t){ col1_x + 55, current_y, col1_x + 55 + 50, current_y + 20 }; // Narrower for port
ui_entry_set_rect(pauk_ui->smtp_port_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->smtp_port_entry));
current_y += 25;

// Email Address
rc = ui_label_create(ui_res, "Email:", &pauk_ui->email_addr_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->email_addr_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->email_addr_label));

rc = ui_entry_create(pauk_ui->window, "zmajsoft@gmail.com", &pauk_ui->email_addr_entry);
entry_rect = (gfx_rect_t){ col1_x + 55, current_y, col1_x + 110 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->email_addr_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->email_addr_entry));
current_y += 25;

// Password
rc = ui_label_create(ui_res, "Pass:", &pauk_ui->email_pwd_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->email_pwd_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->email_pwd_label));

rc = ui_entry_create(pauk_ui->window, "xrck nmmf bhup twms", &pauk_ui->email_pwd_entry);
//ui_entry_set_password(pauk_ui->email_pwd_entry, true);
entry_rect = (gfx_rect_t){ col1_x + 55, current_y, col1_x + 110 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->email_pwd_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->email_pwd_entry));
current_y += 30;

// Email Buttons (positioned below the last entry)
static ui_pbutton_cb_t save_email_cb = { .clicked = NULL }; //save_email_settings
rc = ui_pbutton_create(ui_res, "Sacuvaj", &pauk_ui->save_email_button);
gfx_rect_t mail_button_rect = { col1_x, current_y+80, col1_x + 80, current_y + 115 };
ui_pbutton_set_rect(pauk_ui->save_email_button, &mail_button_rect);
ui_pbutton_set_cb(pauk_ui->save_email_button, &save_email_cb, pauk_ui);
ui_fixed_add(settings_fixed, ui_pbutton_ctl(pauk_ui->save_email_button));

static ui_pbutton_cb_t test_email_cb = { .clicked = NULL }; //test_email_connection
rc = ui_pbutton_create(ui_res, "Test", &pauk_ui->test_email_button);
button_rect = (gfx_rect_t){ col1_x + 85, current_y+80, col1_x + 145, current_y + 115 };
ui_pbutton_set_rect(pauk_ui->test_email_button, &button_rect);
ui_pbutton_set_cb(pauk_ui->test_email_button, &test_email_cb, pauk_ui);
ui_fixed_add(settings_fixed, ui_pbutton_ctl(pauk_ui->test_email_button));

// POP3 Settings
rc = ui_label_create(ui_res, "POP3 Server:", &pauk_ui->pop3_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 80, current_y + 20 };
ui_label_set_rect(pauk_ui->pop3_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->pop3_label));

rc = ui_entry_create(pauk_ui->window, "pop.gmail.com", &pauk_ui->pop3_server_entry);
entry_rect = (gfx_rect_t){ col1_x + 85, current_y, col1_x + 85 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->pop3_server_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->pop3_server_entry));
current_y += 25;

rc = ui_label_create(ui_res, "POP3 Port:", &pauk_ui->pop3_port_label);
label_rect = (gfx_rect_t){ col1_x, current_y, col1_x + 80, current_y + 20 };
ui_label_set_rect(pauk_ui->pop3_port_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->pop3_port_label));

rc = ui_entry_create(pauk_ui->window, "995", &pauk_ui->pop3_port_entry);
entry_rect = (gfx_rect_t){ col1_x + 85, current_y, col1_x + 85 + 50, current_y + 20 };
ui_entry_set_rect(pauk_ui->pop3_port_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->pop3_port_entry));
current_y += 25;

// === RIGHT COLUMN: NETWORK SETTINGS ===
current_y = settings_rect.p0.y + 10;

ui_label_t *network_label;
rc = ui_label_create(ui_res, "Podesavanje mreze", &network_label);
label_rect = (gfx_rect_t){ col2_x, current_y, col2_x + 140, current_y + 20 };
ui_label_set_rect(network_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(network_label));
current_y += 25;

// Proxy Settings
rc = ui_label_create(ui_res, "Proxy:", &pauk_ui->proxy_label);
label_rect = (gfx_rect_t){ col2_x, current_y, col2_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->proxy_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->proxy_label));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->proxy_entry);
entry_rect = (gfx_rect_t){ col2_x + 55, current_y, col2_x + 55 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->proxy_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->proxy_entry));
current_y += 25;

// DNS Settings
rc = ui_label_create(ui_res, "DNS:", &pauk_ui->dns_label);
label_rect = (gfx_rect_t){ col2_x, current_y, col2_x + 50, current_y + 20 };
ui_label_set_rect(pauk_ui->dns_label, &label_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->dns_label));

rc = ui_entry_create(pauk_ui->window, "", &pauk_ui->dns_entry);
entry_rect = (gfx_rect_t){ col2_x + 55, current_y, col2_x + 55 + entry_width, current_y + 20 };
ui_entry_set_rect(pauk_ui->dns_entry, &entry_rect);
ui_fixed_add(settings_fixed, ui_entry_ctl(pauk_ui->dns_entry));
current_y += 80;

// Network Test Button
static ui_pbutton_cb_t test_network_cb = { .clicked = NULL }; //test_network_settings
rc = ui_pbutton_create(ui_res, "Test Network", &pauk_ui->test_network_button);
button_rect = (gfx_rect_t){ col2_x, current_y, col2_x + 100, current_y + 25 };
ui_pbutton_set_rect(pauk_ui->test_network_button, &button_rect);
ui_pbutton_set_cb(pauk_ui->test_network_button, &test_network_cb, pauk_ui);
ui_fixed_add(settings_fixed, ui_pbutton_ctl(pauk_ui->test_network_button));

// === STATUS LABEL (positioned BELOW both columns) ===
int status_y = current_y + 130;  // Position below the buttons
if (button_rect.p1.y + 10 > status_y) {
    status_y = button_rect.p1.y + 10;  // Ensure it's below email buttons too
}

rc = ui_label_create(ui_res, "Spreman", &pauk_ui->settings_status_label);
gfx_rect_t mail_status_rect = { col1_x, status_y, settings_rect.p1.x - 10, status_y + 20 };
ui_label_set_rect(pauk_ui->settings_status_label, &mail_status_rect);
ui_fixed_add(settings_fixed, ui_label_ctl(pauk_ui->settings_status_label));

// Add the single fixed layout to the settings tab
ui_tab_add(pauk_ui->settings_tab, ui_fixed_ctl(settings_fixed));



// Store references
pauk_ui->browser_fixed = browser_fixed;
pauk_ui->email_fixed = email_fixed;
pauk_ui->settings_fixed = settings_fixed;

// ========== STATUS BAR ==========

     // Status label
     rc = ui_label_create(ui_window_get_res(pauk_ui->window), "Spremno", &pauk_ui->status_label);
     if (rc != EOK) return rc;
 
     if (ui_is_textmode(pauk_ui->ui)) {
         rect.p0.x = 1; rect.p0.y = 24 - 30+ SYSTEM_MENU_HEIGHT;  // Shift up by 30 pixels
         rect.p1.x = 79; rect.p1.y = 25 - 30+ SYSTEM_MENU_HEIGHT; // Shift up by 30 pixels
     } else {
        rect.p0.x = CONTENT_MARGIN;
        rect.p0.y = 595 - STATUS_HEIGHT + pauk_ui->globY; // <-- add globY
        rect.p1.x = 1030 - CONTENT_MARGIN + pauk_ui->globX; // <-- add globX
        rect.p1.y = 595 + pauk_ui->globY;
     }
     ui_label_set_rect(pauk_ui->status_label, &rect);
     pauk_ui->status_label_base_rect = rect;

     rc = ui_fixed_add(pauk_ui->fixed, ui_label_ctl(pauk_ui->status_label));
     if (rc != EOK) return rc;



    // Add fixed layout to window
    ui_window_add(pauk_ui->window, ui_fixed_ctl(pauk_ui->fixed));

pauk_ui->html_renderer = malloc(sizeof(html_renderer_t));
if (pauk_ui->html_renderer) {
    html_renderer_init(pauk_ui->html_renderer, pauk_ui->gc, &pauk_ui->font_manager);
    
    // Create the HTML renderer's bitmap
    gfx_rect_t large_bitmap_rect = {
        .p0 = {pauk_ui->tab_rect.p0.x, pauk_ui->tab_rect.p0.y},  // Start at tab position
        .p1 = {pauk_ui->tab_rect.p0.x + 1400, pauk_ui->tab_rect.p0.y + 900}  // Extend from tab position
    };
    
 
    html_renderer_create_bitmap(pauk_ui->html_renderer, large_bitmap_rect);
    
        // ALSO create virtual pixelmap for scrolling
        pauk_ui->virtual_pixmap = create_virtual_pixelmap(1200, 15000);
    
        if (pauk_ui->virtual_pixmap == NULL) {
            printf("[ERROR] Failed to create virtual pixelmap\n");
            // Handle error...
        } else {
           // printf("[PIXELMAP] Created at init_ui: %ldx%ld\n", 
            //       pauk_ui->virtual_pixmap->width, pauk_ui->virtual_pixmap->height);
        }
        
        // Reset scrollbar to top
        if (pauk_ui->vscrollbar) {
            ui_scrollbar_set_pos(pauk_ui->vscrollbar, 0);
        }
        
        pauk_ui->scroll_y = 0;
        pauk_ui->content_height = 0;
        pauk_ui->content_bitmap = pauk_ui->html_renderer->content_bitmap;

    // CRITICAL: Convert bitmap to UI image but use the normal display area
    if (pauk_ui->html_renderer->content_bitmap) {

       ui_resource_t *ui_res = ui_window_get_res(pauk_ui->window);
    
        // Display the bitmap at the tab position (same as bitmap creation)
        gfx_rect_t display_rect = pauk_ui->list_rect; // This should match the tab area
       
      rc = ui_image_create(ui_res, pauk_ui->html_renderer->content_bitmap, 
                            &display_rect, &pauk_ui->content_image);
        if (rc == EOK) {
            ui_image_set_rect(pauk_ui->content_image, &display_rect);
            ui_image_set_flags(pauk_ui->content_image, ui_imgf_frame);
            
            // Add to browser tab's fixed layout
            rc = ui_fixed_add(browser_fixed, ui_image_ctl(pauk_ui->content_image));
            if (rc != EOK) {
               if (DEB_WARNING){  printf("Failed to add image to browser tab: %s\n", str_error(rc));}
            } else {
                if (DEB_BITMAP){ printf("Successfully added large bitmap to browser tab\n");}
            }
        }
        
    }


}

pauk_ui->use_html_rendering = true;
// HTML INIT KRAJ
    // Paint window

    rc = ui_window_paint(pauk_ui->window);
    if (rc != EOK) return rc;
    rc = gfx_update(pauk_ui->gc);

// startujem tajmer poruka status bara
pauk_ui->status_timer = NULL;
pauk_ui->navigating = 0;
    return EOK;
}

errno_t html_renderer_create_bitmap(html_renderer_t *renderer, gfx_rect_t rect) {
    if (!renderer || !renderer->gc) {
        printf("No graphics context available for bitmap creation\n");
        return EINVAL;
    }
    

    // Initialize bitmap parameters
    gfx_bitmap_params_t params;
    gfx_bitmap_params_init(&params);
    
    params.rect.p0.x = rect.p0.x;
    params.rect.p0.y = rect.p0.y;
    params.rect.p1.x = rect.p1.x;
    params.rect.p1.y = rect.p1.y;
    
    // No key color needed for our use case
    params.key_color = 0;
    
    // Prepare bitmap allocation structure
    gfx_bitmap_alloc_t alloc;
    memset(&alloc, 0, sizeof(alloc));
    int width = rect.p1.x-rect.p0.x;
    int height = rect.p1.y - rect.p0.y;
    // Calculate required memory
    alloc.pitch = width * 4; // 4 bytes per pixel (ARGB)
    size_t bitmap_size = alloc.pitch * height;
   // printf("Allocating %zu bytes for bitmap (pitch: %d)\n", bitmap_size, alloc.pitch);
    
    // Allocate memory for bitmap data
    alloc.pixels = malloc(bitmap_size);
    if (!alloc.pixels) {
        printf("Failed to allocate %zu bytes for bitmap\n", bitmap_size);
        return ENOMEM;
    }
   // printf("Alokacija Zavrsena.\n");
    // Initialize bitmap to white background
    memset(alloc.pixels, 0xFF, bitmap_size); // 0xFF = white in ARGB
  //  printf("Alokacija Pixela za bitmap boju zavrsena.\n");
    // Create bitmap using our allocated memory
    errno_t rc = gfx_bitmap_create(renderer->gc, &params, &alloc, &renderer->content_bitmap);
    if (rc != EOK) {
        printf("Failed to create bitmap: %s\n", str_error(rc));
        free(alloc.pixels);
        return rc;
    }
  //  printf("Bitmapa napravljena.\n");
    // Set bitmap parameters
    renderer->bitmap_rect = params.rect;
    renderer->view_width = width;
    renderer->view_height = height;
    renderer->needs_redraw = true;
    
  //  printf("Successfully created bitmap %dx%d with %zu bytes\n", width, height, bitmap_size);
    return EOK;
}


void get_page_dimensions(cJSON *element, int *max_x, int *max_y) {
    if (!element) return;
    
    // Get this element's bottom-right corner
    int x = get_json_number(element, "x", 0);
    int y = get_json_number(element, "y", 0);
    int w = get_json_number(element, "width", 0);
    int h = get_json_number(element, "height", 0);
    
    int right = x + w;
    int bottom = y + h;
    
    if (right > *max_x) *max_x = right;
    if (bottom > *max_y) *max_y = bottom;
    
    // Recurse into children
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            get_page_dimensions(child, max_x, max_y);
        }
    }
}


// MENI KALBEK
// Callback for menu items
void file_exit(ui_menu_entry_t *mentry, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    ui_quit(pauk_ui->ui);
}



errno_t html_renderer_init(html_renderer_t *renderer, gfx_context_t *gc, 
    font_manager_t *font_manager) {
memset(renderer, 0, sizeof(html_renderer_t));
renderer->gc = gc;
renderer->font_manager = font_manager;
renderer->scroll_y = 0;

// Initialize default styles with proper colors
if (create_color(0, 0, 0, &renderer->default_style.color) != EOK) {
return ENOMEM;
}

if (create_color(65535, 65535, 65535, &renderer->default_style.background_color) != EOK) {
gfx_color_delete(renderer->default_style.color);
return ENOMEM;
}

//renderer->default_style.font_index = font_manager->default_font_index;
renderer->default_style.size = 16;
renderer->default_style.bold = false;
renderer->default_style.italic = false;
renderer->default_style.underline = false;

// Link style - blue underlined
if (create_color(0, 0, 32768, &renderer->link_style.color) != EOK) {
gfx_color_delete(renderer->default_style.color);
gfx_color_delete(renderer->default_style.background_color);
return ENOMEM;
}

renderer->link_style.background_color = NULL; // Transparent background
renderer->link_style.font_index = font_manager->default_font_index;
renderer->link_style.size = 16;
renderer->link_style.bold = false;
renderer->link_style.italic = false;
renderer->link_style.underline = true;

return EOK;
}



void help_about(ui_menu_entry_t *mentry, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    ui_msg_dialog_params_t mdparams;
    ui_msg_dialog_t *dialog;
    errno_t rc;
    
    ui_msg_dialog_params_init(&mdparams);
    mdparams.caption = "About Pauk";
    mdparams.text = "A simple web browser for SrBinOS";
    
    rc = ui_msg_dialog_create(pauk_ui->ui, &mdparams, &dialog);
    if (rc != EOK) {
        return;
    }
    
    // Simple callback to destroy the dialog when closed
    static ui_msg_dialog_cb_t msg_dialog_cb = {
        .button = NULL,
        .close = NULL
    };
    
    ui_msg_dialog_set_cb(dialog, &msg_dialog_cb, pauk_ui);
}



void run_ui(pauk_ui_t *pauk_ui)
{
    ui_run(pauk_ui->ui);

}

/**
 * Create pixelmap for virtual content
 * COMPLETE function
 */
pixelmap_t* create_virtual_pixelmap(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;
    
    pixelmap_t* pixmap = malloc(sizeof(pixelmap_t));
    if (pixmap == NULL) return NULL;
    
    pixmap->width = width;
    pixmap->height = height;
    
    size_t pixel_count = width * height;
    pixmap->data = malloc(pixel_count * sizeof(pixel_t));
    
    if (pixmap->data == NULL) {
        free(pixmap);
        return NULL;
    }
    
    // Fill with white
    pixel_t white = PIXEL(255, 255, 255, 255);
    for (size_t i = 0; i < pixel_count; i++) {
        pixmap->data[i] = white;
    }
    
    return pixmap;
}

/**
 * Copy pixelmap to bitmap - EXACT pattern from your working code
 */
void pixelmap_to_bitmap_copy(pauk_ui_t* pauk_ui) {
    if (!pauk_ui || !pauk_ui->content_bitmap || !pauk_ui->virtual_pixmap) {
        printf("[ERROR] Missing components\n");
        return;
    }
    
    // Get bitmap info
    gfx_bitmap_alloc_t alloc;
    gfx_bitmap_get_alloc(pauk_ui->content_bitmap, &alloc);
    
    int bytes_per_pixel = 4;
    int bitmap_width = alloc.pitch / bytes_per_pixel;
    int bitmap_height = pauk_ui->html_renderer->view_height;
    
    uint8_t* bitmap_data = (uint8_t*)alloc.pixels;
    pixel_t* pix_data = pauk_ui->virtual_pixmap->data;
    
    // Get bitmap DESTINATION position in UI
    int bitmap_x = pauk_ui->list_rect.p0.x;  // Where bitmap starts in UI
   // int bitmap_y = pauk_ui->list_rect.p0.y;  // Where bitmap starts in UI
    

    
    // Fill bitmap row by row
    for (int dest_y = 0; dest_y < bitmap_height; dest_y++) {
        // CRITICAL FIX: Source Y in pixelmap = scroll position + destination row
        int source_y_in_pixelmap = pauk_ui->scroll_y + dest_y;  // NO bitmap_y here!
        
        // Check if we have pixelmap content for this row
        if (source_y_in_pixelmap < 0 || (sysarg_t)source_y_in_pixelmap >= pauk_ui->virtual_pixmap->height) {
            // No pixelmap content - fill row with white
            for (int x = 0; x < bitmap_width; x++) {
                size_t idx = (dest_y * alloc.pitch) + (x * bytes_per_pixel);
                bitmap_data[idx] = 255;      // B
                bitmap_data[idx + 1] = 255;  // G
                bitmap_data[idx + 2] = 255;  // R
                bitmap_data[idx + 3] = 255;  // A
            }
            continue;
        } 
        
        // Copy from pixelmap to bitmap
        for (int dest_x = 0; dest_x < bitmap_width; dest_x++) {
            // Source X in pixelmap = bitmap X position + destination column
            int source_x_in_pixelmap = bitmap_x + dest_x;  // This part is CORRECT
            
            size_t bitmap_idx = (dest_y * alloc.pitch) + (dest_x * bytes_per_pixel);
            
            if ((sysarg_t)source_x_in_pixelmap < pauk_ui->virtual_pixmap->width) {
                // We have pixelmap content - copy it
                pixel_t pixel = pix_data[source_y_in_pixelmap * pauk_ui->virtual_pixmap->width + source_x_in_pixelmap];
                
                // Convert pixelmap pixel (ARGB) to bitmap (BGRA or whatever your system uses)
                bitmap_data[bitmap_idx] = pixel & 0xFF;           // B
                bitmap_data[bitmap_idx + 1] = (pixel >> 8) & 0xFF;  // G
                bitmap_data[bitmap_idx + 2] = (pixel >> 16) & 0xFF; // R
                bitmap_data[bitmap_idx + 3] = (pixel >> 24) & 0xFF; // A
            } else {
                // No pixelmap content for this column - fill with white
                bitmap_data[bitmap_idx] = 255;      // B
                bitmap_data[bitmap_idx + 1] = 255;  // G
                bitmap_data[bitmap_idx + 2] = 255;  // R
                bitmap_data[bitmap_idx + 3] = 255;  // A
            }
        }
    }
}


void init_navigation_history(pauk_ui_t *pauk_ui)
{
    for (int i = 0; i < 100; i++) {
        pauk_ui->history[i] = NULL;
    }
    pauk_ui->history_current = -1;
    pauk_ui->history_size = 100;
    pauk_ui->history_count = 0;
}

// Add URL to history
void add_to_history(pauk_ui_t *pauk_ui, const char *url)
{
    if (!pauk_ui || !url) return;
    
    // ===== PROVERI DA LI JE URL ISTI KAO PREDHODNI =====
    if (pauk_ui->history_count > 0 && 
        pauk_ui->history[pauk_ui->history_count - 1] && 
        strcmp(pauk_ui->history[pauk_ui->history_count - 1], url) == 0) {
        printf("⏭️ Duplicate URL, not adding to history: %s\n", url);
        return;
    }
    // ===================================================
    
    // Ako smo u sredini istorije (posle Back), odseci budućnost
    if (pauk_ui->history_current < pauk_ui->history_count - 1) {
        // Izbriši sve posle trenutne pozicije
        for (int i = pauk_ui->history_current + 1; i < pauk_ui->history_count; i++) {
            if (pauk_ui->history[i]) {
                free(pauk_ui->history[i]);
                pauk_ui->history[i] = NULL;
            }
        }
        pauk_ui->history_count = pauk_ui->history_current + 1;
    }
    
    if (pauk_ui->history_count >= pauk_ui->history_size) {
        // Remove oldest entry if history is full
        if (pauk_ui->history[0]) {
            free(pauk_ui->history[0]);
        }
        for (int i = 1; i < pauk_ui->history_size; i++) {
            pauk_ui->history[i - 1] = pauk_ui->history[i];
        }
        pauk_ui->history_count--;
        pauk_ui->history_current--;
    }
    
    pauk_ui->history_current = pauk_ui->history_count;
    pauk_ui->history[pauk_ui->history_count] = strdup(url);
    pauk_ui->history_count++;
    
    printf("📜 History: %d entries, current: %d\n", 
           pauk_ui->history_count, pauk_ui->history_current);
}

void build_search_url(const char *engine, const char *query, char *url_buffer, size_t buffer_size)
{
    if (!engine || !query || !url_buffer) return;
    
    // URL encode the query (simple version - replace spaces with +)
    char encoded_query[256];
    size_t j = 0;
    for (size_t i = 0; query[i] != '\0' && j < sizeof(encoded_query) - 1; i++) {
        if (query[i] == ' ') {
            encoded_query[j++] = '+';
        } else if (query[i] == '&') {
            encoded_query[j++] = '%';
            encoded_query[j++] = '2';
            encoded_query[j++] = '6';
        } else if (query[i] == '=') {
            encoded_query[j++] = '%';
            encoded_query[j++] = '3';
            encoded_query[j++] = 'D';
        } else if (query[i] == '?') {
            encoded_query[j++] = '%';
            encoded_query[j++] = '3';
            encoded_query[j++] = 'F';
        } else {
            encoded_query[j++] = query[i];
        }
    }
    encoded_query[j] = '\0';
    
    // Build URL based on search engine
    if (str_casecmp(engine, "google") == 0) {
        snprintf(url_buffer, buffer_size, "https://www.google.com/search?q=%s", encoded_query);
    } else if (str_casecmp(engine, "yahoo") == 0) {
        snprintf(url_buffer, buffer_size, "https://search.yahoo.com/search?p=%s", encoded_query);
    } else if (str_casecmp(engine, "bing") == 0) {
        snprintf(url_buffer, buffer_size, "https://www.bing.com/search?q=%s", encoded_query);
    } else if (str_casecmp(engine, "duckduckgo") == 0) {
        snprintf(url_buffer, buffer_size, "https://duckduckgo.com/?q=%s", encoded_query);
    } else if (str_casecmp(engine, "yandex") == 0) {
        snprintf(url_buffer, buffer_size, "https://yandex.com/search/?text=%s", encoded_query);
    } else {
        // Default to Google
        snprintf(url_buffer, buffer_size, "https://www.google.com/search?q=%s", encoded_query);
    }
    
    printf("Search URL: %s\n", url_buffer);
}

void search_button_clicked(ui_pbutton_t *pbutton, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    const char *search_text = ui_entry_get_text(pauk_ui->search_entry);
    
    if (!search_text || str_length(search_text) == 0) {
        ui_label_set_text(pauk_ui->status_label, "Enter search terms first");
        ui_label_paint(pauk_ui->status_label);
        return;
    }
    
    printf("Searching for: %s using engine: %s\n", search_text, pauk_ui->current_search_engine);
    ui_label_set_text(pauk_ui->status_label, "Searching...");
    ui_label_paint(pauk_ui->status_label);
    
    // Build search URL using selected engine
    char search_url[512];
    build_search_url(pauk_ui->current_search_engine, search_text, search_url, sizeof(search_url));
    
    // Set the search URL in address bar and navigate to it
    ui_entry_set_text(pauk_ui->address_entry, search_url);
    
    // Trigger navigation after a short delay to ensure UI updates
    fibril_usleep(100000); // 100ms delay
    
    // Use the existing Go button functionality
    go_button_clicked(pauk_ui->go_button, pauk_ui);
}

