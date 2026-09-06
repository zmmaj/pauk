#ifndef GUI_H
#define GUI_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <str.h>
#include <mem.h>
#include <str_error.h>
#include <io/pixelmap.h>

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

#include "cjson.h"
#include "font_manager.h"


// Common definitions
#define NAME "pauk"
#define MAX_RETRIES 3
#define MAX_REDIRECTS 10
#define RETRY_DELAY_MS 20
#define RECV_MAX_RETRIES 500
// DEBUG STUFF////

#define DEB_INFO 0
#define DEB_INIT 0
#define DEB_WARNING 0
#define DEB_INIT_MENU 1
#define DEB_INIT_SCROLLBAR 0
#define DEB_BITMAP 0
#define DEB_INIT_FONT 0
#define DEB_FONT 0
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

typedef struct {
    char *original_url;
    char *local_path;
    cJSON *element;        // Pointer to JSON element
    int status;            // 0=pending, 1=downloaded, 2=failed
    int catalog_index;     // Index in catalog
} ImageTask;

typedef struct {
    ImageTask tasks[256];
    int count;
    char domain[256];
    tcp_conn_t *conn;
    void *tls_conn;
    int is_downloading;
    int current_index;
} ImageCatalog;


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
    int navigating;

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
    ui_scrollbar_t *hscrollbar;
    int scroll_y;
    int scroll_x;
    int content_height;
    int content_width;
    pixelmap_t* virtual_pixmap;
    int scroll_step;  
    int page_step; 
    float pixels_per_scroll_unit; 

    // MIS
    gfx_coord2_t mouse_pos;
    gfx_coord2_t prev_pos; ;
    bool cursor_get_pos;
    bool cursor_move;
    bool mouse_pressed;
    bool hover; 

    cJSON* rendering_json;
    cJSON* current_json; 
    cJSON* current_hover; 
    fibril_timer_t *hover_timer;
    int hover_interval; 

    cJSON* focused_element;
    int cursor_position;

    // tajmer za brisanje statusnih poruka
    fibril_timer_t *status_timer;
    char status_message[256];
    int status_duration;

    #if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt ca_cert;  // <- unique name, not "ca"
    #endif
    
    ImageCatalog image_catalog; 
    char *current_address;  // Trenutna adresa stranice

} pauk_ui_t;

