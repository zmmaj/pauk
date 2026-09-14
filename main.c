
/** @addtogroup lapis
 * @{
 */
/** @file Aplikacija za pisanje texta
 */

#define SVESKA_MAIN
#define STB_TRUETYPE_IMPLEMENTATION
#include "sveska.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mem.h>
#include <math.h>
#include <ui/filedialog.h>
#include <ui/image.h>
#include <ui/ui.h>
#include <ui/wdecor.h>
#include <ui/window.h>
#include <ui/resource.h>
#include <gfx/render.h>
#include <gfx/color.h>
#include <io/pixelmap.h>
#include <ui/paint.h>
#include <ui/fixed.h>
#include <ui/pbutton.h>
#include <ui/menu.h>
#include <ui/menubar.h>
#include <ui/menudd.h>
#include <ui/menuentry.h>
#include <ui/control.h>
#include <vfs/vfs.h>

#include "sveska_font.h"
#include "sveska_keymap.h"


#define NAME  "sveska"
#define DEFAULT_WIDTH  800
#define DEFAULT_HEIGHT 600


// Debug configuration
#define DEBUG_SELECTION 1  // Set to 0 to disable all debug prints

#if DEBUG_SELECTION
#define SEL_DEBUG(fmt, ...) printf("[SELECT] " fmt "\n", ##__VA_ARGS__)
#else
#define SEL_DEBUG(fmt, ...) ((void)0)
#endif



/********************************************************************* */
static void wnd_close(ui_window_t *window, void *arg) {
    sveska_t *sveska = (sveska_t *)arg;
    
    // Set a flag to indicate we're shutting down
    sveska->is_quitting = true;
    
    // This will cause ui_run() to exit
    ui_quit(sveska->ui);
}


/************************************************************************ */


static void wnd_kbd_event(ui_window_t *window, void *arg, kbd_event_t *event) {
    sveska_t *sveska = (sveska_t *)arg;
    if (!sveska || !event) return;
    
    // Only process key presses (ignore releases/repeats)
    if (event->type != KEY_PRESS) {
        ui_window_def_kbd(window, event);
        return;
    }
    
    // Unified handling - replaces all individual handle_* functions
    handle_keyboard_event(sveska, event);
    
    // Pass to default handler
    ui_window_def_kbd(window, event);
}


/******************************************************************* */

void wnd_pos_event(ui_window_t *window, void *arg, pos_event_t *event) {
    sveska_t *sveska = (sveska_t *)arg;
    ui_window_def_pos(window, event);

    gfx_coord2_t mouse_pos = {event->hpos, event->vpos};

    // Ignore menu bar area
    if (mouse_pos.y < sveska->menu_bar_height) {
        sveska->selection.is_selecting = false;
        return;
    }

    if (event->type == POS_PRESS) {
        // Get click position in text coordinates
        int click_pos = get_text_index_at_position(sveska, mouse_pos);
        if (click_pos < 0) click_pos = get_document_length(sveska);

        // Update cursor position
        sveska->cursor_index = click_pos;
        
        // Initialize selection
        sveska->selection.is_selecting = true;
        sveska->selection.start_pos = click_pos;
        sveska->selection.end_pos = click_pos;
        sveska->selection.press_pos = mouse_pos;

        // Force immediate update
        sveska->cursor_visible = true;
        
        // ADDED: Update cursor position before rendering
        update_cursor_position(sveska);
        
        rebuild_display_text(sveska);
        sveska_text_render(sveska);
        draw_char_cursor(sveska);
      //  gfx_update(sveska->window_gc);
    }
    else if (event->type == POS_UPDATE && sveska->selection.is_selecting) {
        // Only update if we have a valid initial position
        if (sveska->selection.start_pos >= 0) {
            int new_pos = get_text_index_at_position(sveska, mouse_pos);
            if (new_pos >= 0) {
                sveska->selection.end_pos = new_pos;
                sveska->cursor_index = new_pos;
            } else {
                // Handle clicks below last line
                sveska->selection.end_pos = get_document_length(sveska);
                sveska->cursor_index = get_document_length(sveska);
            }
    
            // ADDED: Update cursor position
            update_cursor_position(sveska);
    
            // Ensure start is always <= end
            if (sveska->selection.end_pos < sveska->selection.start_pos) {
                // Swap if dragging left
                int temp = sveska->selection.start_pos;
                sveska->selection.start_pos = sveska->selection.end_pos;
                sveska->selection.end_pos = temp;
            }
    
            // Redraw
            rebuild_display_text(sveska);
            sveska_text_render(sveska);
           // gfx_update(sveska->window_gc);
        }
    }
    else if (event->type == POS_RELEASE && sveska->selection.is_selecting) {
        // Finalize selection
        int final_pos = get_text_index_at_position(sveska, mouse_pos);
        if (final_pos < 0) {
            final_pos = get_document_length(sveska);
        }
    
        sveska->selection.end_pos = final_pos;
        sveska->cursor_index = final_pos;
    
        // Normalize selection (start <= end)
        if (sveska->selection.end_pos < sveska->selection.start_pos) {
            int temp = sveska->selection.start_pos;
            sveska->selection.start_pos = sveska->selection.end_pos;
            sveska->selection.end_pos = temp;
        }
    
        // Clear selection if empty (just a click)
        if (sveska->selection.start_pos == sveska->selection.end_pos) {
            sveska->selection.start_pos = -1;
            sveska->selection.end_pos = -1;
        }
    
        sveska->selection.is_selecting = false;
    
        // Force full redraw
        rebuild_display_text(sveska);
        sveska_text_render(sveska);
    }
}




