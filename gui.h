#ifndef GUI_H
#define GUI_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <str.h>
#include <mem.h>
#include <str_error.h>

#include <inet/addr.h>
#include <inet/tcp.h>
#include <inet/dnsr.h>
#include <inet/host.h>

#include <gfx/bitmap.h>
#include <gfx/coord.h>
#include <gfx/font.h>
#include <gfx/text.h>
#include <ui/entry.h>
#include <ui/fixed.h>
#include <ui/label.h>
#include <ui/menu.h>
#include <ui/menubar.h>
#include <ui/menudd.h>
#include <ui/menuentry.h>
#include <ui/msgdialog.h>
#include <ui/pbutton.h>
#include <ui/rbutton.h>
#include <ui/resource.h>
#include <ui/ui.h>
#include <ui/image.h>
#include <ui/tab.h>
#include <ui/list.h>
#include <ui/scrollbar.h>
#include <ui/window.h>

#include "font_manager.h"


// Common definitions
#define NAME "pauk"
#define MAX_RETRIES 3
#define MAX_REDIRECTS 10
#define RETRY_DELAY_MS 1000
#define RECV_MAX_RETRIES 100
// DEBUG STUFF////

#define DEB_INFO 1
#define DEB_INIT 0
#define DEB_WARNING 0
#define DEB_INIT_MENU 0
#define DEB_INIT_SCROLLBAR 0
#define DEB_BITMAP 0
#define DEB_INIT_FONT 0
#define DEB_FONT 1
#define DEB_INIT_UI 0
#define DEB_INIT_LEXBOR 0
#define DEB_LEXBOR 0
#define DEB_CSS 0
#define DEB_GO 0
#define DEB_FETCH 0
#define DEB_INIT_CHILD 0

#define SYSTEM_MENU_HEIGHT 30
#define CONTENT_MARGIN 10
#define BUTTON_HEIGHT 25
#define ENTRY_HEIGHT 25
#define STATUS_HEIGHT 20
#define ROW_SPACING 5  


typedef struct {
    int font_index;
    int size;
    gfx_color_t *color;
    gfx_color_t *background_color;
    bool bold;
    bool italic;
    bool underline;
} html_text_style_t;  // ← CHANGED

typedef struct {
    gfx_context_t *gc;
    font_manager_t *font_manager;
   // html_document_t *document;  // ← CHANGED
    int scroll_y;
    int view_width;
    int view_height;

        // Bitmap rendering
        gfx_bitmap_t *content_bitmap;
        gfx_context_t *bitmap_gc;
        gfx_rect_t bitmap_rect;
        bool needs_redraw;
    
    // Default styles
    html_text_style_t default_style;  // ← CHANGED
    html_text_style_t link_style;  // ← CHANGED
} html_renderer_t;