extern pauk_ui_t *global_pauk_ui;

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
    {"aliceblue", 0xFFF0F8FF},
    {"antiquewhite", 0xFFFAEBD7},
    {"aqua", 0xFF00FFFF},
    {"aquamarine", 0xFF7FFFD4},
    {"azure", 0xFFF0FFFF},
    {"beige", 0xFFF5F5DC},
    {"bisque", 0xFFFFE4C4},
    {"black", 0xFF000000},
    {"blanchedalmond", 0xFFFFEBCD},
    {"blue", 0xFF0000FF},
    {"blueviolet", 0xFF8A2BE2},
    {"brown", 0xFFA52A2A},
    {"burlywood", 0xFFDEB887},
    {"cadetblue", 0xFF5F9EA0},
    {"chartreuse", 0xFF7FFF00},
    {"chocolate", 0xFFD2691E},
    {"coral", 0xFFFF7F50},
    {"cornflowerblue", 0xFF6495ED},
    {"cornsilk", 0xFFFFF8DC},
    {"crimson", 0xFFDC143C},
    {"cyan", 0xFF00FFFF},
    {"darkblue", 0xFF00008B},
    {"darkcyan", 0xFF008B8B},
    {"darkgoldenrod", 0xFFB8860B},
    {"darkgray", 0xFFA9A9A9},
    {"darkgrey", 0xFFA9A9A9},
    {"darkgreen", 0xFF006400},
    {"darkkhaki", 0xFFBDB76B},
    {"darkmagenta", 0xFF8B008B},
    {"darkolivegreen", 0xFF556B2F},
    {"darkorange", 0xFFFF8C00},
    {"darkorchid", 0xFF9932CC},
    {"darkred", 0xFF8B0000},
    {"darksalmon", 0xFFE9967A},
    {"darkseagreen", 0xFF8FBC8F},
    {"darkslateblue", 0xFF483D8B},
    {"darkslategray", 0xFF2F4F4F},
    {"darkslategrey", 0xFF2F4F4F},
    {"darkturquoise", 0xFF00CED1},
    {"darkviolet", 0xFF9400D3},
    {"deeppink", 0xFFFF1493},
    {"deepskyblue", 0xFF00BFFF},
    {"dimgray", 0xFF696969},
    {"dimgrey", 0xFF696969},
    {"dodgerblue", 0xFF1E90FF},
    {"firebrick", 0xFFB22222},
    {"floralwhite", 0xFFFFFAF0},
    {"forestgreen", 0xFF228B22},
    {"fuchsia", 0xFFFF00FF},
    {"gainsboro", 0xFFDCDCDC},
    {"ghostwhite", 0xFFF8F8FF},
    {"gold", 0xFFFFD700},
    {"goldenrod", 0xFFDAA520},
    {"gray", 0xFF808080},
    {"grey", 0xFF808080},
    {"green", 0xFF008000},
    {"greenyellow", 0xFFADFF2F},
    {"honeydew", 0xFFF0FFF0},
    {"hotpink", 0xFFFF69B4},
    {"indianred", 0xFFCD5C5C},
    {"indigo", 0xFF4B0082},
    {"ivory", 0xFFFFFFF0},
    {"khaki", 0xFFF0E68C},
    {"lavender", 0xFFE6E6FA},
    {"lavenderblush", 0xFFFFF0F5},
    {"lawngreen", 0xFF7CFC00},
    {"lemonchiffon", 0xFFFFFACD},
    {"lightblue", 0xFFADD8E6},
    {"lightcoral", 0xFFF08080},
    {"lightcyan", 0xFFE0FFFF},
    {"lightgoldenrodyellow", 0xFFFAFAD2},
    {"lightgray", 0xFFD3D3D3},
    {"lightgrey", 0xFFD3D3D3},
    {"lightgreen", 0xFF90EE90},
    {"lightpink", 0xFFFFB6C1},
    {"lightsalmon", 0xFFFFA07A},
    {"lightseagreen", 0xFF20B2AA},
    {"lightskyblue", 0xFF87CEFA},
    {"lightslategray", 0xFF778899},
    {"lightslategrey", 0xFF778899},
    {"lightsteelblue", 0xFFB0C4DE},
    {"lightyellow", 0xFFFFFFE0},
    {"lime", 0xFF00FF00},
    {"limegreen", 0xFF32CD32},
    {"linen", 0xFFFAF0E6},
    {"magenta", 0xFFFF00FF},
    {"maroon", 0xFF800000},
    {"mediumaquamarine", 0xFF66CDAA},
    {"mediumblue", 0xFF0000CD},
    {"mediumorchid", 0xFFBA55D3},
    {"mediumpurple", 0xFF9370DB},
    {"mediumseagreen", 0xFF3CB371},
    {"mediumslateblue", 0xFF7B68EE},
    {"mediumspringgreen", 0xFF00FA9A},
    {"mediumturquoise", 0xFF48D1CC},
    {"mediumvioletred", 0xFFC71585},
    {"midnightblue", 0xFF191970},
    {"mintcream", 0xFFF5FFFA},
    {"mistyrose", 0xFFFFE4E1},
    {"moccasin", 0xFFFFE4B5},
    {"navajowhite", 0xFFFFDEAD},
    {"navy", 0xFF000080},
    {"oldlace", 0xFFFDF5E6},
    {"olive", 0xFF808000},
    {"olivedrab", 0xFF6B8E23},
    {"orange", 0xFFFFA500},
    {"orangered", 0xFFFF4500},
    {"orchid", 0xFFDA70D6},
    {"palegoldenrod", 0xFFEEE8AA},
    {"palegreen", 0xFF98FB98},
    {"paleturquoise", 0xFFAFEEEE},
    {"palevioletred", 0xFFDB7093},
    {"papayawhip", 0xFFFFEFD5},
    {"peachpuff", 0xFFFFDAB9},
    {"peru", 0xFFCD853F},
    {"pink", 0xFFFFC0CB},
    {"plum", 0xFFDDA0DD},
    {"powderblue", 0xFFB0E0E6},
    {"purple", 0xFF800080},
    {"rebeccapurple", 0xFF663399},
    {"red", 0xFFFF0000},
    {"rosybrown", 0xFFBC8F8F},
    {"royalblue", 0xFF4169E1},
    {"saddlebrown", 0xFF8B4513},
    {"salmon", 0xFFFA8072},
    {"sandybrown", 0xFFF4A460},
    {"seagreen", 0xFF2E8B57},
    {"seashell", 0xFFFFF5EE},
    {"sienna", 0xFFA0522D},
    {"silver", 0xFFC0C0C0},
    {"skyblue", 0xFF87CEEB},
    {"slateblue", 0xFF6A5ACD},
    {"slategray", 0xFF708090},
    {"slategrey", 0xFF708090},
    {"snow", 0xFFFFFAFA},
    {"springgreen", 0xFF00FF7F},
    {"steelblue", 0xFF4682B4},
    {"tan", 0xFFD2B48C},
    {"teal", 0xFF008080},
    {"thistle", 0xFFD8BFD8},
    {"tomato", 0xFFFF6347},
    {"turquoise", 0xFF40E0D0},
    {"violet", 0xFFEE82EE},
    {"wheat", 0xFFF5DEB3},
    {"white", 0xFFFFFFFF},
    {"whitesmoke", 0xFFF5F5F5},
    {"yellow", 0xFFFFFF00},
    {"yellowgreen", 0xFF9ACD32},

    /* extra (not in official named colors but often useful) */
    {"transparent", 0x00000000},

    {NULL, 0}
};