/***************************************************************************** */


void handle_keypress(sveska_t *sveska, const kbd_event_t *event) {
    if (!sveska || !event || !sveska->document.spans) return;

    // Handle Serbian Latin layout
 //   if (sveska->current_layout == KEYBOARD_LAYOUT_SERBIAN_LATIN) {
        char ch = translate_key_to_char(event);
        if (!ch) return;

        if (has_selection(sveska)) {
            int start = MIN(sveska->selection.start_pos, sveska->selection.end_pos);
            int end = MAX(sveska->selection.start_pos, sveska->selection.end_pos);
            int selected_char = end - start;
        
            if (selected_char > 1) {
                delete_selected_text(sveska);
                sveska->cursor_index = start;
            } else {
                sveska->cursor_index = end; 
            }
        }

        size_t span_idx, pos_in_span;
        get_span_and_pos(sveska, sveska->cursor_index, &span_idx, &pos_in_span);


        // Check if we need a new span due to font/style mismatch
text_span_t *span = &sveska->document.spans[span_idx];
if (span->font != sveska->font || 
    span->font_size != sveska->font_size ||
    span->bold != sveska->bold || 
    span->italic != sveska->italic || 
    span->underline != sveska->underline) {
    if (!create_new_span(sveska)) return;
    // Update span_idx and pos_in_span after creating new span
    get_span_and_pos(sveska, sveska->cursor_index, &span_idx, &pos_in_span);
    span = &sveska->document.spans[span_idx];
}

        // Ensure span has capacity
        if (span->length >= span->capacity - 1) {
            size_t new_capacity = span->capacity * 2;
            char *new_text = realloc(span->text, new_capacity);
            if (!new_text) return;
            span->text = new_text;
            span->capacity = new_capacity;
        }

        // Record undo operation before modifying the text
        char char_str[2] = {ch, '\0'};
        edit_operation_t op = {
            .type = OP_INSERT,
            .data.text.text = str_ndup(char_str, 1),
            .data.text.position = sveska->cursor_index,
            .data.text.length = 1,
            .affected_span = span_idx
        };
        
        record_edit(sveska, op);

        // Make space for new character and insert it
        memmove(&span->text[pos_in_span + 1], &span->text[pos_in_span], 
               span->length - pos_in_span);
        span->text[pos_in_span] = ch;
        span->length++;
        span->text[span->length] = '\0';
    // After inserting character
    sveska->cursor_index++;
    sveska_text_render(sveska);
}


static ui_window_cb_t window_cb = {
    .close = wnd_close,
    .kbd = wnd_kbd_event,
    .pos = wnd_pos_event
};

/****************************************************** */