// UI structure
typedef struct pauk_ui {
    ui_t *ui;
    ui_window_t *window;
    ui_fixed_t *fixed;
    ui_menu_bar_t *mbar;
    ui_menu_t *mfile;
    ui_menu_t *mhelp;
    ui_menu_t *mbookmarks;
    ui_entry_t *address_entry;
    ui_pbutton_t *go_button;
    ui_pbutton_t *search_button; 
    ui_pbutton_t *bookmark_button; 
    ui_image_t *content_image;  

    gfx_bitmap_t *content_bitmap;
    gfx_context_t *gc;  
    ui_entry_t *content_area;
    ui_label_t *status_label;
    gfx_color_t *color_bkg;

    gfx_font_t *font;

    // Tab controls
    ui_tab_set_t *tabset;
    ui_tab_t *browser_tab;
    ui_tab_t *email_tab;
    ui_tab_t *settings_tab;
    
    // Tab content fixed layouts
    ui_fixed_t *browser_fixed;
    ui_fixed_t *email_fixed;
    ui_fixed_t *settings_fixed;
    
    // Tab content
    ui_list_t *text_list;
    ui_label_t *email_content;
    ui_label_t *settings_content;

    // Navigation history
    char *history[100];  // Circular buffer for history
    int history_current;
    int history_size;
    int history_count;

    ui_entry_t *search_entry;

    ui_pbutton_t *resize_button;
    ui_pbutton_t *back_button;
    ui_pbutton_t *refresh_button;
    ui_pbutton_t *forward_button;

    #if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt ca_cert;  // <- unique name, not "ca"
    #endif

    int globX;
    int globY;
    bool full_screen_status;

    // Rect globali
    gfx_rect_t win_rect_base;
    gfx_rect_t win_rect_fullscreen;
    gfx_rect_t list_rect; 
    gfx_rect_t tab_rect;
    gfx_rect_t search_rect;
    gfx_rect_t search_entry_rect;
    gfx_rect_t back_button_base_rect;
    gfx_rect_t refresh_button_base_rect;
    gfx_rect_t forward_button_base_rect;
    gfx_rect_t resize_button_rect;
    gfx_rect_t address_label_base_rect;
    gfx_rect_t address_entry_rect;
    gfx_rect_t go_button_rect;
    gfx_rect_t bookmark_button_rect;
    gfx_rect_t search_label_base_rect;
    gfx_rect_t search_entry_base_rect;
    gfx_rect_t search_button_rect;
    gfx_rect_t logo_base_rect;
    gfx_rect_t tab_rect_base;
    gfx_rect_t list_rect_base;
    gfx_rect_t status_label_base_rect;

    // SETTINGS
    ui_label_t *email_settings_label;
    ui_label_t *smtp_label;
    ui_entry_t *smtp_server_entry;
    ui_label_t *smtp_port_label;
    ui_entry_t *smtp_port_entry;
    ui_label_t *email_addr_label;
    ui_entry_t *email_addr_entry;
    ui_label_t *email_pwd_label;
    ui_entry_t *email_pwd_entry;
    ui_label_t *pop3_label;
    ui_entry_t *pop3_server_entry;
    ui_pbutton_t *save_settings_button;
    ui_pbutton_t *test_connection_button;
    ui_label_t *settings_status_label;
    ui_label_t *proxy_label;
    ui_entry_t *proxy_entry;
    ui_label_t *dns_label;
    ui_entry_t *dns_entry;

    // EMAIL
    ui_list_t *email_list;
    ui_pbutton_t *refresh_emails_button;
    ui_pbutton_t *compose_email_button;
    ui_pbutton_t *save_email_button;
    ui_pbutton_t *test_email_button;
    ui_pbutton_t *test_network_button;
    ui_label_t *email_status_label;

    ui_pbutton_t *inbox_button;
    ui_pbutton_t *sent_button;
    ui_pbutton_t *trash_button;
    ui_pbutton_t *spam_button;
    ui_pbutton_t *delete_email_button;
    ui_pbutton_t *reply_email_button;

    // POP3
    ui_entry_t *pop3_port_entry;
    ui_label_t *pop3_port_label;

    // koordinate
    gfx_coord_t current_y1;
    gfx_coord_t current_y2;
    gfx_coord_t current_y3;
    gfx_coord_t current_y4;

    // New Forms tab
    ui_tab_t *forms_tab;
    ui_fixed_t *forms_fixed;
    
    // Form controls in the forms tab
    ui_label_t *forms_title;
    ui_label_t *ui_label_user;
    ui_label_t *ui_label_pass;
    ui_label_t *login_title;
    ui_label_t *ui_label_name;
    ui_label_t *ui_label_contact;
    ui_label_t *ui_label_message;
    ui_entry_t *login_username;
    ui_entry_t *login_password;
    ui_pbutton_t *login_submit;
    ui_label_t *contact_title;
    ui_entry_t *contact_name;
    ui_entry_t *contact_email;
    ui_entry_t *contact_message;
    ui_pbutton_t *contact_submit;
    ui_label_t *forms_status;

    // Search engine selection
    ui_rbutton_group_t *search_engine_group;
    ui_rbutton_t *google_rbutton;
    ui_rbutton_t *yahoo_rbutton;
    ui_rbutton_t *bing_rbutton;
    ui_rbutton_t *duckduckgo_rbutton;
    ui_rbutton_t *yandex_rbutton;
    
    char current_search_engine[32];

    // Font manager
    font_manager_t font_manager;
    html_renderer_t *html_renderer;
    bool use_html_rendering;

    // Scrollbar
    ui_scrollbar_t *vscrollbar;
    int scroll_y;
    int content_height;

} pauk_ui_t;


