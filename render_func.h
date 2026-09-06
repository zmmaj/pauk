
#ifndef RENDER_FUNC_H
#define RENDER_FUNC_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <str.h>
#include <ctype.h>
#include <io/pixelmap.h>
#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>
#include <gfx/color.h>
#include <gfx/font.h>
#include <gfx/typeface.h>
#include <gfx/coord.h>
#include <gfximage/tga.h>

#include "cjson.h"
#include "main.h"
#include "gui.h"
#include "gui.h"

void clear_area_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height,
     const char *css_color_str);
// Color conversion (implement elsewhere)
errno_t css_color_to_srbinos_color(const char *css_color_str, gfx_color_t **gfx_color_out);

uint32_t css_color_to_uint32(const char *color_str);
uint32_t css_color_to_argb(const char *css_color_str);

void draw_div_to_pixelmap(pixelmap_t* pixmap, int x, int y, int width, int height);
void draw_image_placeholder(pixelmap_t* pixmap, int x, int y, int width, int height);
void draw_filled_box_to_pixelmap(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t color);
void draw_box_border(pauk_ui_t *pauk_ui, int bx, int by, int width, int height,
    int border_w, uint32_t border_color,
    int radius, const char *style);
void draw_bottom_border(pauk_ui_t *pauk_ui, int bx, int by, int width, int height,
        int border_w, uint32_t border_color);
void render_ttf_text_to_pixelmap(pauk_ui_t *pauk_ui, const char *text, int x, int y,
        html_font_t *font, float size, uint32_t color_argb, int is_underlined,int is_linethrough);


uint32_t parse_color_string(const char* color_str);
void extract_and_draw_element(pauk_ui_t* pauk_ui, cJSON* element, int parent_x, int parent_y);

void draw_scroll_indicator(pauk_ui_t* pauk_ui);
uint32_t get_tag_color(const char* tag);
//int render_json_with_real_text(pauk_ui_t* pauk_ui, cJSON* element, int parent_x, int parent_y);
int render_json_direct(pauk_ui_t* pauk_ui, cJSON* element, cJSON* parent,
    int offset_x, int offset_y, int scroll_y);
void test_text_rendering(pauk_ui_t* pauk_ui);

const char* daj_element_text(cJSON *element);

// =============== MIS ===========================
cJSON* find_element_at_position(cJSON* element, int x, int y);
void handle_element_click(pauk_ui_t* pauk_ui, cJSON* element, int button);
void check_hover(pauk_ui_t* pauk_ui);
cJSON* get_hover_target(cJSON* elem, cJSON* root);


//crtanje slike
void draw_pixel_to_pixelmap(pauk_ui_t* pauk_ui, int x, int y, uint32_t color);


const char* get_or_create_png(const char *src_path, int target_width, int target_height);
int parse_sizes(const char *sizes, int viewport_width);
const char* select_from_srcset(const char *srcset, int selected_width);

cJSON* find_parent_form(cJSON* root, cJSON* element);
void submit_form(pauk_ui_t* pauk_ui, cJSON* form);

int validate_form(cJSON* form, pauk_ui_t* pauk_ui);

unsigned char* convert_svg_to_rgb(const char *svg_text, int w, int h);
unsigned char* decode_base64_data(const unsigned char *input, size_t input_len, size_t *out_len);
unsigned char* render_svg_string_to_pixels(const char* svg_str, int width, int height);

#endif // RENDER_FUNC_H