// Assuming gfx_coord2_t has 'x' and 'y' instead of 'hpos' and 'vpos'
int get_text_index_at_position(sveska_t *sveska, gfx_coord2_t mouse_pos) {
    if (!sveska || !sveska->text_buffer || !sveska->font) {
        printf("Greska: Null pokazivaci\n");
        return -1;
    }

    // Find matching line
    int closest_line = -1;
    for (size_t i = 0; i < sveska->line_count; i++) {
        if (mouse_pos.y >= sveska->line_y_positions[i] && 
            mouse_pos.y < sveska->line_y_positions[i] + sveska->line_heights[i]) {
            closest_line = i;
            break;
        }
    }
    

    // Handle clicks below last line
    if (closest_line == -1) {
        // Return position after last character
        return get_document_length(sveska);
    }

    // Handle empty lines
    if (sveska->line_heights[closest_line] == 0) {
        return get_line_start(sveska, closest_line);
    }

    // Find start of line in text buffer
    int line_start = get_line_start(sveska, closest_line);
    if (line_start < 0) line_start = 0;

    // Initialize character position tracking
    int x = sveska->margin_x;
    int closest_char = line_start;
    //int min_distance = INT_MAX;
        // FIX: Use unsigned long for distance calculations
        unsigned long min_distance =  ULONG_MAX;
    int prev_codepoint = 0;
    
    // Track which span we're in
    size_t current_span = 0;
    size_t span_offset = 0;
    size_t global_pos = line_start;
    

    // Find the span containing the line start
    while (current_span < sveska->document.count && 
           span_offset + sveska->document.spans[current_span].length <= (size_t)line_start) {
        span_offset += sveska->document.spans[current_span].length;
        current_span++;
    }

    // Scan through the line character by character
    while (current_span < sveska->document.count && 
           global_pos < (size_t)sveska->text_length && 
           sveska->text_buffer[global_pos] != '\n') {
        
        text_span_t *span = &sveska->document.spans[current_span];
        sveska_font_t *font = span->font ? span->font : sveska->font;
        float scale = span->font_size > 0 ? 
                     stbtt_ScaleForPixelHeight(&font->info, span->font_size) : 
                     font->scale;

        // Get current character and its metrics
        int codepoint = (unsigned char)sveska->text_buffer[global_pos];
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&font->info, codepoint, scale, scale, &x0, &y0, &x1, &y1);

        // Apply kerning from previous character
        if (prev_codepoint) {
            x += (int)(stbtt_GetCodepointKernAdvance(&font->info, prev_codepoint, codepoint) * scale);
        }

        // Calculate character boundaries
        int char_left = x + (int)(x0 * scale);
        int char_right = x + (int)(x1 * scale);
        int char_center = (char_left + char_right) / 2;

        // Check if mouse is within this character's bounds
        if (mouse_pos.x >= char_left && mouse_pos.x <= char_right) {
            // Mouse is within this character - check which side is closer
            if (mouse_pos.x < char_center) {
                return global_pos; // Before this character
            } else {
                return global_pos + 1; // After this character
            }
        }

        // Track closest character in case we don't find an exact match
        unsigned long distance = llabs((long)mouse_pos.x - (long)char_center);
        if (distance < min_distance) {
            min_distance = distance;
            closest_char = (mouse_pos.x < char_center) ? global_pos : global_pos + 1;
        }
        // Advance to next character
        x += (int)(advance * scale) + sveska->char_spacing;
        prev_codepoint = codepoint;
        global_pos++;
        
        // Move to next span if needed
        if (global_pos >= (size_t)(span_offset + span->length)) {
            span_offset += span->length;
            current_span++;
            prev_codepoint = 0; // Reset kerning between spans
        }
    }

    // If we didn't find an exact match, return the closest character
    return closest_char;
}



/************************************************************************ */

