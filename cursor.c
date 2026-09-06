#include <ui/ui.h>
#include <ui/window.h>
#include <ui/wdecor.h>

#include "cjson.h"
#include "gui.h"
#include "cursor.h"

// Helper function to get string from cJSON
static const char* get_json_string(cJSON *obj, const char *key, const char *def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return def;
}

// Helper function to get boolean from cJSON
static bool get_json_bool(cJSON *obj, const char *key, bool def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return def;
}

void set_cursor(pauk_ui_t* pauk_ui, ui_stock_cursor_t cursor) {
    if (!pauk_ui || !pauk_ui->window) return;
    
    // Set cursor directly on the window
    ui_window_set_ctl_cursor(pauk_ui->window, cursor);
}

void update_cursor_for_element(pauk_ui_t* pauk_ui, cJSON* element) {
    ui_stock_cursor_t cursor = ui_curs_arrow;  // default
  
    if (element) {
        const char* tag = get_json_string(element, "tag", "");
        bool is_clickable = get_json_bool(element, "is_clickable", false);
    
        if (strcmp(tag, "a") == 0 || 
            strcmp(tag, "button") == 0 ||
            is_clickable) {
       
            cursor = ui_curs_size_ud;  // Or ui_curs_hand if available
        }
        else if (strcmp(tag, "input") == 0 || 
                 strcmp(tag, "textarea") == 0) {
            cursor = ui_curs_ibeam;
        }
    }
    
    set_cursor(pauk_ui, cursor);
}
