// forms_parser.c - Add these functions to your existing file

#include "forms_parser.h"
#include "render_func.h"
#include "layout_engine.h"
#include "rendering_elements/defaults.h"

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "forms_parser.h"
#include "main.h"
#include "layout_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== HELPER FUNCTION ====================

static char* get_attr_value(lxb_dom_element_t *elem, const char *attr_name) {
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(elem, 
                               (lxb_char_t*)attr_name, strlen(attr_name));
    if (!attr) return NULL;
    
    size_t len;
    const lxb_char_t *value = lxb_dom_attr_value(attr, &len);
    if (!value || len == 0) return NULL;
    
    return lexbor_to_cstr(value, len);
}

// ==================== FORM ELEMENT INITIALIZATION ====================

void init_form_element(cJSON *form_json, lxb_dom_element_t *elem) {
    if (!form_json) return;
    
    set_json_string(form_json, "display", "block");
    set_json_bool(form_json, "is_form", 1);
    set_json_number(form_json, "margin_bottom", 20);
    set_json_number(form_json, "padding", 15);
    set_json_string(form_json, "bg_color", "#fafafa");
    set_json_string(form_json, "border_style", "solid");
    set_json_number(form_json, "border_width", 1);
    set_json_string(form_json, "border_color", "#e0e0e0");
    set_json_number(form_json, "border_radius", 5);
    
    char *method = get_attr_value(elem, "method");
    if (method) {
        set_json_string(form_json, "form_method", method);
        free(method);
    }
    
    char *action = get_attr_value(elem, "action");
    if (action) {
        set_json_string(form_json, "form_action", action);
        free(action);
    }
}

void init_input_element(cJSON *input_json, lxb_dom_element_t *elem) {
    if (!input_json) return;
    
    char *type_str = get_attr_value(elem, "type");
    const char *type = type_str ? type_str : "text";
    
    set_json_string(input_json, "display", "inline-block");
    set_json_string(input_json, "input_type", type);
    set_json_bool(input_json, "is_form_element", 1);
    set_json_bool(input_json, "is_input", 1);
    
    if (strcmp(type, "color") == 0) {
        set_json_number(input_json, "width", 60);
        set_json_number(input_json, "height", 40);
    }
    else if (strcmp(type, "date") == 0) {
        set_json_number(input_json, "width", 150);
        set_json_number(input_json, "height", 35);
        set_json_string(input_json, "placeholder", "YYYY-MM-DD");
    }
    else if (strcmp(type, "range") == 0) {
        set_json_number(input_json, "width", 200);
        set_json_number(input_json, "height", 30);
        
        char *min_str = get_attr_value(elem, "min");
        set_json_number(input_json, "min", min_str ? atoi(min_str) : 0);
        if (min_str) free(min_str);
        
        char *max_str = get_attr_value(elem, "max");
        set_json_number(input_json, "max", max_str ? atoi(max_str) : 100);
        if (max_str) free(max_str);
        
        set_json_number(input_json, "value", 50);
    }
    else if (strcmp(type, "file") == 0) {
        set_json_number(input_json, "width", 250);
        set_json_number(input_json, "height", 35);
        set_json_string(input_json, "button_text", "Browse...");
    }
    // 🚀 NOVI BLOK: Obrada za Google Submit i klasičnu dugmad
    else if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) {
        set_json_number(input_json, "width", 130);
        set_json_number(input_json, "height", 36);
        set_json_bool(input_json, "is_button", 1);
    }
    else {
        set_json_number(input_json, "width", 180);
        set_json_number(input_json, "height", 35);
    }
    
    set_json_number(input_json, "padding_top", 8);
    set_json_number(input_json, "padding_bottom", 8);
    set_json_number(input_json, "padding_left", 10);
    set_json_number(input_json, "padding_right", 10);
    set_json_string(input_json, "border_style", "solid");
    set_json_number(input_json, "border_width", 1);
    
    // Vizuelno prilagođavanje dugmadi u odnosu na običan input tekst boks
    if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) {
        set_json_string(input_json, "border_color", "#70757a");  // Tamno siva - VIDLJIVO
        set_json_number(input_json, "border_width", 1);
        set_json_number(input_json, "border_radius", 4);
        set_json_string(input_json, "bg_color", "#f8f9fa");  // Svetlo siva pozadina
    } else {
        set_json_string(input_json, "border_color", "#70757a");  // Tamno siva - VIDLJIVO
        set_json_number(input_json, "border_width", 1);
        set_json_number(input_json, "border_radius", 4);
        set_json_string(input_json, "bg_color", "#ffffff");  // Bela pozadina
    }
    
    // ===== OBRADA ATRIBUTA =====
    char *name = get_attr_value(elem, "name");
    if (name) {
        set_json_string(input_json, "input_name", name);
        
        // ===== DETEKCIJA POLJA ZA PRETRAGU (OPŠTI PRISTUP) =====
        const char *input_type = get_json_string(input_json, "input_type", "text");
        int is_search = 0;
        
        if (strcmp(input_type, "search") == 0) {
            is_search = 1;
        }
        else if (strcmp(input_type, "text") == 0) {
            char *name_lower = strdup(name);
            if (name_lower) {
                for (int i = 0; name_lower[i]; i++) {
                    name_lower[i] = tolower(name_lower[i]);
                }
                
                if (strstr(name_lower, "q") != NULL ||
                    strstr(name_lower, "search") != NULL ||
                    strstr(name_lower, "s") != NULL) {
                    is_search = 1;
                }
                free(name_lower);
            }
        }
        
        if (is_search) {
            set_json_bool(input_json, "is_search_input", 1);
            if (INFO_MESSAGES_JS) {
                printf("🔍 [Forms] Detektovano polje za pretragu: name='%s', type='%s'\n", name, input_type);
            }
        }
        free(name);
    }
    
    // 🚀 POPRAVKA: Ako element nema value (kao polje 'q'), prisilno čistimo 
    // registre kako ne bi povukao token sesije iz prethodnog skrivenog polja!
    char *value = get_attr_value(elem, "value");
    if (value) {
        set_json_string(input_json, "input_value", value);
        set_json_string(input_json, "value", value);
        free(value);
    } else {
        set_json_string(input_json, "input_value", "");
        set_json_string(input_json, "value", "");
    }
    
    char *placeholder = get_attr_value(elem, "placeholder");
    if (placeholder) {
        set_json_string(input_json, "placeholder", placeholder);
        free(placeholder);
    }

    // Parse required attribute (boolean)
    lxb_dom_attr_t *required_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"required", 8);
    if (required_attr) {
        set_json_bool(input_json, "required", 1);
        if (INFO_MESSAGES_JS) {
            printf("📝 Input '%s' has required attribute\n", 
                   get_json_string(input_json, "input_name", "unknown"));
        }
    } else {
        set_json_bool(input_json, "required", 0);
    }
    
    if (type_str) free(type_str);
}