typedef struct {
    char *from;
    char *subject;
    char *date;
    char *body;
    size_t size;
} email_t;

// CSS structures and parsing
typedef struct {
    const char *name;
    uint32_t value;
} css_named_color_t;

static const css_named_color_t css_named_colors[] = {
    {"black", 0xFF000000},
    {"white", 0xFFFFFFFF},
    {"red", 0xFFFF0000},
    {"green", 0xFF008000},
    {"blue", 0xFF0000FF},
    {"yellow", 0xFFFFFF00},
    {"cyan", 0xFF00FFFF},
    {"magenta", 0xFFFF00FF},
    {"gray", 0xFF808080},
    {"grey", 0xFF808080},
    {"dimgray", 0xFF696969},
    {"dimgrey", 0xFF696969},
    {"darkgray", 0xFFA9A9A9},
    {"darkgrey", 0xFFA9A9A9},
    {"lightgray", 0xFFD3D3D3},
    {"lightgrey", 0xFFD3D3D3},
    {"darkred", 0xFF8B0000},
    {"lightred", 0xFFFF6347},
    {"darkgreen", 0xFF006400},
    {"lightgreen", 0xFF90EE90},
    {"darkblue", 0xFF00008B},
    {"lightblue", 0xFFADD8E6},
    {"maroon", 0xFF800000},
    {"purple", 0xFF800080},
    {"fuchsia", 0xFFFF00FF},
    {"lime", 0xFF00FF00},
    {"olive", 0xFF808000},
    {"navy", 0xFF000080},
    {"teal", 0xFF008080},
    {"aqua", 0xFF00FFFF},
    {"silver", 0xFFC0C0C0},
    {"orange", 0xFFFFA500},
    {"brown", 0xFFA52A2A},
    {"pink", 0xFFFFC0CB},
    {"gold", 0xFFFFD700},
    {"violet", 0xFFEE82EE},
    {NULL, 0}
};

// Simple busy wait delay function
static inline void simple_delay(unsigned int ms)
{
    for (volatile unsigned int i = 0; i < ms * 1000; i++) {
        // Do nothing, just wait
    }
}

uint32_t parse_hex_color(const char *str, size_t len);
int hex_digit(char c);


void start_gui(void);

errno_t init_ui(pauk_ui_t *pauk_ui, const char *display_spec);
errno_t html_renderer_create_bitmap(html_renderer_t *renderer, gfx_rect_t rect);



void wnd_close(ui_window_t *window, void *arg);

//meni
void file_exit(ui_menu_entry_t *mentry, void *arg);
void help_about(ui_menu_entry_t *mentry, void *arg);

errno_t html_renderer_init(html_renderer_t *renderer, gfx_context_t *gc, 
    font_manager_t *font_manager);

 void help_about(ui_menu_entry_t *mentry, void *arg);


 void run_ui(pauk_ui_t *pauk_ui);

 void test_simple_text(pauk_ui_t *pauk_ui);

 void render_ttf_text(pauk_ui_t *pauk_ui, const char *text, int x, int y,
    html_font_t *font, float size, gfx_color_t *color);


    float roundf(float value);

#endif // GUI