// Simple busy wait delay function
static inline void simple_delay(unsigned int ms)
{
    for (volatile unsigned int i = 0; i < ms * 1000; i++) {
        // Do nothing, just wait
    }
}

void status_timer_callback(void *arg);
void show_status_message(pauk_ui_t* pauk_ui, const char* message, int duration_ms);
void stop_status_timer(pauk_ui_t *pauk_ui);
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

 void save_debug_snapshot(pauk_ui_t *pauk_ui);

 void run_ui(pauk_ui_t *pauk_ui);

 void test_simple_text(pauk_ui_t *pauk_ui);

 void render_ttf_text(pauk_ui_t *pauk_ui, const char *text, int x, int y,
    html_font_t *font, float size, gfx_color_t *color);
    void render_multiline_text(pauk_ui_t *pauk_ui, const char *text, int x, int y, 
        html_font_t *font, int font_size, uint32_t color,int line_height);

    float roundf(float value);

    int get_content_height_from_json(void) ;
    pixelmap_t* create_virtual_pixelmap(int width, int height);
    void copy_virtual_pixelmap_to_bitmap(pauk_ui_t* pauk_ui);
    void pixelmap_to_bitmap_copy(pauk_ui_t* pauk_ui);


    //mis
    extern void wnd_pos_event(ui_window_t *window, void *arg, pos_event_t *event);
   // HOVER
   void start_hover_timer(pauk_ui_t *pauk_ui, int interval_ms) ;
   void hover_timer_callback(void *arg);
   void stop_hover_timer(pauk_ui_t *pauk_ui);


   void get_page_dimensions(cJSON *element, int *max_x, int *max_y);

   void go_button_clicked(ui_pbutton_t *pbutton, void *arg);
   void bookmark_clicked(ui_menu_entry_t *mentry, void *arg);
   void navigate_forward(ui_pbutton_t *pbutton, void *arg);
   void refresh_page(ui_pbutton_t *pbutton, void *arg);
   void navigate_back(ui_pbutton_t *pbutton, void *arg);
   void bookmark_button_clicked(ui_pbutton_t *pbutton, void *arg);

   void init_navigation_history(pauk_ui_t *pauk_ui);
   void add_to_history(pauk_ui_t *pauk_ui, const char *url);
   void build_search_url(const char *engine, const char *query, char *url_buffer, size_t buffer_size);
   void search_button_clicked(ui_pbutton_t *pbutton, void *arg);


   void pauk_inicijalizuj_rute(void);
#endif // GUI