// Simplified datalist - no complex Lexbor traversal
void init_datalist_element(cJSON *datalist_json, lxb_dom_element_t *elem) {
    if (!datalist_json) return;
    
    set_json_string(datalist_json, "display", "none");
    set_json_bool(datalist_json, "is_datalist", 1);
    set_json_bool(datalist_json, "hidden", 1);
    cJSON_AddItemToObject(datalist_json, "datalist_options", cJSON_CreateArray());
}

void init_output_element(cJSON *output_json, lxb_dom_element_t *elem) {
    if (!output_json) return;
    
    set_json_string(output_json, "display", "inline");  // Not inline-block
    set_json_bool(output_json, "is_form_element", 1);
    set_json_bool(output_json, "is_output", 1);
    set_json_string(output_json, "value", "0");
    
    char *name = get_attr_value(elem, "name");
    if (name) {
        set_json_string(output_json, "output_name", name);
        free(name);
    }
}

// ==================== RENDERING FUNCTIONS ====================

void render_form_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font) {
    int width = get_json_number(element, "width", 0);
    int height = get_json_number(element, "height", 0);
    const char *bg_color = get_json_string(element, "bg_color", "#fafafa");
    
    // Use parse_hex_color instead of color_from_hex
    draw_filled_box_to_pixelmap(pauk_ui, x, y, width, height, 
                                parse_hex_color(bg_color + 1, strlen(bg_color + 1)));
    draw_box_border(pauk_ui, x, y, width, height, 1, 
                    parse_hex_color("e0e0e0", 6), 5, "solid");
}

