#include <stdio.h>
#include <errno.h>
#include <str_error.h>

#include <ui/ui.h>
#include <ui/tab.h>
#include <ui/tabset.h>
#include <ui/list.h>
#include <ui/scrollbar.h>
#include <ui/rbutton.h>

#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>
#include <gfx/color.h>
#include <gfx/font.h>
#include <gfx/typeface.h>
#include <gfx/coord.h>
#include <gfximage/tga.h>

#include "gui.h"
#include "font_manager.h"
#include "render_func.h"
#include "change_size.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

pauk_ui_t *global_pauk_ui = NULL;
static const int scroll_step = 20; // pixels per click
static const int page_step = 100;  // pixels per page up/down

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


/** Scrollbar up button pressed (line up) */
static void scrollbar_up(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    // Get current position
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    
    // Move up by step
    gfx_coord_t new_pos = pos - scroll_step;
    if (new_pos < 0) new_pos = 0;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    
    // Update scroll position
    pauk_ui->scroll_y = new_pos;
    printf("[SCROLL] Up: %d -> %d\n", pos, new_pos);
    
    // Force redraw
    gfx_update(global_pauk_ui->gc);
}

/** Scrollbar down button pressed (line down) */
static void scrollbar_down(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos + scroll_step;
    
    // Get max scroll (content height - view height)
    gfx_coord_t max_scroll = pauk_ui->content_height - 
                             (pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y);
    
    if (new_pos > max_scroll) new_pos = max_scroll;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    printf("[SCROLL] Down: %d -> %d (max: %d)\n", pos, new_pos, max_scroll);
    
    gfx_update(global_pauk_ui->gc);
}

/** Page up */
static void scrollbar_page_up(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos - page_step;
    if (new_pos < 0) new_pos = 0;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    printf("[SCROLL] Page up: %d -> %d\n", pos, new_pos);
    
    gfx_update(global_pauk_ui->gc);
}

/** Page down */
static void scrollbar_page_down(ui_scrollbar_t *scrollbar, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    gfx_coord_t pos = ui_scrollbar_get_pos(scrollbar);
    gfx_coord_t new_pos = pos + page_step;
    
    gfx_coord_t max_scroll = pauk_ui->content_height - 
                             (pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y);
    
    if (new_pos > max_scroll) new_pos = max_scroll;
    
    ui_scrollbar_set_pos(scrollbar, new_pos);
    pauk_ui->scroll_y = new_pos;
    printf("[SCROLL] Page down: %d -> %d\n", pos, new_pos);
    
    gfx_update(global_pauk_ui->gc);
}

/** Scrollbar thumb moved (dragging) */
static void scrollbar_moved(ui_scrollbar_t *scrollbar, void *arg, gfx_coord_t pos)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    
    pauk_ui->scroll_y = pos;
    printf("[SCROLL] Moved to: %d\n", pos);
    
    // Force immediate redraw during drag
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

static ui_window_cb_t window_cb = {
    .close = wnd_close,
    .kbd = handle_keyboard_event
};