static errno_t sveska_window_create(sveska_t *sveska) {
    /* Parameter validation */
    if (!sveska || !sveska->params) {
        return EINVAL;
    }

    /* parametri Prozora setup */
    ui_wnd_params_init(sveska->params);
    sveska->params->caption = "LAPIS"; 
    sveska->params->rect.p1.x = DEFAULT_WIDTH;
    sveska->params->rect.p1.y = DEFAULT_HEIGHT;

    /* Prozor creation */
    errno_t rc = ui_window_create(sveska->ui, sveska->params, &sveska->window);
    if (rc != EOK) {
        return rc;
    }

    /* Get window dimensions */
    gfx_rect_t rect;
    ui_window_get_app_rect(sveska->window, &rect);
    sveska->window_width = rect.p1.x - rect.p0.x;
    sveska->window_height = rect.p1.y - rect.p0.y;
    sveska->bkg_rect = &rect;
    /* Graphics context */
    sveska->window_gc = ui_window_get_gc(sveska->window);
    if (!sveska->window_gc) {
        ui_window_destroy(sveska->window);
        return ENOMEM;
    }

    /* Set up window callbacks */
    ui_window_set_cb(sveska->window, &window_cb, (void *)sveska);

    /* Initialize text color (yellow) */
    rc = gfx_color_new_rgb_i16(0xFF, 0xFF, 0x00, &sveska->color);
    if (rc != EOK) {
        ui_window_destroy(sveska->window);
        return rc;
    }

    /* Text buffer initialization */
    sveska->text_capacity = 1024;
    sveska->text_buffer = malloc(sveska->text_capacity);
    if (!sveska->text_buffer) {
        gfx_color_delete(sveska->color);
        ui_window_destroy(sveska->window);
        return ENOMEM;
    }
    str_cpy(sveska->text_buffer, sveska->text_capacity, "Type here...");
    sveska->text_length = str_length(sveska->text_buffer);

    /* Text layout parameters */
    sveska->margin_x = sveska->text_margin_x;   // Matches background offset (2px window + 4px padding)

    
    if (sveska->font && sveska->font->char_width > 0) {
        sveska->chars_per_line = (sveska->window_width - 2 * sveska->margin_x) / 
                               sveska->font->char_width;
    } else {
        sveska->chars_per_line = 80;
    }
    

    /* Initial render */
    sveska_text_render(sveska);
    return EOK;
}


/********************************************************************** */

