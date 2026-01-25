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


#include "render_func.h"


/**
 * @brief Universal CSS to HelenOS color converter
 * Used by ALL rendering functions (bitmaps, GFX, text, shapes)
 */
errno_t css_color_to_helenos_color(const char *css_color_str, gfx_color_t **gfx_color_out) {
    if (!css_color_str || !gfx_color_out) return EINVAL;
    
    // 1. Convert CSS to 32-bit ARGB using your existing function
    uint32_t argb = css_color_to_uint32(css_color_str);
    
    // 2. Extract components
    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    
    // 3. Handle transparency (blend with white if needed)
    if (a < 255) {
        // Alpha blend with white background for HelenOS
        uint16_t bg = 255;
        uint16_t alpha = a;
        uint16_t inv_alpha = 255 - a;
        
        r = (r * alpha + bg * inv_alpha) / 255;
        g = (g * alpha + bg * inv_alpha) / 255;
        b = (b * alpha + bg * inv_alpha) / 255;
    }
    
    // 4. Convert to HelenOS 16-bit format (0-65535)
    uint16_t r16 = r * 257;  // 65535/255 = 257
    uint16_t g16 = g * 257;
    uint16_t b16 = b * 257;
    
    // 5. Create HelenOS color
    return gfx_color_new_rgb_i16(r16, g16, b16, gfx_color_out);
}

/**
 * @brief Universal CSS to 32-bit ARGB converter (for direct pixel manipulation)
 * Used when you write directly to bitmap pixels
 */
uint32_t css_color_to_argb(const char *css_color_str) {
    // Simply use your existing function
    return css_color_to_uint32(css_color_str);
}



/**
 * @brief Draw line using CSS color (implemented as thin rectangle)
 */
void draw_line_css(pauk_ui_t *pauk_ui, int x1, int y1, int x2, int y2,
    int thickness, const char *css_color_str) {
if (!pauk_ui || !css_color_str) return;

// APPLY SCROLL to BOTH Y coordinates
int scrolled_y1 = y1 - pauk_ui->scroll_y;
int scrolled_y2 = y2 - pauk_ui->scroll_y;

//uint32_t color = css_color_to_uint32(css_color_str);

// Use filled rectangle for line (your approach)
if (abs(x2 - x1) > abs(y2 - y1)) {
// Mostly horizontal
int start_x = (x1 < x2) ? x1 : x2;
int end_x = (x1 > x2) ? x1 : x2;
int length = end_x - start_x;
int center_y = (scrolled_y1 + scrolled_y2) / 2;

draw_filled_box_css(pauk_ui, start_x, center_y - thickness/2,
             length, thickness, css_color_str);
} else {
// Mostly vertical
int start_y = (scrolled_y1 < scrolled_y2) ? scrolled_y1 : scrolled_y2;
int end_y = (scrolled_y1 > scrolled_y2) ? scrolled_y1 : scrolled_y2;
int length = end_y - start_y;
int center_x = (x1 + x2) / 2;

draw_filled_box_css(pauk_ui, center_x - thickness/2, start_y,
             thickness, length, css_color_str);
}
}

/**
* @brief Draw filled box with CSS color string
*/
void draw_filled_box_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height, 
    const char *css_color_str) {
if (!pauk_ui || !css_color_str) return;

// APPLY SCROLL: y - scroll_y
int scrolled_y = y - pauk_ui->scroll_y;

// Skip if completely outside view
int view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
if (scrolled_y + height < 0 || scrolled_y >= view_height) {
return; // Not visible
}

uint32_t color = css_color_to_uint32(css_color_str);
draw_filled_box_pixelmap(pauk_ui, x, scrolled_y, width, height, color);
}

/**
* @brief Draw box border with CSS color string
*/
void draw_box_border_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height,
    const char *css_color_str) {
if (!pauk_ui || !css_color_str) return;

// APPLY SCROLL
int scrolled_y = y - pauk_ui->scroll_y;

int view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
if (scrolled_y + height < 0 || scrolled_y >= view_height) {
return;
}

uint32_t color = css_color_to_uint32(css_color_str);
draw_box_border(pauk_ui, x, scrolled_y, width, height, color);
}

/**
* @brief Render TTF text with CSS color string
*/
void render_ttf_text_css(pauk_ui_t *pauk_ui, const char *text, int x, int y,
    html_font_t *font, float size, const char *css_color_str) {
if (!pauk_ui || !text || !css_color_str || !font) return;

// APPLY SCROLL: y - scroll_y
int scrolled_y = y - pauk_ui->scroll_y;

// Skip if completely outside view
int view_height = pauk_ui->list_rect.p1.y - pauk_ui->list_rect.p0.y;
int approx_height = (int)size;
if (scrolled_y + approx_height < 0 || scrolled_y >= view_height) {
return; // Not visible
}

// Convert color
gfx_color_t *color = NULL;
if (css_color_to_helenos_color(css_color_str, &color) == EOK && color) {
// Draw at SCROLLED position
render_ttf_text(pauk_ui, text, x, scrolled_y, font, size, color);
gfx_color_delete(color);
}
}

/**
* @brief Clear area with CSS color
*/
void clear_area_css(pauk_ui_t *pauk_ui, int x, int y, int width, int height,
     const char *css_color_str) {
draw_filled_box_css(pauk_ui, x, y, width, height, css_color_str);
}