static void handle_keyboard_event(ui_window_t *window, void *arg, kbd_event_t *event)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;


    if (event->type == KEY_PRESS) {
        switch (event->key) {
            case KC_ENTER:
            case KC_NENTER:
                // Check if search entry has focus and contains text
                if (ui_entry_get_text(pauk_ui->search_entry) && 
                    str_length(ui_entry_get_text(pauk_ui->search_entry)) > 0) {
                    printf("Enter pressed in search field - triggering search\n");
              //      search_button_clicked(pauk_ui->search_button, pauk_ui);
                } else {
                    // Enter key in address bar - trigger Go button
                    printf("Enter key pressed - triggering Go button\n");
                //    go_button_clicked(pauk_ui->go_button, pauk_ui);
                }
                break;
                
            case KC_LEFT:
            printf("Left arrow pressed - going back\n");
           // navigate_back(NULL, pauk_ui);  // Call the button callback version
                break;
                
            case KC_RIGHT:
                // Right arrow - forward navigation
                printf("Right arrow pressed - going forward\n");
             //   navigate_forward(NULL, pauk_ui);  // Call the button callback version
                break;

                case KC_R:
                if (event->mods & KM_CTRL) {
                    printf("Ctrl+R pressed - reloading page\n");
             //       go_button_clicked(pauk_ui->go_button, pauk_ui);
                } else if (event->mods & (KM_ALT | KM_SHIFT)) {
                    // Allow Alt+R or Shift+R to pass through
                    ui_window_def_kbd(window, event);
                } else {
                    // Allow regular 'r' key
                    ui_window_def_kbd(window, event);
                }
                break;
                
            default:
                        // Let the window handle regular text input
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

void start_gui(void)
{
    printf("Start GUI\n");

    pauk_ui_t pauk_ui;

    errno_t rc = init_ui(&pauk_ui, UI_ANY_DEFAULT);
    if (rc != EOK) {
        fprintf(stderr, "Failed to initialize UI: %s\n", str_error(rc));
        return;
    }
    else{ printf("start gui pokrenut.\n");}

    

    gfx_update(global_pauk_ui->gc);
    run_ui(&pauk_ui);
    
   
}



errno_t init_ui(pauk_ui_t *pauk_ui, const char *display_spec)
{
    
    ui_wnd_params_t params;
    ui_menu_entry_t *mexit;
    ui_menu_entry_t *mabout;
 


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

  //  init_navigation_history(pauk_ui);


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

 /*   
// Load bookmarks from file
 rc = load_bookmarks();
if (rc != EOK) {
    if (DEB_WARNING){  printf("Failed to load bookmarks: %s\n", str_error(rc));}
    // Continue without bookmarks
}
*/
// Bookmarks menu
rc = ui_menu_dd_create(pauk_ui->mbar, "~B~ookmarks", NULL, &pauk_ui->mbookmarks);
if (rc != EOK) {
    if (DEB_INIT_MENU){printf("Error creating bookmarks menu.\n");}
    return rc;
}
printf("Bookmarks menu created successfully at address: %p\n", (void*)pauk_ui->mbookmarks);

/*
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
*/
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
static ui_pbutton_cb_t resize_button_cb = { .clicked = change_size }; // change_size  
static ui_pbutton_cb_t back_button_cb = { .clicked = NULL }; //navigate_back
static ui_pbutton_cb_t refresh_button_cb = { .clicked = NULL }; //refresh_page
static ui_pbutton_cb_t forward_button_cb = { .clicked = NULL}; //navigate_forward 


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

rect.p0.x += 33;  // 5px spacing between buttons
rect.p1.x += 33;
ui_pbutton_set_rect(pauk_ui->refresh_button, &rect);
pauk_ui->refresh_button_base_rect =rect;

rect.p0.x += 33;
rect.p1.x += 30;
ui_pbutton_set_rect(pauk_ui->forward_button, &rect);
pauk_ui->forward_button_base_rect =rect;
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
    static ui_pbutton_cb_t pbutton_cb = { .clicked = NULL }; //go_button_clicked
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
static ui_pbutton_cb_t bookmark_button_cb = { .clicked = NULL }; //bookmark_button_clicked
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
static ui_pbutton_cb_t search_button_cb = { .clicked = NULL }; //search_button_clicked
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



// HTML INIT START
font_manager_init(&pauk_ui->font_manager);
font_manager_load_fonts(&pauk_ui->font_manager, "/data/font/");

font_manager_init_substitutions(&pauk_ui->font_manager);

pauk_ui->html_renderer = malloc(sizeof(html_renderer_t));
if (pauk_ui->html_renderer) {
    html_renderer_init(pauk_ui->html_renderer, pauk_ui->gc, &pauk_ui->font_manager);
    
    // Create the HTML renderer's bitmap
    gfx_rect_t large_bitmap_rect = {
        .p0 = {pauk_ui->tab_rect.p0.x, pauk_ui->tab_rect.p0.y},  // Start at tab position
        .p1 = {pauk_ui->tab_rect.p0.x + 1400, pauk_ui->tab_rect.p0.y + 900}  // Extend from tab position
    };
    
 
    html_renderer_create_bitmap(pauk_ui->html_renderer, large_bitmap_rect);
    
    
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

test_simple_text( pauk_ui);

    // Paint window

    rc = ui_window_paint(pauk_ui->window);
    if (rc != EOK) return rc;
    rc = gfx_update(pauk_ui->gc);

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
    printf("Allocating %zu bytes for bitmap (pitch: %d)\n", bitmap_size, alloc.pitch);
    
    // Allocate memory for bitmap data
    alloc.pixels = malloc(bitmap_size);
    if (!alloc.pixels) {
        printf("Failed to allocate %zu bytes for bitmap\n", bitmap_size);
        return ENOMEM;
    }
    printf("Alokacija Zavrsena.\n");
    // Initialize bitmap to white background
    memset(alloc.pixels, 0xFF, bitmap_size); // 0xFF = white in ARGB
    printf("Alokacija Pixela za bitmap boju zavrsena.\n");
    // Create bitmap using our allocated memory
    errno_t rc = gfx_bitmap_create(renderer->gc, &params, &alloc, &renderer->content_bitmap);
    if (rc != EOK) {
        printf("Failed to create bitmap: %s\n", str_error(rc));
        free(alloc.pixels);
        return rc;
    }
    printf("Bitmapa napravljena.\n");
    // Set bitmap parameters
    renderer->bitmap_rect = params.rect;
    renderer->view_width = width;
    renderer->view_height = height;
    renderer->needs_redraw = true;
    
    printf("Successfully created bitmap %dx%d with %zu bytes\n", width, height, bitmap_size);
    return EOK;
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


// PROBAAAA 
/*ova kodna jedinica predstavlja test funkciju 
koja demonstrira renderovanje teksta i trougla
 sa određenim bojom i pozicijom na 
grafičkom korisničkom interfejsu (GUI).
*/
void test_simple_text(pauk_ui_t *pauk_ui) {
    printf("[TEST] Font-only test\n");

    // Clear with white
    int width = pauk_ui->list_rect.p1.x - pauk_ui->list_rect.p0.x;
    int height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
    clear_area_css(pauk_ui, 0, 0, width, height, "white");

    // Get font (should be Arial from your mappings)
    html_font_t *font = font_manager_get_by_name(&pauk_ui->font_manager, "Arial");
    if (!font) {
        printf("ERROR: No font found!\n");
        return;
    }

    // Test different fonts/styles
    int y = 50;
    render_ttf_text_css(pauk_ui, "Arial 24pt", 50, y, font, 24.0f, "black");
    y += 40;
    
    render_ttf_text_css(pauk_ui, "Blue Text", 50, y, font, 20.0f, "blue");
    y += 35;
    
    render_ttf_text_css(pauk_ui, "Red Bold", 50, y, font, 18.0f, "red");
    y += 30;
    
    render_ttf_text_css(pauk_ui, "Green Medium", 50, y, font, 16.0f, "green");
    y += 25;
    
    render_ttf_text_css(pauk_ui, "Gray Small", 50, y, font, 14.0f, "#666666");
    y += 20;
    
    render_ttf_text_css(pauk_ui, "Purple Tiny", 50, y, font, 12.0f, "purple");
    
    printf("[TEST] Font test done\n");
} 


void render_ttf_text(pauk_ui_t *pauk_ui, const char *text, int x, int y,
    html_font_t *font, float size, gfx_color_t *color)
{
if (!pauk_ui || !pauk_ui->html_renderer || !text)
return;

html_renderer_t *renderer = pauk_ui->html_renderer;
if (!renderer->content_bitmap) {
    printf("[TTF] No content bitmap in renderer\n");
return;
}
printf("[TTF] IMA content bitmap in renderer\n");

// Get bitmap allocation
gfx_bitmap_alloc_t alloc;
if (gfx_bitmap_get_alloc(renderer->content_bitmap, &alloc) != EOK) {
     printf("[TTF] Failed to get bitmap allocation\n");
return;
}
printf("[TTF] URADIO bitmap allokaciju\n");

uint32_t *pixels = (uint32_t *)alloc.pixels;
int stride = alloc.pitch / 4;
int width  = renderer->view_width;
int height = renderer->view_height;

// Use provided font or fallback to default
html_font_t *use_font = font ? font :
font_manager_get_font(&pauk_ui->font_manager, 
             pauk_ui->font_manager.default_font_index);

if (!use_font || !use_font->is_loaded) {
    printf("[TTF] Font not loaded (expecting default font)\n");
return;
}

printf("[TTF] Font UCITAN (expecting default font)\n");

stbtt_fontinfo *info = &use_font->info;
float scale = stbtt_ScaleForPixelHeight(info, size);

// Get vertical metrics
int ascent, descent, lineGap;
stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
int baseline = (int)roundf(ascent * scale);

printf("[TTF] OK FONT METRICS\n");

int pen_x = x;
int pen_y = y + baseline;

// Get RGB color components
uint16_t rr = 0, gg = 0, bb = 0;
if (color)
gfx_color_get_rgb_i16(color, &rr, &gg, &bb);
uint8_t r = rr >> 8, g = gg >> 8, b = bb >> 8;

const unsigned char *p = (const unsigned char *)text;
while (*p) {
int code_point = *p;

// Glyph metrics
int advance, lsb;
stbtt_GetCodepointHMetrics(info, code_point, &advance, &lsb);

int x0, y0, x1, y1;
stbtt_GetCodepointBitmapBox(info, code_point, scale, scale, &x0, &y0, &x1, &y1);

int w = x1 - x0;
int h = y1 - y0;

if (w > 0 && h > 0) {
unsigned char *bitmap = calloc(w * h, 1);
if (!bitmap) {
printf("[TTF] Memory allocation failed for glyph\n");
return;
}
printf("[TTF] OK MEMORY ALOKACIJA ZA GLUPH\n");

stbtt_MakeCodepointBitmap(info, bitmap, w, h, w, scale, scale, code_point);
printf("[TTF] OK KODEPOINT BITMAP\n");
int draw_x = pen_x + (int)roundf(lsb * scale);
int draw_y = pen_y + y0;

for (int by = 0; by < h; by++) {
for (int bx = 0; bx < w; bx++) {
   unsigned char a = bitmap[by * w + bx];
   if (a == 0) continue;

   int dx = draw_x + bx;
   int dy = draw_y + by;
   if (dx < 0 || dy < 0 || dx >= width || dy >= height)
       continue;

   uint32_t *dst = &pixels[dy * stride + dx];
   uint32_t old = *dst;

   uint8_t old_r = (old >> 16) & 0xFF;
   uint8_t old_g = (old >> 8) & 0xFF;
   uint8_t old_b = old & 0xFF;

   uint8_t inv_a = 255 - a;
   uint8_t new_r = (r * a + old_r * inv_a) / 255;
   uint8_t new_g = (g * a + old_g * inv_a) / 255;
   uint8_t new_b = (b * a + old_b * inv_a) / 255;

   *dst = 0xFF000000 | (new_r << 16) | (new_g << 8) | new_b;
}
}

free(bitmap);
}

// Kerning with next character
int next = *(p + 1);
int kern = stbtt_GetCodepointKernAdvance(info, code_point, next);

pen_x += (int)roundf((advance + kern) * scale);
p++;
}
printf("[TTF] PRED CRTANJE\n");
// Refresh the display
gfx_bitmap_render(renderer->content_bitmap, &pauk_ui->list_rect, NULL);
gfx_update(pauk_ui->gc);
printf("[TTF] ZAVRSIO\n");
} 
