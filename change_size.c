
#include "main.h"
#include "gui.h"
//#include "lua_config.h"
#include <ui/image.h>
#include <ui/list.h>
#include <ui/tab.h>
#include <ui/tabset.h>
#include <ui/scrollbar.h>
#include <ui/rbutton.h>
#include <ui/resource.h>

#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>
#include <gfx/color.h>
#include <gfx/font.h>
#include <gfx/typeface.h>
#include <gfx/coord.h>
#include <gfximage/tga.h>
#include "change_size.h"
//#include "images.h"

void change_size(ui_pbutton_t *pbutton, void *arg)
{
    pauk_ui_t *pauk_ui = (pauk_ui_t *)arg;
    gfx_rect_t win_rect;
    gfx_rect_t new_rect;
    
    pauk_ui->full_screen_status = !pauk_ui->full_screen_status;
    
    if (pauk_ui->full_screen_status) {
        ui_window_def_maximize(pauk_ui->window);
        ui_window_get_app_rect(pauk_ui->window, &win_rect);
        pauk_ui->win_rect_fullscreen = win_rect;
        pauk_ui->globX = (win_rect.p1.x - win_rect.p0.x) - 
                        (pauk_ui->win_rect_base.p1.x - pauk_ui->win_rect_base.p0.x);
        pauk_ui->globY = (win_rect.p1.y - win_rect.p0.y) - 
                        (pauk_ui->win_rect_base.p1.y - pauk_ui->win_rect_base.p0.y);
        
      //  printf("Window expanded by: X=%d, Y=%d\n", pauk_ui->globX, pauk_ui->globY);
   
        // RESIZE BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->resize_button));
        gfx_rect_t test_resize_rect;
        test_resize_rect.p0.x = 940 + pauk_ui->globX;
        test_resize_rect.p0.y = pauk_ui->current_y1-30;
        test_resize_rect.p1.x = pauk_ui->resize_button_rect.p1.x + pauk_ui->globX;
        test_resize_rect.p1.y = pauk_ui->current_y1-5;
        ui_pbutton_set_rect(pauk_ui->resize_button, &test_resize_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->resize_button));
        
        // GO BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->go_button));
        gfx_rect_t test_go_rect;
        test_go_rect.p0.x = pauk_ui->go_button_rect.p0.x + pauk_ui->globX;
        test_go_rect.p0.y = pauk_ui->current_y1;
        test_go_rect.p1.x = pauk_ui->go_button_rect.p1.x + pauk_ui->globX;
        test_go_rect.p1.y = pauk_ui->current_y1+25;
        ui_pbutton_set_rect(pauk_ui->go_button, &test_go_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->go_button));
        
        // BOOKMARK BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->bookmark_button));
        gfx_rect_t test_bookmark_rect;
        test_bookmark_rect.p0.x = pauk_ui->bookmark_button_rect.p0.x + pauk_ui->globX;
        test_bookmark_rect.p0.y = pauk_ui->current_y1;
        test_bookmark_rect.p1.x = pauk_ui->bookmark_button_rect.p1.x + pauk_ui->globX;
        test_bookmark_rect.p1.y = pauk_ui->current_y1+25;
        ui_pbutton_set_rect(pauk_ui->bookmark_button, &test_bookmark_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->bookmark_button));
        
        // SEARCH BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->search_button));
        gfx_rect_t test_search_button_rect;
        test_search_button_rect.p0.x = pauk_ui->search_button_rect.p0.x + pauk_ui->globX;
        test_search_button_rect.p0.y = pauk_ui->current_y2;
        test_search_button_rect.p1.x = pauk_ui->search_button_rect.p1.x + pauk_ui->globX;
        test_search_button_rect.p1.y = pauk_ui->current_y2+25;
        ui_pbutton_set_rect(pauk_ui->search_button, &test_search_button_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->search_button));

        // ADRESS ENTRY  
        ui_fixed_remove(pauk_ui->fixed, ui_entry_ctl(pauk_ui->address_entry));
        gfx_rect_t test_entry_rect;
        test_entry_rect.p0.x = CONTENT_MARGIN + 170;
        test_entry_rect.p0.y = pauk_ui->current_y1;
        test_entry_rect.p1.x = pauk_ui->address_entry_rect.p1.x + pauk_ui->globX;
        test_entry_rect.p1.y = pauk_ui->current_y1 + ENTRY_HEIGHT;
        ui_entry_set_rect(pauk_ui->address_entry, &test_entry_rect);
        ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->address_entry));
   
        // SEARCH ENTRY
        ui_fixed_remove(pauk_ui->fixed, ui_entry_ctl(pauk_ui->search_entry));
        gfx_rect_t test_search_rect;
        test_search_rect.p0.x = CONTENT_MARGIN + 170;
        test_search_rect.p0.y = pauk_ui->current_y2;
        test_search_rect.p1.x = pauk_ui->search_entry_rect.p1.x + pauk_ui->globX;
        test_search_rect.p1.y = pauk_ui->current_y2 + ENTRY_HEIGHT;
        ui_entry_set_rect(pauk_ui->search_entry, &test_search_rect);
        ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->search_entry));
    
        // STATUS LABEL
        ui_fixed_remove(pauk_ui->fixed, ui_label_ctl(pauk_ui->status_label));
        new_rect = pauk_ui->status_label_base_rect;
        new_rect.p0.y += pauk_ui->globY;
        new_rect.p1.y += pauk_ui->globY;
        new_rect.p1.x += pauk_ui->globX;
        ui_label_set_rect(pauk_ui->status_label, &new_rect);
        ui_fixed_add(pauk_ui->fixed, ui_label_ctl(pauk_ui->status_label));
    
        // TABSET - Expand
        ui_fixed_remove(pauk_ui->fixed, ui_tab_set_ctl(pauk_ui->tabset));
        new_rect = pauk_ui->tab_rect_base;
        new_rect.p1.x += pauk_ui->globX;
        new_rect.p1.y += pauk_ui->globY;
        ui_tab_set_set_rect(pauk_ui->tabset, &new_rect);
        ui_fixed_add(pauk_ui->fixed, ui_tab_set_ctl(pauk_ui->tabset));
    
        // LIST inside tab - Expand
        new_rect = pauk_ui->list_rect_base;
        new_rect.p1.x += pauk_ui->globX;
        new_rect.p1.y += pauk_ui->globY;
        pauk_ui->list_rect = new_rect;
        
        // Simply update the display rectangle - bitmap stays the same
        if (pauk_ui->content_image) {
            ui_image_set_rect(pauk_ui->content_image, &pauk_ui->list_rect);
           // printf("✅ Display area expanded to fullscreen\n");
        }
               
    // UPDATE SCROLLBAR POSITION AND SIZE FOR FULLSCREEN
    if (pauk_ui->vscrollbar) {
        gfx_rect_t scrollbar_rect = {
            .p0 = { 
                pauk_ui->list_rect.p1.x - 20,     // 20px from right edge of expanded content
                pauk_ui->list_rect.p0.y           // Same top as expanded content
            },
            .p1 = { 
                pauk_ui->list_rect.p1.x,          // Right edge of expanded content
                pauk_ui->list_rect.p1.y           // Bottom of expanded content
            }
        };
        ui_scrollbar_set_rect(pauk_ui->vscrollbar, &scrollbar_rect);
        
        // UPDATE THUMB SIZE for fullscreen
        gfx_coord_t view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
        gfx_coord_t thumb_length = view_height / 3;
        if (thumb_length < 20) thumb_length = 20;
        if (thumb_length > view_height - 20) thumb_length = view_height - 20;
        ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, thumb_length);
        
        if (DEB_INIT_SCROLLBAR) { 
            printf("✅ Scrollbar repositioned for fullscreen - thumb: %d, view: %d\n", 
                   thumb_length, view_height);
        }
    }
    
            
            // Update UI image rectangle
            if (pauk_ui->content_image) {
                ui_image_set_rect(pauk_ui->content_image, &pauk_ui->list_rect);
               // printf("✅ UI image rectangle updated for fullscreen\n");
            }


       ui_window_paint(pauk_ui->window);
       gfx_update(pauk_ui->gc);

    } else {
        // RESTORE NORMAL SIZE
        ui_window_def_unmaximize(pauk_ui->window);
        
        // Reset expansion factors
        pauk_ui->globX = 0;
        pauk_ui->globY = 0;
        
        // RESIZE BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->resize_button));
        ui_pbutton_set_rect(pauk_ui->resize_button, &pauk_ui->resize_button_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->resize_button));
        
        // GO BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->go_button));
        ui_pbutton_set_rect(pauk_ui->go_button, &pauk_ui->go_button_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->go_button));
        
        // BOOKMARK BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->bookmark_button));
        ui_pbutton_set_rect(pauk_ui->bookmark_button, &pauk_ui->bookmark_button_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->bookmark_button));
        
        // SEARCH BUTTON
        ui_fixed_remove(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->search_button));
        ui_pbutton_set_rect(pauk_ui->search_button, &pauk_ui->search_button_rect);
        ui_fixed_add(pauk_ui->fixed, ui_pbutton_ctl(pauk_ui->search_button));
        
        // ADRESS ENTRY
        ui_fixed_remove(pauk_ui->fixed, ui_entry_ctl(pauk_ui->address_entry));
        ui_entry_set_rect(pauk_ui->address_entry, &pauk_ui->address_entry_rect);
        ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->address_entry));
        
        // SEARCH ENTRY
        ui_fixed_remove(pauk_ui->fixed, ui_entry_ctl(pauk_ui->search_entry));
        ui_entry_set_rect(pauk_ui->search_entry, &pauk_ui->search_entry_rect);
        ui_fixed_add(pauk_ui->fixed, ui_entry_ctl(pauk_ui->search_entry));
        
        // STATUS LABEL
        ui_fixed_remove(pauk_ui->fixed, ui_label_ctl(pauk_ui->status_label));
        ui_label_set_rect(pauk_ui->status_label, &pauk_ui->status_label_base_rect);
        ui_fixed_add(pauk_ui->fixed, ui_label_ctl(pauk_ui->status_label));
        
        // TABSET - Restore to base
        ui_fixed_remove(pauk_ui->fixed, ui_tab_set_ctl(pauk_ui->tabset));
        ui_tab_set_set_rect(pauk_ui->tabset, &pauk_ui->tab_rect_base);
        ui_fixed_add(pauk_ui->fixed, ui_tab_set_ctl(pauk_ui->tabset));
        
        // LIST inside tab - Restore to base
        
        pauk_ui->list_rect=pauk_ui->list_rect_base;

     // Restore display rectangle
     if (pauk_ui->content_image) {
        ui_image_set_rect(pauk_ui->content_image, &pauk_ui->list_rect_base);
        printf("✅ Display area restored to normal\n");
    }


 // RESTORE SCROLLBAR POSITION AND SIZE TO NORMAL
 if (pauk_ui->vscrollbar) {
    gfx_rect_t scrollbar_rect = {
        .p0 = { 
            pauk_ui->list_rect_base.p1.x - 20,     // 20px from right edge of normal content
            pauk_ui->list_rect_base.p0.y           // Same top as normal content
        },
        .p1 = { 
            pauk_ui->list_rect_base.p1.x,          // Right edge of normal content
            pauk_ui->list_rect_base.p1.y           // Bottom of normal content
        }
    };
    ui_scrollbar_set_rect(pauk_ui->vscrollbar, &scrollbar_rect);
    
    // RESTORE THUMB SIZE to normal
    gfx_coord_t view_height = pauk_ui->list_rect_base.p1.y - pauk_ui->list_rect_base.p0.y;
    gfx_coord_t thumb_length = view_height / 3;
    if (thumb_length < 20) thumb_length = 20;
    if (thumb_length > view_height - 20) thumb_length = view_height - 20;
    ui_scrollbar_set_thumb_length(pauk_ui->vscrollbar, thumb_length);
    
    if (DEB_INIT_SCROLLBAR) { 
        printf("✅ Scrollbar restored to normal - thumb: %d, view: %d\n", 
               thumb_length, view_height);
    }
}

// Update UI image rectangle back
if (pauk_ui->content_image) {
    ui_image_set_rect(pauk_ui->content_image, &pauk_ui->list_rect_base);
   // printf("✅ UI image rectangle restored to normal\n");
}

        ui_window_paint(pauk_ui->window);
        gfx_update(pauk_ui->gc);
    }
    
    fibril_usleep(1000);
    ui_window_paint(pauk_ui->window);
     gfx_update(pauk_ui->gc);
   // printf("UI resized successfully\n");
}