void render_input_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font) {
    const char *type = get_json_string(element, "input_type", "text");
  //preskoci renderovanje za hidden polja
    if (strcmp(type, "hidden") == 0) {
        return; 
    }
  
  
    int width = get_json_number(element, "width", 150);
    int height = get_json_number(element, "height", 35);
    const char *bg_color = get_json_string(element, "bg_color", "#ffffff");
    
    // Draw input background
    draw_filled_box_to_pixelmap(pauk_ui, x, y, width, height, 
                                parse_hex_color(bg_color + 1, strlen(bg_color + 1)));
    
    // REMOVED the first draw_box_border - will draw at the end
    
    if (strcmp(type, "color") == 0) {
        const char *value = get_json_string(element, "input_value", "#000000");
        draw_filled_box_to_pixelmap(pauk_ui, x + 5, y + 5, width - 10, height - 10, 
                                    parse_hex_color(value + 1, strlen(value + 1)));
    }
    else if (strcmp(type, "date") == 0) {
        const char *placeholder = get_json_string(element, "placeholder", "YYYY-MM-DD");
        if (font) {
            int text_y = y + (DEFAULT_FONT_SIZE/2);
            render_ttf_text_to_pixelmap(pauk_ui, placeholder, x + 10, text_y, 
                                        font, DEFAULT_FONT_SIZE, parse_hex_color("999999", 6), 0, 0);
        }
        // Draw calendar icon
        draw_box_border(pauk_ui, x + width - 25, y + 8, 18, 18, 1, 
                        parse_hex_color("cccccc", 6), 3, "solid");
        draw_filled_box_to_pixelmap(pauk_ui, x + width - 23, y + 10, 14, 3, 
                                    parse_hex_color("f0f0f0", 6));
    }
    else if (strcmp(type, "range") == 0) {
        int min = get_json_number(element, "min", 0);
        int max = get_json_number(element, "max", 100);
        int value = get_json_number(element, "value", 50);
        int track_y = y + height / 2 - 2;
        int track_width = width - 20;
        
        draw_filled_box_to_pixelmap(pauk_ui, x + 10, track_y, track_width, 4, 
                                    parse_hex_color("e0e0e0", 6));
        
        int fill_width = (value - min) * track_width / (max - min);
        if (fill_width > 0) {
            draw_filled_box_to_pixelmap(pauk_ui, x + 10, track_y, fill_width, 4, 
                                        parse_hex_color("3366cc", 6));
        }
        
        int thumb_x = x + 10 + fill_width;
        draw_filled_box_to_pixelmap(pauk_ui, thumb_x - 8, track_y - 6, 16, 16, 
                                    parse_hex_color("3366cc", 6));
        draw_filled_box_to_pixelmap(pauk_ui, thumb_x - 6, track_y - 4, 12, 12, 
                                    parse_hex_color("ffffff", 6));
    }
    else if (strcmp(type, "file") == 0) {
        const char *button_text = get_json_string(element, "button_text", "Browse...");
        
        draw_filled_box_to_pixelmap(pauk_ui, x + 5, y + 5, width - 90, height - 10, 
                                    parse_hex_color("fafafa", 6));
        draw_box_border(pauk_ui, x + 5, y + 5, width - 90, height - 10, 1, 
                        parse_hex_color("dddddd", 6), 3, "solid");
        
        if (font) {
            int text_y = y + (DEFAULT_FONT_SIZE/2);
            render_ttf_text_to_pixelmap(pauk_ui, "No file chosen", x + 10, text_y, 
                                        font, DEFAULT_FONT_SIZE, parse_hex_color("666666", 6), 0, 0);
        }
        
        int button_x = x + width - 80;
        draw_filled_box_to_pixelmap(pauk_ui, button_x, y + 5, 75, height - 10, 
                                    parse_hex_color("f0f0f0", 6));
        draw_box_border(pauk_ui, button_x, y + 5, 75, height - 10, 1, 
                        parse_hex_color("a0a0a0", 6), 3, "solid");
        
        if (font) {
            int text_y = y + (DEFAULT_FONT_SIZE/2);
            render_ttf_text_to_pixelmap(pauk_ui, button_text, button_x + 15, text_y, 
                                        font, DEFAULT_FONT_SIZE, parse_hex_color("000000", 6), 0, 0);
        }
    }
    else {
        // ===== TEXT INPUT - display current value =====
        const char *value = get_json_string(element, "input_value", "");
        const char *placeholder = get_json_string(element, "placeholder", "");
        
        // Get default font if none provided
        html_font_t *use_font = font;
        if (!use_font || !use_font->is_loaded) {
            use_font = font_manager_get_font(&pauk_ui->font_manager, 
                                              pauk_ui->font_manager.default_font_index);
        }
        
        // Draw the text value or placeholder
        if (use_font && use_font->is_loaded) {
            int text_y = y + (DEFAULT_FONT_SIZE/2);
            if (strlen(value) > 0) {
                render_ttf_text_to_pixelmap(pauk_ui, value, x + 10, text_y, 
                                            use_font, DEFAULT_FONT_SIZE, parse_hex_color("000000", 6), 0, 0);
            } else if (strlen(placeholder) > 0) {
                render_ttf_text_to_pixelmap(pauk_ui, placeholder, x + 10, text_y, 
                                            use_font, DEFAULT_FONT_SIZE, parse_hex_color("999999", 6), 0, 0);
            }
        }
    }
    
    // Draw border - ONLY ONCE, at the end
    int border_width = 1;
    uint32_t border_color = parse_hex_color("cccccc", 6);
    
    // If this element is focused, draw thicker border
    if (pauk_ui->focused_element == element) {
        border_width = 2;
        border_color = 0xFF3366CC;  // Blue highlight
    }
    
    draw_box_border(pauk_ui, x, y, width, height, border_width, border_color, 4, "solid");
}