uint32_t css_color_to_uint32(const char *color_str) {
    if (!color_str) return 0xFF000000; // Black or default
    
    size_t len = strlen(color_str);
    
    // Trim whitespace
    while (len > 0 && isspace(color_str[len-1])) len--;
    
    // 1. Check for Named color (case-insensitive)
    for (const css_named_color_t *nc = css_named_colors; nc->name != NULL; nc++) {
        if (str_ncasecmp(color_str, nc->name, len) == 0 && strlen(nc->name) == len) {
           // printf("  [COLOR] Named color '%s' -> 0x%08X\n", nc->name, nc->value);
            return nc->value;
        }
    }
    
    // 2. Check for Hex color (#...)
    if (color_str[0] == '#') {
        if (len == 4 || len == 7) {
            uint32_t color = parse_hex_color(color_str + 1, len - 1);
           // printf("  [COLOR] Hex color '%s' -> 0x%08X\n", color_str, color);
            return color;
        }
    }
    
    // 3. Check for rgb() functional notation
    if (str_ncasecmp(color_str, "rgb(", 4) == 0 && len > 4) {
        int r, g, b;
        // Try to parse rgb(r, g, b) format
        if (sscanf(color_str, "rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            sscanf(color_str, "rgb(%d, %d, %d)", &r, &g, &b) == 3) {
            
            // Clamp values
            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;
            
            uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
           // printf("  [COLOR] RGB color '%s' -> 0x%08X\n", color_str, color);
            return color;
        }
    }
    
    // 4. Check for rgba() functional notation
    if (str_ncasecmp(color_str, "rgba(", 5) == 0 && len > 5) {
        int r, g, b;
        float a;
        if (sscanf(color_str, "rgba(%d,%d,%d,%f)", &r, &g, &b, &a) == 4 ||
            sscanf(color_str, "rgba(%d, %d, %d, %f)", &r, &g, &b, &a) == 4) {
            
            // Clamp values
            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;
            a = (a < 0.0f) ? 0.0f : (a > 1.0f) ? 1.0f : a;
            
            uint8_t alpha = (uint8_t)(a * 255);
            uint32_t color = (alpha << 24) | (r << 16) | (g << 8) | b;
           //printf("  [COLOR] RGBA color '%s' -> 0x%08X\n", color_str, color);
            return color;
        }
    }
    
   // printf("  [COLOR] Unrecognized color format: '%s', defaulting to black\n", color_str);
    return 0xFF000000; // Default black if unrecognized
}

void draw_box_border(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t border_color) {
    if (!pauk_ui || !pauk_ui->html_renderer || !pauk_ui->html_renderer->content_bitmap)
        return;

    gfx_bitmap_alloc_t alloc;
    if (gfx_bitmap_get_alloc(pauk_ui->html_renderer->content_bitmap, &alloc) != EOK)
        return;

    uint32_t *pixels = (uint32_t *)alloc.pixels;
    int stride = alloc.pitch / 4;
    int bitmap_width = pauk_ui->html_renderer->view_width;
    int bitmap_height = pauk_ui->html_renderer->view_height;

    for (int px = x; px < x + width && px < bitmap_width; px++) {
        if (y >= 0 && y < bitmap_height)
            pixels[y * stride + px] = border_color;
    }

    for (int px = x; px < x + width && px < bitmap_width; px++) {
        int bottom_y = y + height - 1;
        if (bottom_y >= 0 && bottom_y < bitmap_height)
            pixels[bottom_y * stride + px] = border_color;
    }

    for (int py = y; py < y + height && py < bitmap_height; py++) {
        if (x >= 0 && x < bitmap_width)
            pixels[py * stride + x] = border_color;
    }

    for (int py = y; py < y + height && py < bitmap_height; py++) {
        int right_x = x + width - 1;
        if (right_x >= 0 && right_x < bitmap_width)
            pixels[py * stride + right_x] = border_color;
    }
}


void render_body_box(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t bg_color) {
    if(DEB_INFO) printf("🏠 Rendering BODY at bitmap-relative (%d,%d)\n", x, y);
 // defaultna velicina boxa.
    if (width <= 0) width = 580;
    if (height <= 0) height = 30;

    draw_filled_box_pixelmap(pauk_ui, x, y, width, height, bg_color);
    if(DEB_INFO) printf("✅ BODY box drawn\n");
}


void draw_filled_box_pixelmap(pauk_ui_t *pauk_ui, int x, int y, int width, int height, uint32_t color) {
    if (!pauk_ui || !pauk_ui->html_renderer || !pauk_ui->html_renderer->content_bitmap)
        return;

    if(DEB_BITMAP) printf("🎨 draw_filled_box_pixelmap: bitmap-relative (%d,%d) %dx%d color=0x%08X\n", x, y, width, height, color);

    gfx_bitmap_alloc_t alloc;
    if (gfx_bitmap_get_alloc(pauk_ui->html_renderer->content_bitmap, &alloc) != EOK) {
        if(DEB_BITMAP) printf("❌ Failed to get bitmap allocation\n");
        return;
    }

    uint32_t *pixels = (uint32_t *)alloc.pixels;
    int stride = alloc.pitch / 4;
    int bitmap_width = pauk_ui->html_renderer->view_width;
    int bitmap_height = pauk_ui->html_renderer->view_height;

    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = (x + width > bitmap_width) ? bitmap_width : x + width;
    int end_y = (y + height > bitmap_height) ? bitmap_height : y + height;

    for (int py = start_y; py < end_y; py++) {
        for (int px = start_x; px < end_x; px++) {
            pixels[py * stride + px] = color;
        }
    }

    if(DEB_BITMAP) printf("✅ Box drawn to bitmap\n");
}