void sveska_window_destroy(sveska_t *sveska) {
    if (sveska == NULL) return;

    
    if (sveska->lines) {
        free(sveska->lines);
        sveska->lines = NULL;
    }
    
    if (sveska->bg_color) {
        gfx_color_delete(sveska->bg_color);
        sveska->bg_color = NULL;
    }

   // Free character positions
   if (sveska->char_positions) {
    free(sveska->char_positions);
    sveska->char_positions = NULL;
}

// Free background color
if (sveska->bg_color) {
    gfx_color_delete(sveska->bg_color);
    sveska->bg_color = NULL;
}

    // Free line arrays
    if (sveska->line_y_positions) free(sveska->line_y_positions);
    if (sveska->line_heights) free(sveska->line_heights);


if (sveska->color) {
    gfx_color_delete(sveska->color);
}

if (sveska->text_buffer) {
    free(sveska->text_buffer);
}

if (sveska->font) {
    sveska_font_destroy(sveska->font);
}

if (sveska->keymap_debug) {
    free(sveska->keymap_debug);
}

if (sveska->edit_history) {
    for (size_t i = 0; i < sveska->history_size; i++) {
        if (sveska->edit_history[i].type == OP_INSERT || 
            sveska->edit_history[i].type == OP_DELETE) {
            free(sveska->edit_history[i].data.text.text);
        }
    }
    free(sveska->edit_history);
}

if (sveska->params) {
    free(sveska->params);
}

if (sveska->bparams) {
    free(sveska->bparams);
}

    if (sveska->window != NULL)
        ui_window_destroy(sveska->window);
    
    if (sveska->color != NULL)
        gfx_color_delete(sveska->color);
}
/*********************************************************** */
int main(int argc, char *argv[]) {
    sveska = calloc(1, sizeof(sveska_t));
    if (!sveska) return 1;
    sveska->overwrite_mode = false; 
    sveska->is_quitting = false; 

    // Initialize Caps Lock state
    sveska->caps_lock_on = false;
    
    // Load config
    sveska_config_load(sveska, "/sveska.cfg");
    sveska_init_utf8(sveska); 
    
    // Default colors
    gfx_color_new_rgb_i16(0xFFFF, 0xFFFF, 0xFFFF, &sveska->bkg_color);
    gfx_color_new_rgb_i16(0x0000, 0x0000, 0x0000, &sveska->color);
    gfx_color_new_rgb_i16(0x6666, 0x88FF, 0xFFFF, &sveska->highlight_color);
    
    // Keyboard layout
    sveska->current_layout = KEYBOARD_LAYOUT_US;
    
    // Font scan fallback
    errno_t rc = scan_and_save_font_list();
    if (rc != EOK) {
        printf("Napomena: Ne mogu da skeniram fontove (%d), koristim podrazumevani\n", rc);
    }

    // Default font style settings
    sveska->bold = 0;
    sveska->italic = 0;
    sveska->underline = 0;
    sveska->force_cursor_position = false;
    
    // Selection init
    sveska->selection.is_selecting = 0;
    sveska->selection.start_pos = -1;
    sveska->selection.end_pos = -1;

    // Load font list
    rc = sveska_load_font_list(sveska);
    if (rc != EOK || sveska->font_count == 0) {
        printf("FATALNO: Ne mogu da ucitam listu fontova, ili fontova nema\n");
        goto cleanup;
    }

    // Set default font
    if (sveska->current_font_index < sveska->font_count) {
        sveska->font = &sveska->fonts[sveska->current_font_index];
   
    } else {
        printf("FATALNO: Neispravan current_font_index\n");
        goto cleanup;
    }

    // Allocate params
    sveska->params = calloc(1, sizeof(ui_wnd_params_t));
    sveska->bparams = calloc(1, sizeof(gfx_bitmap_params_t));
    if (!sveska->params || !sveska->bparams) {
        printf("Neuspela alokacija parametara prozora\n");
        goto cleanup;
    }

    // Init edit history
    init_history(sveska);

    // Create UI
    rc = ui_create(UI_ANY_DEFAULT, &sveska->ui);
    if (rc != EOK) {
        printf("Neuspela kreacija UI: %d\n", rc);
        goto cleanup;
    }

    rc = sveska_window_create(sveska);
    if (rc != EOK) {
        printf("Neupela kreacija prozora: %d\n", rc);
        goto cleanup;
    }

    // Debug keymap
    sveska->keymap_debug = calloc(256, sizeof(keymap_debug_t));
    if (!sveska->keymap_debug) {
        printf("Neuspela alokacija rasporeda tastera\n");
        goto cleanup;
    }
    sveska->keymap_debug_count = 0;

    // Text buffer
    sveska->text_capacity = 1024;
    sveska->text_buffer = malloc(sveska->text_capacity);
    if (!sveska->text_buffer) {
        printf("Neuspela alokacija bafera texta\n");
        goto cleanup;
    }
    sveska->text_buffer[0] = '\0';
    sveska->text_length = 0;

    sveska->line_height = sveska->font->line_height;
    sveska->cursor_y = sveska->margin_y + (int)(sveska->font->scale * sveska->ascent);
    sveska->cursor_x = sveska->margin_x;

    // Initialize document
    if (!initialize_document(sveska)) {
        printf("Neuspelo iniciranje dokumenta\n");
        goto cleanup;
    }

    // Menu bar & layout
    rc = ui_fixed_create(&sveska->fixed);
    if (rc != EOK) {
        printf("Greska pri kreaciji fixnog izgleda: %d\n", rc);
        goto cleanup;
    }

    rc = create_menu_bar(sveska);
    if (rc != EOK) {
        printf("Ne mogu da kreiram meni bar: %d\n", rc);
        goto cleanup;
    }

    ui_window_add(sveska->window, ui_fixed_ctl(sveska->fixed));

  // Check for command-line arguments
 // bool file_opened = false;
 if (argc > 1) {
    const char *filename = argv[1];
    
    // Check if file exists before attempting to open
    FILE *test = fopen(filename, "r");
    if (test) {
        fclose(test);
        
        // Use your existing import function
        import_file(sveska, filename);
       // file_opened = true;
    } else {
        printf("File not found: %s\n", filename);
    }
}

    // Initial draw
    ui_window_paint(sveska->window);
    sveska_text_render(sveska);
    ui_menu_bar_paint(sveska->menubar);
    gfx_update(sveska->window_gc);



    // Run the UI loop
    ui_run(sveska->ui);

    if (sveska->is_quitting) {
        // Perform comprehensive cleanup
        goto cleanup;
    }


cleanup:
    // --------------------------
    // ENHANCED CLEANUP SECTION
    // --------------------------
    
    // 1. Free UI controls and callbacks first
    if (sveska && sveska->window) {
        // Remove all controls from the window
        // Clear window callbacks
        ui_window_set_cb(sveska->window, NULL, NULL);
        
        // Destroy the window itself
        ui_window_destroy(sveska->window);
        sveska->window = NULL;
    }

    // 2. Free document spans
    if (sveska && sveska->document.spans) {
        for (size_t i = 0; i < sveska->document.count; i++) {
            text_span_t *span = &sveska->document.spans[i];
            if (span->text) free(span->text);
            if (span->font) sveska_font_unref(sveska, span->font);
        }
        free(sveska->document.spans);
        sveska->document.spans = NULL;
    }

    // 3. Free menu bar and UI resources
    if (sveska && sveska->menubar) {
        ui_menu_bar_destroy(sveska->menubar);
        sveska->menubar = NULL;
    }
    if (sveska && sveska->fixed) {
        ui_control_destroy(ui_fixed_ctl(sveska->fixed));
        sveska->fixed = NULL;
    }

    // 4. Free font cache and fonts
    if (sveska) {
        for (size_t i = 0; i < sveska->font_count; i++) {
            if (sveska->fonts[i].font_data) {
                free(sveska->fonts[i].font_data);
                sveska->fonts[i].font_data = NULL;
            }
        }
        for (size_t i = 0; i < sveska->font_cache_count; i++) {
            if (sveska->font_cache[i].font) {
                sveska_font_unref(sveska, sveska->font_cache[i].font);
                sveska->font_cache[i].font = NULL;
            }
        }
    }

    // 5. Free line tracking arrays
    if (sveska && sveska->line_y_positions) {
        free(sveska->line_y_positions);
        sveska->line_y_positions = NULL;
    }
    if (sveska && sveska->line_heights) {
        free(sveska->line_heights);
        sveska->line_heights = NULL;
    }

    // 6. Free edit history
    if (sveska && sveska->edit_history) {
        for (size_t i = 0; i < sveska->history_size; i++) {
            edit_operation_t *op = &sveska->edit_history[i];
            if ((op->type == OP_INSERT || op->type == OP_DELETE) && 
                op->data.text.text) {
                free(op->data.text.text);
                op->data.text.text = NULL;
            }
            if (op->user_data) {
                free(op->user_data);
                op->user_data = NULL;
            }
        }
        free(sveska->edit_history);
        sveska->edit_history = NULL;
    }

    // 7. Free graphics resources
    if (sveska) {
        if (sveska->color) {
            gfx_color_delete(sveska->color);
            sveska->color = NULL;
        }
        if (sveska->bkg_color) {
            gfx_color_delete(sveska->bkg_color);
            sveska->bkg_color = NULL;
        }
        if (sveska->highlight_color) {
            gfx_color_delete(sveska->highlight_color);
            sveska->highlight_color = NULL;
        }
    }

    // 8. Free text buffer and keymap
    if (sveska) {
        if (sveska->text_buffer) {
            free(sveska->text_buffer);
            sveska->text_buffer = NULL;
        }
        if (sveska->keymap_debug) {
            free(sveska->keymap_debug);
            sveska->keymap_debug = NULL;
        }
    }

    // 9. Free UI instance
    if (sveska && sveska->ui) {
        ui_destroy(sveska->ui);
        sveska->ui = NULL;
    }

    // 10. Free params
    if (sveska) {
        if (sveska->params) {
            free(sveska->params);
            sveska->params = NULL;
        }
        if (sveska->bparams) {
            free(sveska->bparams);
            sveska->bparams = NULL;
        }
    }

    // 11. Free main struct
    if (sveska) {
        free(sveska);
        sveska = NULL;
    }

    return 0;
}


/** @}
 */