void render_output_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font) {
    const char *value = get_json_string(element, "value", "0");
    
    if (font && value && strlen(value) > 0) {
        int text_y = y + (DEFAULT_FONT_SIZE / 2);
        render_ttf_text_to_pixelmap(pauk_ui, value, x, text_y, 
                                    font, DEFAULT_FONT_SIZE, parse_hex_color("000000", 6), 0, 0);
    }
}


void render_textarea_element(pauk_ui_t *pauk_ui, cJSON *element, int x, int y, html_font_t *font) {
    int width = get_json_number(element, "width", 400);
    int height = get_json_number(element, "height", 80);
    const char *placeholder = get_json_string(element, "placeholder", "");
    const char *value = get_json_string(element, "value", "");
    
    // Get default font if none provided
    html_font_t *use_font = font;
    if (!use_font || !use_font->is_loaded) {
        use_font = font_manager_get_font(&pauk_ui->font_manager, 
                                          pauk_ui->font_manager.default_font_index);
    }
    
    // Draw background
    draw_filled_box_to_pixelmap(pauk_ui, x, y, width, height, 0xFFFFFFFF);
    
    // Draw text
    if (use_font && use_font->is_loaded) {
        const char *display_text = (strlen(value) > 0) ? value : placeholder;
        uint32_t text_color = (strlen(value) > 0) ? 0xFF000000 : 0xFF999999;
        
        int line_height = DEFAULT_FONT_SIZE + 4;
        int max_lines = get_json_number(element, "textarea_rows", 4);
        int start_y = y + (DEFAULT_FONT_SIZE / 2);
        
        // Split by newlines only (no auto-wrap)
        char *text_copy = strdup(display_text);
        if (text_copy) {
            int current_y = start_y;
            int line_count = 0;
            char *line = strtok(text_copy, "\n");
            
            while (line && line_count < max_lines) {
                render_ttf_text_to_pixelmap(pauk_ui, line, x + 10, current_y, 
                                            use_font, DEFAULT_FONT_SIZE, text_color, 0, 0);
                current_y += line_height;
                line = strtok(NULL, "\n");
                line_count++;
            }
            free(text_copy);
        }
    }
    
    // Draw border - ONLY ONCE, after text
    int border_width = 1;
    uint32_t border_color = 0xFFCCCCCC;
    
    // If this element is focused, draw thicker border
    if (pauk_ui->focused_element == element) {
        border_width = 2;
        border_color = 0xFF3366CC;  // Blue highlight
    }
    
    draw_box_border(pauk_ui, x, y, width, height, border_width, border_color, 4, "solid");
}

// ==================== INTEGRATION FUNCTION ====================

void process_form_element(cJSON *elem_json, lxb_dom_element_t *elem, const char *tag) {
    if (strcmp(tag, "form") == 0) {
        init_form_element(elem_json, elem);
    }
    else if (strcmp(tag, "input") == 0) {
        init_input_element(elem_json, elem);
    }
    else if (strcmp(tag, "datalist") == 0) {
        init_datalist_element(elem_json, elem);
    }
    else if (strcmp(tag, "output") == 0) {
        init_output_element(elem_json, elem);
    }
}
