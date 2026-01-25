
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


void draw_line_css(pauk_ui_t *pauk_ui, int x1, int y1, int x2, int y2,
    int thickness, const char *css_color_str);
void draw_filled_box_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height, 
          const char *css_color_str);
          void draw_filled_box_pixelmap(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t color);
          void draw_box_border(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t border_color);
          void draw_box_border_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height,
          const char *css_color_str);
void render_ttf_text_css(pauk_ui_t *pauk_ui, const char *text, int x, int y,
          html_font_t *font, float size, const char *css_color_str);
void clear_area_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height,
     const char *css_color_str);
void test_css_rendering(pauk_ui_t *pauk_ui);
void render_body_box(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t bg_color);
// Color conversion (implement elsewhere)
errno_t css_color_to_helenos_color(const char *css_color_str, gfx_color_t **gfx_color_out);

uint32_t css_color_to_uint32(const char *color_str);
uint32_t css_color_to_argb(const char *css_color_str);

#endif // RENDER_FUNC_H