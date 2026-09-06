
#ifndef CSS_CURSOR_H
#define CSS_CURSOR_H

#include "gui.h"
#include <ui/ui.h>
#include <ui/window.h>
#include <ui/wdecor.h>


#ifdef __cplusplus
extern "C" {
#endif

void set_cursor(pauk_ui_t* pauk_ui, ui_stock_cursor_t cursor);
void update_cursor_for_element(pauk_ui_t* pauk_ui, cJSON* element);


#ifdef __cplusplus
}
#endif

#endif // CSS_CURSOR_H
