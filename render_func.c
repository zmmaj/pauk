#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <str.h>
#include <ctype.h>
#include <io/pixelmap.h>
#include <pixconv.h>
#include <gfx/bitmap.h>
#include <gfx/render.h>
#include <gfx/context.h>
#include <gfx/color.h>
#include <gfx/font.h>
#include <gfx/typeface.h>
#include <gfx/coord.h>
#include <gfximage/tga.h>
#include <nanosvgrast.h>
#include <nanosvg.h>
#include "cjson.h"
#include "main.h"
#include "gui.h"
#include "layout_engine.h"
#include "render_func.h"
#include "text_rules.h"
#include "cursor.h"
#include "layout_engine.h"
#include "buttons.h"
#include "forms_parser.h"
#include "tables_parser.h"
#include "image_cache.h"
#include "event_handler.h"
#include "url_utils.h"
#include "rendering_elements/defaults.h"

#include "stb_image.h"
#include "stb_image_write.h"



// Cursor type definitions (add after includes)
#define CURSOR_DEFAULT 0
#define CURSOR_POINTER 1
#define CURSOR_TEXT    2


static inline int imin(int a, int b)
{
    return a < b ? a : b;
}

static inline int imax(int a, int b)
{
    return a > b ? a : b;
}


// Helper: Get color for tag
uint32_t get_tag_color(const char* tag) {
    if (!tag) return 0xFFCCCCCC;
    
    if (strcmp(tag, "body") == 0) return 0xFFFFFFFF;
    if (strcmp(tag, "div") == 0) return 0xFFE6F2FF;   // Light blue
    if (strcmp(tag, "h1") == 0) return 0xFFFFE6CC;    // Light orange
    if (strcmp(tag, "h2") == 0) return 0xFFFFF0CC;    // Light yellow
    if (strcmp(tag, "img") == 0) return 0xFFF0F0F0;   // Light gray
    if (strcmp(tag, "form") == 0) return 0xFFE6FFE6;  // Light green
    if (strcmp(tag, "table") == 0) return 0xFFF8F8F8; // Very light gray
    
    return 0xFFCCCCCC + (rand() % 100 * 0x00010101); // Random pastel
}

/**
 * @brief Universal CSS to SrBinOS color converter
 * Used by ALL rendering functions (bitmaps, GFX, text, shapes)
 */
errno_t css_color_to_srbinos_color(const char *css_color_str, gfx_color_t **gfx_color_out) {
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

/**
 * Draw to pixelmap with BITMAP-RELATIVE coordinates
 * Coordinates are relative to bitmap position (list_rect)
 */
void draw_filled_box_to_pixelmap(pauk_ui_t *pauk_ui, int bx, int by, int width, int height, uint32_t color) {
    // bx, by are ABSOLUTE positions in the 15000-pixel tall document
    // Draw directly at these coordinates
    
    pixel_t *pixels = pauk_ui->virtual_pixmap->data;
    int pix_width = pauk_ui->virtual_pixmap->width;
    int pix_height = pauk_ui->virtual_pixmap->height;
    
    // Simple bounds checking
    if (by < 0 || by >= pix_height) {
      //  printf("    ⚠️  Warning: y=%d outside pixelmap (0-%d)\n", by, pix_height-1);
        return;
    }
    
    // Draw at absolute position
    for (int y = by; y < by + height && y < pix_height; y++) {
        for (int x = bx; x < bx + width && x < pix_width; x++) {
            pixels[y * pix_width + x] = (pixel_t)color;
        }
    }
}

void draw_box_border(pauk_ui_t *pauk_ui, int bx, int by, int width, int height,
    int border_w, uint32_t border_color,
    int radius, const char *style)
{
    if (border_w <= 0) return;
    
    // For now only support solid style
    if (strcmp(style, "solid") != 0) return;
    
    // Draw rectangle border (works for both radius=0 and radius>0)
    // top
    draw_filled_box_to_pixelmap(pauk_ui, bx, by, width, border_w, border_color);
    // bottom
    draw_filled_box_to_pixelmap(pauk_ui, bx, by + height - border_w, width, border_w, border_color);
    // left
    draw_filled_box_to_pixelmap(pauk_ui, bx, by, border_w, height, border_color);
    // right
    draw_filled_box_to_pixelmap(pauk_ui, bx + width - border_w, by, border_w, height, border_color);
    
    // TODO: implement rounded corners for radius > 0
}


void draw_bottom_border(pauk_ui_t *pauk_ui, int bx, int by, int width, int height,
    int border_w, uint32_t border_color)
{
if (border_w <= 0) return;

// Draw only the bottom border line
draw_filled_box_to_pixelmap(pauk_ui, 
            bx, 
            by + height - border_w, 
            width, 
            border_w, 
            border_color);
}

void render_ttf_text_to_pixelmap(pauk_ui_t *pauk_ui, const char *text, int x, int y,
    html_font_t *font, float size, uint32_t color_argb, int is_underlined, int is_linethrough)
{
    if (!pauk_ui || !pauk_ui->virtual_pixmap || !text || !font)
        return;

    int pix_width = pauk_ui->virtual_pixmap->width;
    int pix_height = pauk_ui->virtual_pixmap->height;

    if (y < 0 || y >= pix_height) return;

    pixel_t* pixels = pauk_ui->virtual_pixmap->data;

    html_font_t *use_font = font;
    if (!use_font || !use_font->is_loaded) {
        use_font = font_manager_get_font(&pauk_ui->font_manager, 
                    pauk_ui->font_manager.default_font_index);
        if (!use_font || !use_font->is_loaded) {
            printf("[TTF->Pixelmap] Font not loaded\n");
            return;
        }
    }

    stbtt_fontinfo *info = &use_font->info;

    // ========== SCALE CALCULATION ==========
    float scale = stbtt_ScaleForPixelHeight(&font->info, size);

    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
    
    // ========== AUTO-CORRECT BROKEN FONT ==========
    int expected_em = (ascent - descent);
    int real_em = 2048;
    
    if (expected_em < 1500) {
        float correction = (float)real_em / (float)expected_em;
        scale *= correction;
    }

    // ========== NORMALIZE BOLD FONT SIZE ==========
    if (font == &pauk_ui->font_manager.fonts[pauk_ui->font_manager.bold_font_index]) {
        html_font_t* regular_font = &pauk_ui->font_manager.fonts[pauk_ui->font_manager.default_font_index];
        float regular_scale = stbtt_ScaleForPixelHeight(&regular_font->info, size);
        float bold_scale = stbtt_ScaleForPixelHeight(&font->info, size);
        
        float target_scale = (bold_scale > regular_scale) ? bold_scale : regular_scale;
        scale = target_scale;
    }

    // ========== NORMALIZE ITALIC FONT SIZE ==========
    if (font == &pauk_ui->font_manager.fonts[pauk_ui->font_manager.italic_font_index]) {
        html_font_t* regular_font = &pauk_ui->font_manager.fonts[pauk_ui->font_manager.default_font_index];
        float regular_scale = stbtt_ScaleForPixelHeight(&regular_font->info, size);
        scale = regular_scale;
    }

    int baseline = y + (int)roundf(ascent * scale);
    int pen_x = x;

    // Extract ARGB components
    uint8_t a = (color_argb >> 24) & 0xFF;
    uint8_t r = (color_argb >> 16) & 0xFF;
    uint8_t g = (color_argb >> 8) & 0xFF;
    uint8_t b = color_argb & 0xFF;

    // ========== UTF-8 DECODER FUNCTION ==========
    // Converts pointer to next UTF-8 character and returns codepoint
    int utf8_next(const char **ptr) {
        const unsigned char *p = (const unsigned char*)*ptr;
        int codepoint = 0;
        
        if (p[0] < 0x80) {
            // 1-byte (ASCII)
            codepoint = p[0];
            *ptr += 1;
        }
        else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            // 2-byte
            codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            *ptr += 2;
        }
        else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            // 3-byte (e.g., most math symbols: ∮, ∑, →)
            codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            *ptr += 3;
        }
        else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && 
                 (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            // 4-byte (rare, but handle anyway)
            codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | 
                        ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            *ptr += 4;
        }
        else {
            // Invalid UTF-8, skip one byte and use replacement character
            *ptr += 1;
            return 0xFFFD; // Unicode replacement character (�)
        }
        
        return codepoint;
    }
    // ===============================================

    const char *p = text;
    
    while (*p) {
        // Get the next Unicode codepoint
        int code_point = utf8_next(&p);
        if (code_point <= 0) continue;  // Skip invalid
        
        // Get glyph index (better than using codepoint directly)
        int glyph_index = stbtt_FindGlyphIndex(info, code_point);
        if (glyph_index == 0) {
            // Glyph not found in font, try fallback
            if (code_point != 0xFFFD) {
                // Try replacement character
                glyph_index = stbtt_FindGlyphIndex(info, 0xFFFD);
                if (glyph_index == 0) {
                    // Fallback to '?'
                    glyph_index = stbtt_FindGlyphIndex(info, '?');
                }
            }
            if (glyph_index == 0) continue;
        }
        
        // Get glyph metrics using glyph index
        int advance, lsb;
        stbtt_GetGlyphHMetrics(info, glyph_index, &advance, &lsb);
        
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(info, glyph_index, scale, scale, &x0, &y0, &x1, &y1);
        
        int w = x1 - x0;
        int h = y1 - y0;
        
        if (w > 0 && h > 0) {
            unsigned char *bitmap = calloc(w * h, 1);
            if (bitmap) {
                stbtt_MakeGlyphBitmap(info, bitmap, w, h, w, scale, scale, glyph_index);
                
                int draw_x = pen_x + (int)roundf(lsb * scale);
                int draw_y = baseline + y0;
                
                // Draw glyph to pixelmap
                for (int by = 0; by < h; by++) {
                    for (int bx = 0; bx < w; bx++) {
                        unsigned char glyph_alpha = bitmap[by * w + bx];
                        if (glyph_alpha == 0) continue;
                        
                        int px = draw_x + bx;
                        int py = draw_y + by;
                        
                        if (px < 0 || py < 0 || px >= pix_width || py >= pix_height)
                            continue;
                        
                        uint8_t final_alpha = (a * glyph_alpha) / 255;
                        if (final_alpha == 0) continue;
                        
                        pixel_t* dst = &pixels[py * pix_width + px];
                        uint32_t existing = *dst;
                        
                        if (final_alpha == 255) {
                            *dst = color_argb;
                        } else {
                            uint8_t existing_r = (existing >> 16) & 0xFF;
                            uint8_t existing_g = (existing >> 8) & 0xFF;
                            uint8_t existing_b = existing & 0xFF;
                            uint8_t existing_a = (existing >> 24) & 0xFF;
                            
                            uint8_t new_r = (r * final_alpha + existing_r * (255 - final_alpha)) / 255;
                            uint8_t new_g = (g * final_alpha + existing_g * (255 - final_alpha)) / 255;
                            uint8_t new_b = (b * final_alpha + existing_b * (255 - final_alpha)) / 255;
                            uint8_t new_a = (a > existing_a) ? a : existing_a;
                            
                            *dst = (new_a << 24) | (new_r << 16) | (new_g << 8) | new_b;
                        }
                    }
                }
                free(bitmap);
            }
        }
        
        // Get next codepoint for kerning (need to peek ahead)
        const char *p_next = p;
        int next_codepoint = 0;
        if (*p_next) {
            next_codepoint = utf8_next(&p_next);
        }
        
        if (next_codepoint) {
            int kern = stbtt_GetCodepointKernAdvance(info, code_point, next_codepoint);
            pen_x += (int)roundf((advance + kern) * scale);
        } else {
            pen_x += (int)roundf(advance * scale);
        }
    }

    // ========== DRAW UNDERLINE ==========
    if (is_underlined) {
        int text_width = pen_x - x;
        int underline_y = y + (int)(size * 1.2);
        int thickness = (size > 20) ? 2 : 1;
        
        for (int t = 0; t < thickness; t++) {
            for (int px = x; px < x + text_width; px++) {
                int py = underline_y + t;
                if (px >= 0 && px < pix_width && py >= 0 && py < pix_height) {
                    pixels[py * pix_width + px] = color_argb;
                }
            }
        }
    }
    
    // ========== DRAW LINE-THROUGH ==========
    if (is_linethrough) {
        int text_width = pen_x - x;
        int midline_y = y + (int)(size * 0.5);
        
        for (int px = x; px < x + text_width; px++) {
            if (px >= 0 && px < pix_width && midline_y >= 0 && midline_y < pix_height) {
                pixels[midline_y * pix_width + px] = color_argb;
            }
        }
    }
}

// Helper: Parse color string (#RRGGBB) to ARGB
uint32_t parse_color_string(const char* color_str) {
    if (!color_str || color_str[0] != '#') {
        return 0xFFFFFFFF;
    }
    
    const char* hex = color_str + 1;
    size_t len = strlen(hex);
    
    if (len != 6 && len != 3) {
        return 0xFFFFFFFF;
    }
    
    unsigned int r = 0, g = 0, b = 0;
    
    if (len == 6) {
        // Parse RRGGBB manually
        for (int i = 0; i < 6; i++) {
            char c = hex[i];
            unsigned int val = 0;
            
            if (c >= '0' && c <= '9') val = c - '0';
            else if (c >= 'a' && c <= 'f') val = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') val = 10 + (c - 'A');
            else return 0xFFFFFFFF; // Invalid char
            
            if (i < 2) r = (r << 4) | val;
            else if (i < 4) g = (g << 4) | val;
            else b = (b << 4) | val;
        }
    } 
    else { // len == 3
        // Parse RGB -> RRGGBB
        for (int i = 0; i < 3; i++) {
            char c = hex[i];
            unsigned int val = 0;
            
            if (c >= '0' && c <= '9') val = c - '0';
            else if (c >= 'a' && c <= 'f') val = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') val = 10 + (c - 'A');
            else return 0xFFFFFFFF;
            
            val = (val << 4) | val; // Expand to 2 digits
            
            if (i == 0) r = val;
            else if (i == 1) g = val;
            else b = val;
        }
    }
    
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

// NEW: Extract coordinates from ANY field name
void extract_and_draw_element(pauk_ui_t* pauk_ui, cJSON* element, int parent_x, int parent_y) {
    // Get tag for debugging
    cJSON* tag_item = cJSON_GetObjectItem(element, "tag");
    const char* tag = tag_item ? tag_item->valuestring : "unknown";
    
    // Get viewport dimensions for percentage calculations
    int viewport_width = pauk_ui->virtual_pixmap->width;
    int viewport_height = pauk_ui->virtual_pixmap->height;
    
    int x = 0, y = 0, width = 0, height = 0;
    
    // Try "x", "y" first
    cJSON* x_item = cJSON_GetObjectItem(element, "x");
    cJSON* y_item = cJSON_GetObjectItem(element, "y");
    
    if (x_item && cJSON_IsNumber(x_item)) x = x_item->valueint;
    if (y_item && cJSON_IsNumber(y_item)) y = y_item->valueint;
    
    // Try "layout_width", "layout_height" (these should be numbers)
    cJSON* w_item = cJSON_GetObjectItem(element, "layout_width");
    cJSON* h_item = cJSON_GetObjectItem(element, "layout_height");
    
    if (w_item && cJSON_IsNumber(w_item)) width = w_item->valueint;
    if (h_item && cJSON_IsNumber(h_item)) height = h_item->valueint;
    
    // If no layout_*, try "width", "height" (may be strings with units)
    if (width == 0) {
        cJSON* width_item = cJSON_GetObjectItem(element, "width");
        if (width_item) {
            if (cJSON_IsString(width_item)) {
                // Parse "800px", "50%", etc. using viewport as reference
                width = parse_css_length(width_item->valuestring, viewport_width);
            } else if (cJSON_IsNumber(width_item)) {
                width = width_item->valueint;
            }
        }
    }
    
    if (height == 0) {
        cJSON* height_item = cJSON_GetObjectItem(element, "height");
        if (height_item) {
            if (cJSON_IsString(height_item)) {
                // Parse "600px", "50%", etc. using viewport as reference
                height = parse_css_length(height_item->valuestring, viewport_height);
            } else if (cJSON_IsNumber(height_item)) {
                height = height_item->valueint;
            }
        }
    }
    
    // Calculate absolute position
    int abs_x = parent_x + x;
    int abs_y = parent_y + y;
    
    // Draw if we have valid size
    if (width > 0 && height > 0) {
        uint32_t color = get_tag_color(tag);
        draw_filled_box_to_pixelmap(pauk_ui, abs_x, abs_y, width, height, color);
    }
    
    // Recursively process children
    cJSON* children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON* child;
        cJSON_ArrayForEach(child, children) {
            extract_and_draw_element(pauk_ui, child, abs_x, abs_y);
        }
    }
}

// Helper function to extract text from any element
const char* daj_element_text(cJSON *element) {
    if (!element) return NULL;
    
    // Try direct text/content fields
    cJSON *text_item = cJSON_GetObjectItem(element, "text");
    if (!text_item) text_item = cJSON_GetObjectItem(element, "content");
    
    if (text_item && cJSON_IsString(text_item) && strlen(text_item->valuestring) > 0) {
       // printf("   📄 Found direct text: '%s'\n", text_item->valuestring);
        return text_item->valuestring;
    }
    
    // Look in children array
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
    //    printf("   📁 Checking %d children\n", cJSON_GetArraySize(children));
        
        // Check each child for text
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            const char *child_tag = cJSON_GetObjectItem(child, "tag")->valuestring;
            
            // If child is a text node, get its content
            if (strcmp(child_tag, "text") == 0) {
                text_item = cJSON_GetObjectItem(child, "text");
                if (!text_item) text_item = cJSON_GetObjectItem(child, "content");
                if (text_item && cJSON_IsString(text_item) && strlen(text_item->valuestring) > 0) {
                 //   printf("   📄 Found text in child: '%s'\n", text_item->valuestring);
                    return text_item->valuestring;
                }
            }
            // If child is another inline element, recurse (for nested formatting)
            else if (is_inline(child_tag)) {
                const char *nested_text = daj_element_text(child);
                if (nested_text) return nested_text;
            }
        }
    }
    
    return NULL;
}


// Parse sizes string like "(max-width: 600px) 480px, 800px"
int parse_sizes(const char *sizes, int viewport_width) {
    if (!sizes || strlen(sizes) == 0) return 0;
    
    char *sizes_copy = strdup(sizes);
    char *token = strtok(sizes_copy, ",");
    int result = 0;
    
    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        
        // Check for media condition like (max-width: 600px)
        if (token[0] == '(') {
            char *end = strchr(token, ')');
            if (end) {
                char *colon = strchr(token, ':');
                if (colon && colon < end) {
                    int media_value = atoi(colon + 1);
                    if (viewport_width <= media_value) {
                        // Find the size value after the condition
                        char *size_start = end + 1;
                        while (*size_start == ' ') size_start++;
                        result = atoi(size_start);
                        break;
                    }
                }
            }
        } else {
            // No media condition, this is the default
            result = atoi(token);
        }
        
        token = strtok(NULL, ",");
    }
    
    free(sizes_copy);
    return result > 0 ? result : 800; // Default to 800px
}

// Select best image from srcset
const char* select_from_srcset(const char *srcset, int selected_width) {
    if (!srcset || strlen(srcset) == 0) return NULL;
    
    static char best_url[512];
    char *srcset_copy = strdup(srcset);
    char *token = strtok(srcset_copy, ",");
    int best_diff = INT_MAX;
    int found = 0;
    
    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        
        char url[256];
        int width = 0;
        
        // Parse "image.jpg 320w" format
        if (sscanf(token, "%s %dw", url, &width) == 2) {
            int diff = abs(width - selected_width);
            if (diff < best_diff) {
                best_diff = diff;
                strcpy(best_url, url);
                found = 1;
            }
        } else {
            // Just URL without width descriptor
            sscanf(token, "%s", url);
            if (!found) {
                strcpy(best_url, url);
                found = 1;
            }
        }
        
        token = strtok(NULL, ",");
    }
    
    free(srcset_copy);
    return found ? best_url : NULL;
}

int render_json_direct(pauk_ui_t* pauk_ui, cJSON* element, cJSON* parent,
    int offset_x, int offset_y, int scroll_y)
{
    if (!element) return 0;

  
    static html_font_t* forms_font = NULL;
    if (forms_font == NULL) {
        forms_font = font_manager_get_font(&pauk_ui->font_manager, 
                                            pauk_ui->font_manager.default_font_index);
    }

    if (parent && strcmp(get_json_string(parent, "tag", ""), "table") == 0) {
        const char *child_tag = get_json_string(element, "tag", "");
        if (strcmp(child_tag, "caption") == 0 ||
            strcmp(child_tag, "thead") == 0 ||
            strcmp(child_tag, "tbody") == 0 ||
            strcmp(child_tag, "tfoot") == 0 ||
            strcmp(child_tag, "tr") == 0 ||
            strcmp(child_tag, "th") == 0 ||
            strcmp(child_tag, "td") == 0) {
            // Skip rendering - already handled by render_table_from_data
            return 0;
        }
    }

    int count = 0;

    // Handle arrays
    if (cJSON_IsArray(element)) {
        cJSON* child;
        cJSON_ArrayForEach(child, element) {
        count += render_json_direct(pauk_ui, child, parent, offset_x, offset_y, scroll_y);
        }
        return count;
    }

    if (!cJSON_IsObject(element)) return 0;

    const char* tag = "unknown";
    cJSON* tag_item = cJSON_GetObjectItem(element, "tag");
    if (tag_item && cJSON_IsString(tag_item))
       { tag = tag_item->valuestring;}

        if (!tag_item) {
            printf("⚠️ Element missing 'tag' field\n");
            // Print the element to see what's wrong
            char *json_str = cJSON_Print(element);
            printf("Problem element: %s\n", json_str);
            free(json_str);
            return 0;
        }

    const char* type = NULL;
    cJSON* type_item = cJSON_GetObjectItem(element, "type");
    if (type_item && cJSON_IsString(type_item))
      {  type = type_item->valuestring;}

        int x = 0, y = 0, width = 0, height = 0;

        cJSON* x_item = cJSON_GetObjectItem(element, "x");
        cJSON* y_item = cJSON_GetObjectItem(element, "y");
        cJSON* w_item = cJSON_GetObjectItem(element, "width");
        cJSON* h_item = cJSON_GetObjectItem(element, "height");
    
        if (x_item) {
            if (cJSON_IsNumber(x_item)) x = x_item->valueint;
            else if (cJSON_IsString(x_item)) x = parse_css_length(x_item->valuestring, pauk_ui->virtual_pixmap->width);
        }
    
        if (y_item) {
            if (cJSON_IsNumber(y_item)) y = y_item->valueint;
            else if (cJSON_IsString(y_item)) y = parse_css_length(y_item->valuestring, pauk_ui->virtual_pixmap->height);
        }
    
        if (w_item) {
            if (cJSON_IsNumber(w_item)) width = w_item->valueint;
            else if (cJSON_IsString(w_item)) width = parse_css_length(w_item->valuestring, pauk_ui->virtual_pixmap->width);
        }
    
        if (h_item) {
            if (cJSON_IsNumber(h_item)) height = h_item->valueint;
            else if (cJSON_IsString(h_item)) height = parse_css_length(h_item->valuestring, pauk_ui->virtual_pixmap->height);
        }
    
        int final_x = offset_x + x;
        int final_y = offset_y + y - scroll_y;


// ========== SPECIAL HANDLING FOR TABLES ==========
if (strcmp(tag, "table") == 0) {
    // 🚀 HIRURŠKI FIX: Ovde NE crtamo stari okvir jer ga render_table_from_data crta perfektno!
    
    // Get table_data
    cJSON *table_data = cJSON_GetObjectItem(element, "table_data");
    if (table_data) {
        // VRAĆAMO final_x i final_y jer oni sadrže ispravne layout pozicije i razmak za caption!
        render_table_from_data(pauk_ui, table_data, final_x, final_y, 0, 0, scroll_y);
    }
}

// =================================================


    bool is_text_element = false;

    // ========== MARK (highlight) ==========
if (strcmp(tag, "mark") == 0) {
    // Draw yellow background
    uint32_t bg_color = css_color_to_uint32(get_json_string(element, "bg_color", "#ffff00"));
    draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    // Don't return - let text render on top
}

    // Determine if this element should be rendered as text
    if ((tag && strcmp(tag, "text") == 0) || (type && strcmp(type, "text") == 0)) {
        is_text_element = true;
    }
    else if (is_inline(tag)) {
        // Inline elements might contain text
        const char *test_text = daj_element_text(element);
        if (test_text) {
            is_text_element = true;
          //  printf("🔤 Inline element '%s' contains text\n", tag);
        }
    }

    if (x_item) {
        if (cJSON_IsNumber(x_item)) x = x_item->valueint;
        else if (cJSON_IsString(x_item)) x = parse_css_length(x_item->valuestring, pauk_ui->virtual_pixmap->width);
    }

    if (y_item) {
        if (cJSON_IsNumber(y_item)) y = y_item->valueint;
        else if (cJSON_IsString(y_item)) y = parse_css_length(y_item->valuestring, pauk_ui->virtual_pixmap->height);
    }

    if (w_item) {
        if (cJSON_IsNumber(w_item)) width = w_item->valueint;
        else if (cJSON_IsString(w_item)) width = parse_css_length(w_item->valuestring, pauk_ui->virtual_pixmap->width);
    }

    if (h_item) {
        if (cJSON_IsNumber(h_item)) height = h_item->valueint;
        else if (cJSON_IsString(h_item)) height = parse_css_length(h_item->valuestring, pauk_ui->virtual_pixmap->height);
    }

    // IMPORTANT FIX:
    // JSON x/y are ABSOLUTE. Only apply global offset and scroll.
     final_x = offset_x + x;
     final_y = offset_y + y - scroll_y;

    // ========== TEXT ==========
    if (is_text_element) {
        const char* text = daj_element_text(element);
        
        if (!text || strlen(text) == 0) {
            return 0;
        }

        uint32_t text_color = 0xFF000000;
        int font_size = 16;

        cJSON* color_item = cJSON_GetObjectItem(element, "color");
        cJSON* font_size_item = cJSON_GetObjectItem(element, "font_size");
        cJSON* weight_item = NULL;
        cJSON* style_item = NULL; 

        if (color_item && cJSON_IsString(color_item))
            text_color = css_color_to_uint32(color_item->valuestring);

        if (font_size_item) {
            if (cJSON_IsNumber(font_size_item)) font_size = font_size_item->valueint;
            else if (cJSON_IsString(font_size_item)) font_size = parse_css_length(font_size_item->valuestring, 16);
        }

        // ========== SELECT FONT BASED ON WEIGHT AND STYLE ==========
        int font_index = pauk_ui->font_manager.default_font_index;
        int is_bold = 0;
        int is_italic = 0;

        weight_item = cJSON_GetObjectItem(element, "font_weight");
        if (weight_item && cJSON_IsString(weight_item)) {
            is_bold = (strcmp(weight_item->valuestring, "bold") == 0);
        }

        style_item = cJSON_GetObjectItem(element, "font_style");
        if (style_item && cJSON_IsString(style_item)) {
            is_italic = (strcmp(style_item->valuestring, "italic") == 0);
        }

        // Get font family
        const char* family = DEFAULT_FONT_FAMILY;  // Default
        cJSON* family_item = cJSON_GetObjectItem(element, "font_family");
        if (family_item && cJSON_IsString(family_item)) {
            family = family_item->valuestring;
        }

        // Build expected font name
        char expected_name[256];
        if (is_bold && is_italic) {
            snprintf(expected_name, sizeof(expected_name), "%s_bold_italic", family);
        } else if (is_bold) {
            snprintf(expected_name, sizeof(expected_name), "%s_bold", family);
        } else if (is_italic) {
            snprintf(expected_name, sizeof(expected_name), "%s_italic", family);
        } else {
            snprintf(expected_name, sizeof(expected_name), "%s", family);
        }

        // Make it lowercase
        for (int i = 0; expected_name[i]; i++) {
            expected_name[i] = tolower(expected_name[i]);
        }

        // Add .ttf extension
        char filename[512];
        snprintf(filename, sizeof(filename), "%s.ttf", expected_name);

        // Find exact match
        for (int i = 0; i < pauk_ui->font_manager.font_count; i++) {
            if (str_casecmp(pauk_ui->font_manager.fonts[i].name, filename) == 0) {
                font_index = i;
                break;
            }
        }

        html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, font_index);   
        
        // ========== CHECK FOR TEXT DECORATION ==========
        int is_underlined = 0;
        int is_linethrough = 0;
        cJSON* decoration_item = cJSON_GetObjectItem(element, "text_decoration");
        if (decoration_item && cJSON_IsString(decoration_item)) {
            const char *dec = decoration_item->valuestring;
            is_underlined = (strcmp(dec, "underline") == 0 || strstr(dec, "underline") != NULL);
            is_linethrough = (strcmp(dec, "line-through") == 0 || strstr(dec, "line-through") != NULL);
        }

        if (font && font->is_loaded) {
            // ========== HANDLE MULTI-LINE TEXT ==========
            if (strchr(text, '\n')) {
                // Split by newline and render each line
                char text_copy[8192];
                strncpy(text_copy, text, sizeof(text_copy) - 1);
                text_copy[sizeof(text_copy) - 1] = '\0';
                
                int line_height = font_size + 4;
                int current_y = final_y;
                int line_num = 0;
                
                // Get text alignment
                const char *text_align = get_json_string(element, "text_align", "left");
                
                char *line = strtok(text_copy, "\n");
                while (line) {
                    // Skip empty lines
                    if (strlen(line) > 0) {
                        int line_x = final_x;
                        
                        // Apply text alignment
                        if (strcmp(text_align, "center") == 0) {
                            int line_width = estimate_text_width(line, font_size, 
                                get_json_string(element, "font_weight", "normal"),
                                get_json_string(element, "font_style", "normal"));
                            line_x = final_x + (width - line_width) / 2;
                        } else if (strcmp(text_align, "right") == 0) {
                            int line_width = estimate_text_width(line, font_size,
                                get_json_string(element, "font_weight", "normal"),
                                get_json_string(element, "font_style", "normal"));
                            line_x = final_x + width - line_width;
                        }
                        
                        render_ttf_text_to_pixelmap(pauk_ui, line, line_x, current_y,
                            font, (float)font_size, text_color, is_underlined, is_linethrough);
                    }
                    
                    current_y += line_height;
                    line = strtok(NULL, "\n");
                    line_num++;
                }
                
                // Update element height if needed (optional)
                int total_height = line_num * line_height;
                if (total_height > height) {
                    cJSON_ReplaceItemInObject(element, "height", cJSON_CreateNumber(total_height));
                }
            } else {
                // Single line - original behavior
                render_ttf_text_to_pixelmap(pauk_ui, text, final_x, final_y,
                    font, (float)font_size, text_color, is_underlined, is_linethrough);
            }
        } else {
            printf("❌ Font not loaded for text: %s\n", text);
        }
        
        return 1;
    }

    uint32_t bg_color = 0xFFFFFFFF;
    int is_in_menu = get_json_bool(element, "is_menu", 0);
    cJSON* bg_item = cJSON_GetObjectItem(element, "background_color");
    if (!bg_item) bg_item = cJSON_GetObjectItem(element, "bg_color");

    if (bg_item && cJSON_IsString(bg_item)) {
        bg_color = css_color_to_uint32(bg_item->valuestring);
        // 🚀 ALFA SHIELD: Prisilno dodaj punu neprovidnost
        if ((bg_color & 0xFF000000) == 0) {
            bg_color |= 0xFF000000; 
        }
    }

        if ((strcmp(tag, "ul") == 0 || strcmp(tag, "li") == 0) && is_in_menu) {
            // Skip drawing background for menu lists
        } else {
    if (bg_color != 0xFFFFFFFF && bg_color != 0x00FFFFFF)  {
        draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    }
}

// ========== FORME ================
if (get_json_bool(element, "is_form", 0)) {
    render_form_element(pauk_ui, element, final_x, final_y, forms_font);
}
else if (get_json_bool(element, "is_input", 0)) {
    render_input_element(pauk_ui, element, final_x, final_y, forms_font);
}
else if (get_json_bool(element, "is_output", 0)) {
    render_output_element(pauk_ui, element, final_x, final_y, forms_font);
}
else if (get_json_bool(element, "is_textarea", 0)) {
    render_textarea_element(pauk_ui, element, final_x, final_y, forms_font);
}

// ===== DRAW CURSOR FOR TEXTAREA =====
if (pauk_ui->focused_element == element) {
    const char *current_value = get_json_string(element, "value", "");
    char before_cursor[1024];
    int cursor_pos = pauk_ui->cursor_position;
    
    // Clamp cursor position
    if (cursor_pos < 0) cursor_pos = 0;
    int value_len = strlen(current_value);
    if (cursor_pos > value_len) cursor_pos = value_len;
    
    // Get text before cursor
    if (cursor_pos > 0) {
        memcpy(before_cursor, current_value, cursor_pos);
    }
    before_cursor[cursor_pos] = '\0';
    
    // Calculate cursor X position
    int cursor_x = x + 10 + estimate_text_width(before_cursor, 16, "normal", "normal");
    int cursor_y = y + 20; // Match text position
    int cursor_height = 20;
    
    // Draw solid cursor (no blinking)
    draw_filled_box_to_pixelmap(pauk_ui, cursor_x, cursor_y, 2, cursor_height, 0xFF000000);
}

// ========== IFRAME PLACEHOLDER ==========
if (strcmp(tag, "iframe") == 0) {
    // Draw background
    uint32_t bg_color = css_color_to_uint32(get_json_string(element, "bg_color", "#F0F0F0"));
    draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    
    // Draw border
    int border_w = get_json_number(element, "border_width", 1);
    uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#CCCCCC"));
    draw_box_border(pauk_ui, final_x, final_y, width, height, border_w, border_color, 0, "solid");
    
    // Draw iframe label
    const char *src = get_json_string(element, "iframe_src", "");
    const char *srcdoc = get_json_string(element, "iframe_srcdoc", "");
    
    char label[256];
    if (strlen(srcdoc) > 0) {
        snprintf(label, sizeof(label), "Iframe (srcdoc)");
    } else if (strlen(src) > 0) {
        snprintf(label, sizeof(label), "Iframe: %s", src);
    } else {
        snprintf(label, sizeof(label), "Iframe (empty)");
    }
    
    int font_size = 14;
    int text_width = estimate_text_width(label, font_size, "normal", "normal");
    int text_x = final_x + (width - text_width) / 2;
    int text_y = final_y + (height - font_size) / 2;
    
    html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, 
                                               pauk_ui->font_manager.default_font_index);
    render_ttf_text_to_pixelmap(pauk_ui, label, text_x, text_y,
                              font, font_size, 0xFF666666, 0, 0);
    
    return 1; // Don't render children
}

// ========== AUDIO PLACEHOLDER ==========
if (get_json_bool(element, "is_audio", 0)) {
    uint32_t bg_color = css_color_to_uint32(get_json_string(element, "bg_color", "#F0F0F0"));
    draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    
    int border_w = get_json_number(element, "border_width", 1);
    uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#CCCCCC"));
    draw_box_border(pauk_ui, final_x, final_y, width, height, border_w, border_color, 0, "solid");
    
    const char *label = "Audio Player (not supported)";
    int font_size = 14;
    int text_width = estimate_text_width(label, font_size, "normal", "normal");
    int text_x = final_x + (width - text_width) / 2;
    int text_y = final_y + (height - font_size) / 2;
    
    html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, 
                                               pauk_ui->font_manager.default_font_index);
    render_ttf_text_to_pixelmap(pauk_ui, label, text_x, text_y,
                              font, font_size, 0xFF666666, 0, 0);
    return 1;
}

// ========== VIDEO PLACEHOLDER ==========
if (get_json_bool(element, "is_video", 0)) {
    uint32_t bg_color = css_color_to_uint32(get_json_string(element, "bg_color", "#F0F0F0"));
    draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    
    int border_w = get_json_number(element, "border_width", 1);
    uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#CCCCCC"));
    draw_box_border(pauk_ui, final_x, final_y, width, height, border_w, border_color, 0, "solid");
    
    const char *label = "Video Player (not supported)";
    int font_size = 14;
    int text_width = estimate_text_width(label, font_size, "normal", "normal");
    int text_x = final_x + (width - text_width) / 2;
    int text_y = final_y + (height - font_size) / 2;
    
    html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, 
                                               pauk_ui->font_manager.default_font_index);
    render_ttf_text_to_pixelmap(pauk_ui, label, text_x, text_y,
                              font, font_size, 0xFF666666, 0, 0);
    
    // Draw play button triangle in the center
    int center_x = final_x + width / 2;
    int center_y = final_y + height / 2;
    draw_filled_box_to_pixelmap(pauk_ui, center_x - 10, center_y - 10, 3, 20, 0xFF666666);
    draw_filled_box_to_pixelmap(pauk_ui, center_x - 7, center_y - 7, 3, 14, 0xFF666666);
    draw_filled_box_to_pixelmap(pauk_ui, center_x - 4, center_y - 4, 3, 8, 0xFF666666);
    
    return 1;
}

// ========== CANVAS PLACEHOLDER ==========
if (get_json_bool(element, "is_canvas", 0)) {
    uint32_t bg_color = css_color_to_uint32(get_json_string(element, "bg_color", "#FFFFFF"));
    draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, bg_color);
    
    int border_w = get_json_number(element, "border_width", 1);
    uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#000000"));
    draw_box_border(pauk_ui, final_x, final_y, width, height, border_w, border_color, 0, "solid");
    
    const char *label = "Canvas (not supported)";
    int font_size = 12;
    int text_width = estimate_text_width(label, font_size, "normal", "normal");
    int text_x = final_x + (width - text_width) / 2;
    int text_y = final_y + (height - font_size) / 2;
    
    html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, 
                                               pauk_ui->font_manager.default_font_index);
    render_ttf_text_to_pixelmap(pauk_ui, label, text_x, text_y,
                              font, font_size, 0xFF666666, 0, 0);
    return 1;
}

// ========== DETAILS/SUMMARY ==========
if (get_json_bool(element, "is_details", 0)) {
    // Draw border around details
    int border_w = get_json_number(element, "border_width", 1);
    uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#CCCCCC"));
    draw_box_border(pauk_ui, final_x, final_y, width, height, border_w, border_color, 0, "solid");
    // DO NOT return - let children render
}

if (get_json_bool(element, "is_summary", 0)) {
    // Draw marker
    int marker_x = final_x - 15;
    int marker_y = final_y + (height / 2) - 4;
    
    // Draw a simple right arrow
    draw_filled_box_to_pixelmap(pauk_ui, marker_x, marker_y, 2, 8, 0xFF000000);
    draw_filled_box_to_pixelmap(pauk_ui, marker_x + 2, marker_y + 2, 2, 4, 0xFF000000);
    draw_filled_box_to_pixelmap(pauk_ui, marker_x + 4, marker_y + 4, 2, 1, 0xFF000000);
    
    // DO NOT return - let the text render normally
}

// ========== TABLE BORDER ==========
// ========== CELL BORDER ==========
// Only draw cell borders if this is NOT a table cell (they're already drawn by render_table_from_data)
if (strcmp(tag, "td") != 0 && strcmp(tag, "th") != 0) {
    if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
        int border = get_json_number(element, "border", 1);
        if (border > 0) {
            uint32_t border_color = css_color_to_uint32(get_json_string(element, "border_color", "#808080"));
            draw_box_border(pauk_ui, final_x, final_y, width, height,
                            border, border_color, 0, "solid");
        }
    }
}

// ---- GET BORDER INFO ----
// Only draw general borders if this is NOT a table cell
if (strcmp(tag, "td") != 0 && strcmp(tag, "th") != 0 && strcmp(tag, "table") != 0) {
    int border_w = 0;
    cJSON *bw_item = cJSON_GetObjectItem(element, "border_width");
    if (bw_item && cJSON_IsString(bw_item)) {
        border_w = parse_css_length(bw_item->valuestring, 0);
    }

    uint32_t border_color = 0xFF000000;
    cJSON *bc_item = cJSON_GetObjectItem(element, "border_color");
    if (bc_item && cJSON_IsString(bc_item)) {
        border_color = css_color_to_uint32(bc_item->valuestring);
    }

    int border_radius = 0;
    cJSON *br_item = cJSON_GetObjectItem(element, "border_radius");
    if (br_item && cJSON_IsString(br_item)) {
        border_radius = parse_css_length(br_item->valuestring, 0);
    }

    const char *border_style = "solid";
    cJSON *bs_item = cJSON_GetObjectItem(element, "border_style");
    if (bs_item && cJSON_IsString(bs_item)) {
        border_style = bs_item->valuestring;
    }

    if (border_w > 0 && border_color != bg_color) {
        draw_box_border(pauk_ui, final_x, final_y, width, height,
                        border_w, border_color, border_radius, border_style);
    }
}

// ========== RENDER BULLET FOR LIST ITEMS ==========
if (strcmp(tag, "li") == 0) {
    // Get bullet info from JSON
    int bullet_x = get_json_number(element, "bullet_x", final_x);
   // int bullet_y = get_json_number(element, "bullet_y", final_y);
    const char *bullet_char = get_json_string(element, "bullet_char", "");
    
    // ========== CONVERT TO SIMPLE ASCII ==========
    const char *render_char = bullet_char;
    
    // Map Unicode/HTML bullets to simple ASCII
    if (strcmp(bullet_char, "•") == 0 || strcmp(bullet_char, "●") == 0) {
        render_char = "*";  // Disc → asterisk
    } else if (strcmp(bullet_char, "◦") == 0 || strcmp(bullet_char, "○") == 0) {
        render_char = "o";  // Circle → lowercase o
    } else if (strcmp(bullet_char, "▪") == 0 || strcmp(bullet_char, "■") == 0) {
        render_char = "#";  // Square → hash
    } else if (strcmp(bullet_char, "a") == 0) {
        render_char = "*";  // Your test 'a' → asterisk
    }
    // Numbers (1., 2., etc.) stay as-is
    // =============================================
    
    if (render_char && render_char[0] != '\0') {
        // Get font for bullet
        int font_size = 16;
        cJSON* font_size_item = cJSON_GetObjectItem(element, "font_size");
        if (font_size_item) {
            if (cJSON_IsNumber(font_size_item)) font_size = font_size_item->valueint;
            else if (cJSON_IsString(font_size_item)) font_size = parse_css_length(font_size_item->valuestring, 16);
        }
        
        int font_index = pauk_ui->font_manager.default_font_index;
        html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, font_index);
        
        if (font && font->is_loaded) {
            // Render bullet at (bullet_x, bullet_y)
            render_ttf_text_to_pixelmap(pauk_ui, render_char, bullet_x, final_y,
                font, (float)font_size, 0xFF000000, 0, 0);
        }
    }

    // ========== ADD BOTTOM BORDER HERE ==========
    // Check for border-bottom property
    cJSON *border_bottom_width_item = cJSON_GetObjectItem(element, "border_bottom_width");
    cJSON *border_bottom_style_item = cJSON_GetObjectItem(element, "border_bottom_style");
    cJSON *border_bottom_color_item = cJSON_GetObjectItem(element, "border_bottom_color");
    
    if (border_bottom_width_item && border_bottom_style_item && border_bottom_color_item) {
        const char *bw_str = border_bottom_width_item->valuestring;
        const char *bs_str = border_bottom_style_item->valuestring;
        const char *bc_str = border_bottom_color_item->valuestring;
        
        int border_bottom_w = parse_css_length(bw_str, 0);
        uint32_t border_bottom_color = css_color_to_uint32(bc_str);
        
        if (border_bottom_w > 0 && strcmp(bs_str, "none") != 0) {
            // Draw only bottom border
            draw_filled_box_to_pixelmap(pauk_ui,
                                       final_x,
                                       final_y + height - border_bottom_w,
                                       width,
                                       border_bottom_w,
                                       border_bottom_color);
        }
    }
}

// ====================================================
// ========== IMAGE ==========
if (strcmp(tag, "img") == 0 || strcmp(tag, "image") == 0 || 
    (type && strcmp(type, "image") == 0)) {

    int width = get_json_number(element, "width", 100);
    int height = get_json_number(element, "height", 100);
    const char *src = get_json_string(element, "src", "");
    const char *srcset = get_json_string(element, "srcset", "");
    const char *sizes = get_json_string(element, "sizes", "");
    const char *alt = get_json_string(element, "alt", "");
  //  printf("🖼️ IMAGE ELEMENT FOUND in RENDER_JSON_DIRECT !\n");
    
    if (width <= 0) width = 100;
    if (height <= 0) height = 100;
    
    // Select image source
    const char *selected_src = NULL;
    
    if (srcset && strlen(srcset) > 0) {
        int viewport_width = pauk_ui->tab_rect_base.p1.x - pauk_ui->tab_rect_base.p0.x;
        int target_width = width;
        
        if (sizes && strlen(sizes) > 0) {
            target_width = parse_sizes(sizes, viewport_width);
        }
        
        const char *best_src = select_from_srcset(srcset, target_width);
        if (best_src) {
            selected_src = best_src;
        } else {
            selected_src = src;
        }
    } else {
        selected_src = src;
    }
    
    int img_loaded = 0;
    
    if (selected_src && selected_src[0] != '\0') {
        // Load image from memory cache (no disk I/O!)
        int img_width, img_height;
        unsigned char *img_data = get_cached_image(selected_src, width, height, &img_width, &img_height);
        
        if (img_data && img_width > 0 && img_height > 0) {
            img_loaded = 1;
            
            // Render the image directly from memory
            for (int y = 0; y < height && y < 2000; y++) {
                for (int x = 0; x < width && x < 2000; x++) {
                    int src_x = (x * img_width) / width;
                    int src_y = (y * img_height) / height;
                    
                    if (src_x >= img_width) src_x = img_width - 1;
                    if (src_y >= img_height) src_y = img_height - 1;
                    
                    int idx = (src_y * img_width + src_x) * 3;  // Always 3 channels (RGB)
                    
                    // Convert RGB to BGR for your OS
                    uint8_t b = img_data[idx + 2];
                    uint8_t g = img_data[idx + 1];
                    uint8_t r = img_data[idx];
                    
                    uint8_t bgr[3] = {b, g, r};
                    pixel_t pixel = bgr_888_2pixel(bgr);
                    pixelmap_put_pixel(pauk_ui->virtual_pixmap, final_x + x, final_y + y, pixel);
                }
            }
        }
        else {
            // ===== ADD THIS: TRIGGER DOWNLOAD =====
            // Check if this is a remote URL that needs downloading
            if (strstr(selected_src, "http://") == selected_src || 
                strstr(selected_src, "https://") == selected_src) {
                
                // Check if already queued for download
                int already_queued = get_json_bool(element, "download_queued", 0);
                if (!already_queued) {
                  //  printf("📥 Queueing image for download: %s\n", selected_src);
                    set_json_bool(element, "download_queued", 1);
                    queue_media_download(selected_src, element);
                }
            }
            // ====================================
        }
    }
    
    if (!img_loaded) {
     //   printf("  DRAWING PLACEHOLDER at (%d,%d) size %dx%d\n", final_x, final_y, width, height);
        
        // Draw placeholder
        uint32_t placeholder_color = 0xFF808080;
        draw_filled_box_to_pixelmap(pauk_ui, final_x, final_y, width, height, placeholder_color);
        draw_box_border(pauk_ui, final_x, final_y, width, height, 1, 0xFF000000, 0, "solid");
        
        const char *text = (alt && alt[0]) ? alt : "Image";
        int font_index = pauk_ui->font_manager.default_font_index;
        html_font_t* font = font_manager_get_font(&pauk_ui->font_manager, font_index);
  
        if (font && font->is_loaded) {
            int text_width = estimate_text_width(text, 12, "normal", "normal");
            int text_x = final_x + (width - text_width) / 2;
            int text_y = final_y + (height - 12) / 2;
            render_ttf_text_to_pixelmap(pauk_ui, text, text_x, text_y,
                font, 12, 0xFF000000, 0, 0);
        }
    }
}
//========== DUGME =====================================
if (get_json_bool(element, "is_button", 0)) {
    render_button_element(pauk_ui, element, final_x, final_y, width, height);
    return 1; // Don't process children further
}
//===========================================
    count++;

    // Render children WITHOUT adding parent_x/parent_y
    cJSON* children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        count += render_json_direct(pauk_ui, children, element, offset_x, offset_y, scroll_y);
    }
    gfx_update(pauk_ui->gc);
    return count;
}


void test_text_rendering(pauk_ui_t* pauk_ui) {
    if (!pauk_ui->rendering_json) {
        printf("No JSON data to render\n");
        return;
    }
    
        // Sync renderer scroll position with UI scroll position
        pauk_ui->html_renderer->scroll_y = pauk_ui->scroll_y;
        
    // Clear pixelmap
    draw_filled_box_to_pixelmap(pauk_ui, 0, 0, 
                               pauk_ui->virtual_pixmap->width,
                               pauk_ui->virtual_pixmap->height,
                               0xFFFFFFFF);
    
    // Render from memory
    int elements_drawn = render_json_direct(pauk_ui, pauk_ui->rendering_json, NULL, 
                                            40, 40, pauk_ui->scroll_y);
    printf("Rendered %d elements\n", elements_drawn);
    
    // Store for hover (already stored)
    // pauk_ui->current_json = pauk_ui->rendering_json;
}


// ======================== MIS ======================
cJSON* find_element_at_position(cJSON* root, int x, int y) {
    if (!root) return NULL;
    
    // If root is an array, check each element
    if (cJSON_IsArray(root)) {
        cJSON *item;
        cJSON_ArrayForEach(item, root) {
            cJSON *hit = find_element_at_position(item, x, y);
            if (hit) return hit;
        }
        return NULL;
    }
    
    // Check children first
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            cJSON *hit = find_element_at_position(child, x, y);
            if (hit) {
                // ===== SPECIAL CASE: If hit is text, check if parent is clickable =====
                const char* hit_tag = get_json_string(hit, "tag", "");
                if (strcmp(hit_tag, "text") == 0) {
                    // Check if parent is clickable (link, button, image, etc.)
                    int parent_is_clickable = get_json_bool(root, "is_clickable", 0);
                    int parent_is_link = get_json_bool(root, "is_link", 0);
                    int parent_is_button = get_json_bool(root, "is_button", 0);
                    int parent_is_image = get_json_bool(root, "is_image", 0);
                    const char* parent_tag = get_json_string(root, "tag", "");
                    
                    // Return parent if it's clickable
                    if (parent_is_clickable || parent_is_link || parent_is_button || parent_is_image ||
                        strcmp(parent_tag, "a") == 0 ||
                        strcmp(parent_tag, "button") == 0 ||
                        strcmp(parent_tag, "img") == 0 ||
                        strcmp(parent_tag, "image") == 0) {
                        return root;  // Return the clickable parent
                    }
                }
                return hit;
            }
        }
    }
    
    // Check this element
    int elem_x = get_json_number(root, "x", 0);
    int elem_y = get_json_number(root, "y", 0);
    int elem_w = get_json_number(root, "width", 0);
    int elem_h = get_json_number(root, "height", 0);
    
    if (elem_w > 0 && elem_h > 0 &&
        x >= elem_x && x < elem_x + elem_w &&
        y >= elem_y && y < elem_y + elem_h) {
        return root;
    }
    
    return NULL;
}

void handle_element_click(pauk_ui_t* pauk_ui, cJSON* element, int button) {
    if (!element) return;
    
    // Ignore text nodes
    const char* tag = get_json_string(element, "tag", "");
  //  const char* type = get_json_string(element, "type", "");
    if (strcmp(tag, "text") == 0) {
        return;
    }
    
     // ===== ADD THIS: Execute stored event handlers from event_handler.c =====
     int element_id = get_json_number(element, "element_id", -1);
     if (element_id != -1) {
         char id_str[32];
         snprintf(id_str, sizeof(id_str), "%d", element_id);
         
         // Execute click handlers stored in event_handler.c
         extern void execute_stored_event_handler(const char *element_id, const char *event_type);
         execute_event_handler(id_str, "click");
     }
     // =======================================================================

    // ===== CHECK FOR INPUT/TEXTAREA FOCUS =====
    int is_textarea = get_json_bool(element, "is_textarea", 0);
    int is_input = get_json_bool(element, "is_input", 0);
    
    if (is_textarea || is_input) {
        // Store previously focused element
        cJSON* previous_focused = pauk_ui->focused_element;
        
        // Set new focus
        printf("✅ Setting focus to %s\n", tag);
        pauk_ui->focused_element = element;
        pauk_ui->cursor_position = strlen(get_json_string(element, "value", ""));
        
       // ===== TRIGGER JS BLUR ON PREVIOUS ELEMENT =====
if (previous_focused && previous_focused != element) {
    int prev_id_num = get_json_number(previous_focused, "id", -1);
    if (prev_id_num != -1) {
        char prev_id_str[32];
        snprintf(prev_id_str, sizeof(prev_id_str), "%d", prev_id_num);
        
        extern JSContext *g_js_context;
        if (g_js_context) {
            char js_code[256];
            snprintf(js_code, sizeof(js_code), 
                "try { var el = document.getElementById('%s'); if(el && el.onblur) el.onblur(); } catch(e) {}",
                prev_id_str);
            js_execute_code(g_js_context, js_code);
        }
    }
}

int element_id_num = get_json_number(element, "id", -1);
       // ===== TRIGGER JS FOCUS ON NEW ELEMENT =====
if (element_id_num != -1) {
    char element_id_str[32];
    snprintf(element_id_str, sizeof(element_id_str), "%d", element_id_num);
    
    extern JSContext *g_js_context;
    if (g_js_context) {
        char js_code[256];
        snprintf(js_code, sizeof(js_code), 
            "try { var el = document.getElementById('%s'); if(el && el.onfocus) el.onfocus(); } catch(e) {}",
            element_id_str);
        js_execute_code(g_js_context, js_code);
    }
}
        
        // Get default font
        html_font_t *font = font_manager_get_font(&pauk_ui->font_manager, 
                                                   pauk_ui->font_manager.default_font_index);
        
        // Redraw the previously focused element (to remove its thick border)
        if (previous_focused && previous_focused != element) {
            // Find parent element for correct coordinates
            cJSON* prev_render = previous_focused;
            const char* prev_tag = get_json_string(prev_render, "tag", "");
            if (strcmp(prev_tag, "text") == 0) {
                int parent_id = get_json_number(prev_render, "parent_id", -1);
                prev_render = find_element_by_id(pauk_ui->rendering_json, parent_id);
            }
            
            if (prev_render) {
                int prev_x = get_json_number(prev_render, "x", 0) + 40;
                int prev_y = get_json_number(prev_render, "y", 0) + 40;
                int prev_w = get_json_number(prev_render, "width", 0);
                int prev_h = get_json_number(prev_render, "height", 0);
                
                // CLEAR THE ENTIRE AREA FIRST
                draw_filled_box_to_pixelmap(pauk_ui, prev_x, prev_y, prev_w, prev_h, 0xFFFFFFFF);
                
                // Redraw with normal border
                if (get_json_bool(previous_focused, "is_textarea", 0)) {
                    render_textarea_element(pauk_ui, prev_render, prev_x, prev_y, font);
                } else if (get_json_bool(previous_focused, "is_input", 0)) {
                    render_input_element(pauk_ui, prev_render, prev_x, prev_y, font);
                }
            }
        }
        
        // Redraw the newly focused element (with thick border)
        cJSON* new_render = element;
        const char* new_tag = get_json_string(new_render, "tag", "");
        if (strcmp(new_tag, "text") == 0) {
            int parent_id = get_json_number(new_render, "parent_id", -1);
            new_render = find_element_by_id(pauk_ui->rendering_json, parent_id);
        }
        
        if (new_render) {
            int new_x = get_json_number(new_render, "x", 0) + 40;
            int new_y = get_json_number(new_render, "y", 0) + 40;
            int new_w = get_json_number(new_render, "width", 0);
            int new_h = get_json_number(new_render, "height", 0);
            
            // CLEAR THE ENTIRE AREA FIRST
            draw_filled_box_to_pixelmap(pauk_ui, new_x, new_y, new_w, new_h, 0xFFFFFFFF);
            
            // Redraw with thick border
            if (is_textarea) {
                render_textarea_element(pauk_ui, new_render, new_x, new_y, font);
            } else if (is_input) {
                render_input_element(pauk_ui, new_render, new_x, new_y, font);
            }
        }
        
        // Force full screen update
        pixelmap_to_bitmap_copy(pauk_ui);
        gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                          &pauk_ui->list_rect, NULL);
        gfx_update(pauk_ui->gc);
        return;
    }
    
    // If clicked on non-input, clear focus
    if (pauk_ui->focused_element) {
        // Redraw the previously focused element with normal border
        cJSON* prev_render = pauk_ui->focused_element;
        const char* prev_tag = get_json_string(prev_render, "tag", "");
        if (strcmp(prev_tag, "text") == 0) {
            int parent_id = get_json_number(prev_render, "parent_id", -1);
            prev_render = find_element_by_id(pauk_ui->rendering_json, parent_id);
        }
        
        if (prev_render) {
            int prev_x = get_json_number(prev_render, "x", 0) + 40;
            int prev_y = get_json_number(prev_render, "y", 0) + 40;
            int prev_w = get_json_number(prev_render, "width", 0);
            int prev_h = get_json_number(prev_render, "height", 0);
            html_font_t *font = font_manager_get_font(&pauk_ui->font_manager, 
                                                       pauk_ui->font_manager.default_font_index);
            
            // CLEAR THE ENTIRE AREA FIRST
            draw_filled_box_to_pixelmap(pauk_ui, prev_x, prev_y, prev_w, prev_h, 0xFFFFFFFF);
            
            if (get_json_bool(pauk_ui->focused_element, "is_textarea", 0)) {
                render_textarea_element(pauk_ui, prev_render, prev_x, prev_y, font);
            } else if (get_json_bool(pauk_ui->focused_element, "is_input", 0)) {
                render_input_element(pauk_ui, prev_render, prev_x, prev_y, font);
            }
        }
        
        pauk_ui->focused_element = NULL;
        
        // Force full screen update
        pixelmap_to_bitmap_copy(pauk_ui);
        gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                          &pauk_ui->list_rect, NULL);
        gfx_update(pauk_ui->gc);
        return;
    }
    
   // ===== EXISTING CODE - LINKS, BUTTONS, ETC. =====

if (strcmp(tag, "a") == 0) {
    printf("🔗 Link element detected\n");
    
    const char* href = get_json_string(element, "href", "");
    printf("🔗 Raw href: '%s'\n", href ? href : "(NULL)");
    
    if (href && href[0]) {
        printf("🔗 href is valid, length: %zu\n", strlen(href));
        
        if (pauk_ui && pauk_ui->address_entry) {
            printf("🔗 pauk_ui and address_entry are valid\n");
            
            // ===== NOVA LOGIKA =====
            char *full_url = NULL;
            
            // Proveri da li je href relativan
            int is_relative = (strstr(href, "://") == NULL);
            printf("🔗 Is relative: %d\n", is_relative);
            
            if (is_relative && pauk_ui->current_address) {
                printf("🔗 Current address: '%s'\n", pauk_ui->current_address);
                
                // Spoji sa trenutnom adresom
                full_url = resolve_url(pauk_ui->current_address, href);
                printf("🔗 Resolved URL: '%s'\n", full_url ? full_url : "(NULL)");
            } else {
                // Već je pun URL
                full_url = strdup(href);
                printf("🔗 Using href as-is: '%s'\n", full_url ? full_url : "(NULL)");
            }
            
            if (full_url) {
                printf("🔗 Setting address entry to: '%s'\n", full_url);
                ui_entry_set_text(pauk_ui->address_entry, full_url);
                free(full_url);
                printf("🔗 Done\n");
            } else {
                printf("⚠️ full_url is NULL, using fallback\n");
                ui_entry_set_text(pauk_ui->address_entry, href);
            }
            // =========================
        } else {
            printf("⚠️ pauk_ui or address_entry is NULL!\n");
            printf("   pauk_ui: %p\n", (void*)pauk_ui);
            if (pauk_ui) {
                printf("   address_entry: %p\n", (void*)pauk_ui->address_entry);
            }
        }
    } else {
        printf("⚠️ href is empty or NULL\n");
    }
}

    else if (strcmp(tag, "button") == 0) {
        const char *button_id = get_json_string(element, "id", "");
        printf("🖱️ Button clicked: %s\n", button_id);
        
        // FIRST: Try JavaScript callback (from setOnclick)
        if (button_id && button_id[0] != '\0') {
            js_execute_onclick(button_id);
            refresh_page_after_js(pauk_ui);
            return;  // Don't process further
        }
        
        // SECOND: Check for HTML onclick attribute
        int has_onclick = get_json_bool(element, "has_onclick", 0);
        if (has_onclick) {
            const char *onclick_code = get_json_string(element, "onclick", "");
            if (onclick_code && strlen(onclick_code) > 0) {
                printf("🎯 Executing HTML onclick: %s\n", onclick_code);
                
                extern JSContext *g_js_context;
                if (g_js_context) {
                    js_execute_onclick_handler(g_js_context, onclick_code);
                }
                
                pixelmap_to_bitmap_copy(pauk_ui);
                gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                                  &pauk_ui->list_rect, NULL);
                gfx_update(pauk_ui->gc);
                return;
            }
        }
    

    
    // ===== NO JS HANDLER - PROCESS C FORM HANDLING =====
    // Get button's parent_id
    int parent_id = get_json_number(element, "parent_id", -1);
    
    // Try to find parent directly
    cJSON* direct_parent = find_element_by_id(pauk_ui->rendering_json, parent_id);
    if (direct_parent) {
        const char* parent_tag = get_json_string(direct_parent, "tag", "unknown");
        int is_form = get_json_bool(direct_parent, "is_form", 0);
        printf("Direct parent: tag=%s, is_form=%d\n", parent_tag, is_form);
    } else {
        printf("Direct parent NOT found for ID %d\n", parent_id);
    }
    
    // Climb up to find form
    cJSON* form = NULL;
    int current_id = parent_id;
    
    for (int i = 0; i < 4 && current_id != -1; i++) {
        printf("Level %d: looking for ID %d\n", i, current_id);
        cJSON* parent = find_element_by_id(pauk_ui->rendering_json, current_id);
        if (!parent) break;
        
        if (get_json_bool(parent, "is_form", 0)) {
            form = parent;
            break;
        }
        current_id = get_json_number(parent, "parent_id", -1);
    }
    
    if (form) {
        const char* button_type = get_json_string(element, "button_type", "button");
        if (strcmp(button_type, "submit") == 0) {
            printf("✅ Submitting form!\n");
            submit_form(pauk_ui, form);
        }
    } else {
        printf("No form found\n");
    }
}
    
    // Force full screen update for links/buttons
    pixelmap_to_bitmap_copy(pauk_ui);
    gfx_bitmap_render(pauk_ui->html_renderer->content_bitmap, 
                      &pauk_ui->list_rect, NULL);
    gfx_update(pauk_ui->gc);
    
    return;
}

cJSON* find_parent_form(cJSON* root, cJSON* element) {
    if (!root || !element) return NULL;
    
    // Start from the current element's parent
    int parent_id = get_json_number(element, "parent_id", -1);
    
    // Keep climbing up the tree until we find a form or reach the root
    while (parent_id != -1) {
        cJSON* parent = find_element_by_id(root, parent_id);
        if (!parent) return NULL;
        
        // Check if this parent is a form
        if (get_json_bool(parent, "is_form", 0)) {
            return parent;
        }
        
        // Move up to the next parent
        parent_id = get_json_number(parent, "parent_id", -1);
    }
    
    return NULL;
}

// Helper function to validate form
int validate_form(cJSON* form, pauk_ui_t* pauk_ui) {
    if (!form) return 1;
    
    cJSON* children = cJSON_GetObjectItem(form, "children");
    if (!children) return 1;
    
    cJSON* child;
    cJSON_ArrayForEach(child, children) {
        const char* child_tag = get_json_string(child, "tag", "");
        
        // Check direct inputs
        if (strcmp(child_tag, "input") == 0) {
            int required = get_json_bool(child, "required", 0);
            const char* name = get_json_string(child, "input_name", "");
            const char* value = get_json_string(child, "input_value", "");
            
            if (required && (!value || strlen(value) == 0)) {
                char status_msg[256];
                snprintf(status_msg, sizeof(status_msg), "Error: Field '%s' is required!", name);
                show_status_message(pauk_ui, status_msg, 3000);
                return 0;
            }
        }
        else if (strcmp(child_tag, "textarea") == 0) {
            int required = get_json_bool(child, "required", 0);
            const char* name = get_json_string(child, "input_name", "");
            const char* value = get_json_string(child, "value", "");
            
            if (required && (!value || strlen(value) == 0)) {
                char status_msg[256];
                snprintf(status_msg, sizeof(status_msg), "Error: Field '%s' is required!", name);
                show_status_message(pauk_ui, status_msg, 3000);
                return 0;
            }
        }
        
        // Check nested elements (inside divs)
        cJSON* grandchildren = cJSON_GetObjectItem(child, "children");
        if (grandchildren && cJSON_IsArray(grandchildren)) {
            cJSON* gc;
            cJSON_ArrayForEach(gc, grandchildren) {
                const char* gc_tag = get_json_string(gc, "tag", "");
                
                if (strcmp(gc_tag, "input") == 0) {
                    int required = get_json_bool(gc, "required", 0);
                    const char* name = get_json_string(gc, "input_name", "");
                    const char* value = get_json_string(gc, "input_value", "");
                    
                    if (required && (!value || strlen(value) == 0)) {
                        char status_msg[256];
                        snprintf(status_msg, sizeof(status_msg), "Error: Field '%s' is required!", name);
                        show_status_message(pauk_ui, status_msg, 3000);
                        return 0;
                    }
                }
                else if (strcmp(gc_tag, "textarea") == 0) {
                    int required = get_json_bool(gc, "required", 0);
                    const char* name = get_json_string(gc, "input_name", "");
                    const char* value = get_json_string(gc, "value", "");
                    
                    if (required && (!value || strlen(value) == 0)) {
                        char status_msg[256];
                        snprintf(status_msg, sizeof(status_msg), "Error: Field '%s' is required!", name);
                        show_status_message(pauk_ui, status_msg, 3000);
                        return 0;
                    }
                }
            }
        }
    }
    
    return 1;
}


// =========================================================================
// 🔍 POMOĆNI REKURZIVNI SAKUPLJAČ PARAMETARA FORME
// Prolazi duboko kroz sve nivoe (tabele, divove, ćelije) i traži uneti kvešt 'q'
// =========================================================================
static const char* pronadji_tekst_pretrage_duboko(cJSON *element) {
    if (!element) return "";

    if (cJSON_IsArray(element)) {
        cJSON *item;
        cJSON_ArrayForEach(item, element) {
            const char *res = pronadji_tekst_pretrage_duboko(item);
            if (res && strlen(res) > 0) return res;
        }
        return "";
    }

    const char *tag = get_json_string(element, "tag", "");
    if (strcmp(tag, "input") == 0 || strcmp(tag, "textarea") == 0) {
        const char *input_type = get_json_string(element, "input_type", "");
        const char *name = get_json_string(element, "input_name", "");
        
        // Traži polje za pretragu po:
        // 1. input_type = "search" (Yahoo koristi)
        // 2. name = "q" (Google)
        // 3. name = "p" (Yahoo)
        if (strcmp(input_type, "search") == 0 || 
            strcmp(name, "q") == 0 || 
            strcmp(name, "p") == 0) {
            
            const char *val = get_json_string(element, "value", "");
            if (!val || strlen(val) == 0) {
                val = get_json_string(element, "input_value", "");
            }
            return val;
        }
    }

    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        return pronadji_tekst_pretrage_duboko(children);
    }

    return "";
}

// Updated submit_form with validation
// =========================================================================
// 🚀 GLAVNA INTERAKTIVNA FUNKCIJA ZA SUBMIT FORME (REVIDIRANO)
// Povezuje uneti tekst sa mrežnim drajverom i pokreće živu pretragu interneta!
// =========================================================================
void submit_form(pauk_ui_t* pauk_ui, cJSON* form) {
    if (!form || !pauk_ui) return;

    // 1. Čišćenje inline skripti i validacija formata
    if (cJSON_HasObjectItem(form, "onsubmit")) {
        cJSON_ReplaceItemInObject(form, "onsubmit", cJSON_CreateString(""));
    }
    if (!validate_form(form, pauk_ui)) return;

// 2. Skupljamo parametre direktno iz forme (NEMA hardkodovanja!)
const char *privremeni_query = "";

// Prvo traži polje sa is_search_input flag-om (detektovano u init_input_element)
cJSON *children = cJSON_GetObjectItem(form, "children");
if (children && cJSON_IsArray(children)) {
    for (int i = 0; i < cJSON_GetArraySize(children); i++) {
        cJSON *child = cJSON_GetArrayItem(children, i);
        if (get_json_bool(child, "is_search_input", 0)) {
            privremeni_query = get_json_string(child, "value", "");
            if (INFO_MESSAGES_JS) {
                printf("🔍 [Form] Pronađeno polje za pretragu: '%s'\n", privremeni_query);
            }
            break;
        }
    }
}

// Fallback: ako nije pronađeno, probaj sa focused_element
if (strlen(privremeni_query) == 0 && pauk_ui->focused_element) {
    privremeni_query = get_json_string(pauk_ui->focused_element, "value", "");
    if (INFO_MESSAGES_JS && strlen(privremeni_query) > 0) {
        printf("🔍 [Form] Korišćen focused_element: '%s'\n", privremeni_query);
    }
}

// Drugi fallback: ako i dalje nema, probaj pronadji_tekst_pretrage_duboko (stara logika)
if (strlen(privremeni_query) == 0) {
    privremeni_query = pronadji_tekst_pretrage_duboko(form);
    if (INFO_MESSAGES_JS && strlen(privremeni_query) > 0) {
        printf("🔍 [Form] Korišćen pronadji_tekst_pretrage_duboko: '%s'\n", privremeni_query);
    }
}

// 🛡️ MEMORY SHIELD: Izolujemo pojam za pretragu
char *search_query = strdup(privremeni_query);
if (!search_query) return;

const char* action = get_json_string(form, "form_action", "");
    
    // 🚀 LOKALNI PREKID: Ako je akcija prazna, '#' ili nema pravog hosta
    if (!action || strlen(action) == 0 || strcmp(action, "#") == 0) {
        printf("🛑 [Form Engine] Detektovana lokalna akcija ('%s'). Zaustavljam mrežno slanje!\n", action ? action : "NULL");
        if (pauk_ui) {
            extern void test_text_rendering(pauk_ui_t* pauk_ui);
            test_text_rendering(pauk_ui);
            refresh_page_after_js(pauk_ui);
        }
        free(search_query);
        return;
    }

    if (action && strlen(action) > 0) {
        // Detektujemo trenutni host sa stranice na kojoj se nalaziš
        char trenutni_host[256];
        strcpy(trenutni_host, "google.com"); // Fallback
        
        if (pauk_ui->current_address) {
            const char *proto_end = strstr(pauk_ui->current_address, "://");
            if (proto_end) {
                const char *host_start = proto_end + 3;
                const char *host_end = strchr(host_start, '/');
                int h_len = host_end ? (host_end - host_start) : (int)strlen(host_start);
                
                if (h_len > 255) h_len = 255;
                strncpy(trenutni_host, host_start, h_len);
                trenutni_host[h_len] = '\0';
            }
        }

        // ===== SAKUPLJANJE SVIH PARAMETARA (UKLJUČUJUĆI HIDDEN) =====
        char *query_string = NULL;
        char *full_url = NULL;

        // 1. Dodaj q (pretraga) - enkodiraj
        char *q_encoded = url_encode(search_query);
        if (q_encoded) {
            asprintf(&query_string, "q=%s", q_encoded);
            free(q_encoded);
        }

        // 2. Prođi kroz svu decu forme i dodaj sva input polja
        cJSON *children = cJSON_GetObjectItem(form, "children");
        if (children && cJSON_IsArray(children)) {
            int size = cJSON_GetArraySize(children);
            for (int i = 0; i < size; i++) {
                cJSON *child = cJSON_GetArrayItem(children, i);
                if (!child) continue;
                
                const char *tag = get_json_string(child, "tag", "");
                if (strcmp(tag, "input") != 0) continue;
                
                const char *input_name = get_json_string(child, "input_name", "");
                const char *input_value = get_json_string(child, "input_value", "");
                const char *input_type = get_json_string(child, "input_type", "text");
                
                // Preskoči prazna imena i submit dugmad
                if (strlen(input_name) == 0) continue;
                if (strcmp(input_type, "submit") == 0) continue;
                if (strcmp(input_name, "q") == 0) continue; // već smo dodali
                
                // Enkodiraj i dodaj u query string (čak i ako je hidden!)
                char *encoded_name = url_encode(input_name);
                char *encoded_value = url_encode(input_value);
                
                if (encoded_name && encoded_value) {
                    char *new_query = NULL;
                    if (strlen(encoded_value) > 0) {
                        asprintf(&new_query, "%s&%s=%s", query_string, encoded_name, encoded_value);
                    } else {
                        asprintf(&new_query, "%s&%s", query_string, encoded_name);
                    }
                    free(query_string);
                    query_string = new_query;
                }
                
                if (encoded_name) free(encoded_name);
                if (encoded_value) free(encoded_value);
            }
        }

        // ===== 🚀 SIGURNOSNA POPRAVKA: Ako gbv nije dodat, dodaj ga ručno =====
        if (query_string && strstr(query_string, "gbv=") == NULL) {
            char *new_query = NULL;
            asprintf(&new_query, "%s&gbv=1", query_string);
            free(query_string);
            query_string = new_query;
            printf("🔧 [Form Engine] Dodat gbv=1 (nije pronađen u formi)\n");
        }

        // 3. Sklopi kompletan URL
        if (query_string && strlen(query_string) > 0) {
            // Ako action već sadrži '?', dodaj '&' umesto '?'
            if (strchr(action, '?') != NULL) {
                asprintf(&full_url, "https://%s%s&%s", trenutni_host, action, query_string);
            } else {
                asprintf(&full_url, "https://%s%s?%s", trenutni_host, action, query_string);
            }
        } else {
            asprintf(&full_url, "https://%s%s", trenutni_host, action);
        }
        
        free(query_string);

        // ===== KORISTI full_url =====
        if (full_url) {
            printf("🚀 [Form Engine] Konačni URL: %s\n", full_url);
            
            // Ažuriramo tekst u adresnoj traki pretraživača
            ui_entry_set_text(pauk_ui->address_entry, full_url);
            
            // Oslobađamo staru adresu i upisujemo novu u glavni pauk_ui kontekst
            if (pauk_ui->current_address) free(pauk_ui->current_address);
            pauk_ui->current_address = full_url;
            
            show_status_message(pauk_ui, "Ucitavam rezultate forme...", 2000);
            
            // 🚀 PRAVI MREŽNI OKIDAČ
            go_button_clicked(pauk_ui->go_button, pauk_ui);
        }
    }
    
    // 🛡️ MEMORY CLEAN
    free(search_query);
}

void check_hover(pauk_ui_t* pauk_ui) {
    if (!pauk_ui || !pauk_ui->current_json) return;

   // Check if cJSON is still valid (basic sanity)
   if (cJSON_IsInvalid(pauk_ui->current_json)) {
    pauk_ui->current_json = NULL;
    return;
}

    int tab_x = pauk_ui->tab_rect_base.p0.x;
    int tab_y = pauk_ui->tab_rect_base.p0.y;
    int tab_w = pauk_ui->tab_rect_base.p1.x - tab_x - 25;
    int tab_h = pauk_ui->tab_rect_base.p1.y - tab_y;

    int mouse_x = pauk_ui->mouse_pos.x;
    int mouse_y = pauk_ui->mouse_pos.y;

    // Outside tab area → reset hover
    if (mouse_x < tab_x || mouse_x >= tab_x + tab_w ||
        mouse_y < tab_y || mouse_y >= tab_y + tab_h)
    {
        if (pauk_ui->current_hover) {
            pauk_ui->current_hover = NULL;
            set_cursor(pauk_ui, ui_curs_arrow);
        }
        return;
    }

    int adjusted_x = mouse_x - tab_x - 27;
    int adjusted_y = mouse_y - tab_y - 42 + pauk_ui->scroll_y;  // Added scroll_y

    // Get element under mouse
    cJSON* new_hover = find_element_at_position(pauk_ui->current_json,
                                                adjusted_x,
                                                adjusted_y);

    // Only update if hover changed
    if (new_hover != pauk_ui->current_hover) {
        update_cursor_for_element(pauk_ui, new_hover);
        pauk_ui->current_hover = new_hover;
    }
}

cJSON* get_hover_target(cJSON* elem, cJSON* root)
{
    if (!elem || !root) return NULL; 

    const char* tag = get_json_string(elem, "tag", "");
    if (strcmp(tag, "a") == 0 ||
        strcmp(tag, "button") == 0 ||
        strcmp(tag, "input") == 0)
    {
        return elem;
    }

    // Special case: text inside link
    if (strcmp(tag, "text") == 0) {
        const char* parent_tag = get_json_string(root, "tag", "");
        if (strcmp(parent_tag, "a") == 0) {
            return root;  // return <a>
        }
    }

    return NULL;  // not hoverable
}



void draw_pixel_to_pixelmap(pauk_ui_t* pauk_ui, int x, int y, uint32_t color) {
    if (!pauk_ui || !pauk_ui->virtual_pixmap) return;
    
    pixel_t *pixels = pauk_ui->virtual_pixmap->data;
    int pix_width = pauk_ui->virtual_pixmap->width;
    int pix_height = pauk_ui->virtual_pixmap->height;
    
    if (x >= 0 && x < pix_width && y >= 0 && y < pix_height) {
        pixels[y * pix_width + x] = (pixel_t)color;
    }
}


unsigned char* decode_base64_data(const unsigned char *input, size_t input_len, size_t *out_len) {
    if (!input || input_len == 0 || !out_len) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    // Build a secure, local 256-byte decoding table on the stack
    static const unsigned char b64_table[64] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char d_table[256];
    memset(d_table, 0x80, sizeof(d_table)); // 0x80 marks unallocated/invalid bytes
    
    for (int i = 0; i < 64; i++) {
        d_table[b64_table[i]] = i;
    }
    d_table['='] = 0; // Pad marker termination boundary

    // Calculate maximum potential output allocation size
    size_t max_out = (input_len / 4) * 3 + 1;
    unsigned char *output = malloc(max_out);
    if (!output) {
        *out_len = 0;
        return NULL;
    }

    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < input_len) {
        // Skip over whitespace or newlines injected by server formatting blocks
        while (in_pos < input_len && (input[in_pos] <= 32 || d_table[input[in_pos]] == 0x80)) {
            if (input[in_pos] == '=') break; // Allow terminator to pass through
            in_pos++;
        }
        if (in_pos >= input_len) break;

        // Gather 4 valid Base64 encoded characters
        unsigned char block[4] = {0, 0, 0, 0};
        int valid_chars = 0;
        
        for (int i = 0; i < 4 && in_pos < input_len; i++) {
            while (in_pos < input_len && (input[in_pos] <= 32 && input[in_pos] != '=')) {
                in_pos++; // Skip raw spacing mid-stream
            }
            if (in_pos < input_len) {
                block[i] = input[in_pos++];
                if (block[i] != '=') valid_chars++;
            }
        }

        // Decode index mappings safely
        uint32_t sexet_0 = d_table[block[0]];
        uint32_t sexet_1 = d_table[block[1]];
        uint32_t sexet_2 = d_table[block[2]];
        uint32_t sexet_3 = d_table[block[3]];

        // Structural bit-shift back to 8-bit RGB byte formatting
        uint32_t buffer = (sexet_0 << 18) | (sexet_1 << 12) | (sexet_2 << 6) | sexet_3;

        if (valid_chars >= 2) output[out_pos++] = (buffer >> 16) & 0xFF;
        if (valid_chars >= 3) output[out_pos++] = (buffer >> 8) & 0xFF;
        if (valid_chars == 4) output[out_pos++] = buffer & 0xFF;

        // Break early if we encounter trailing evaluation padding markers
        if (block[2] == '=' || block[3] == '=') break;
    }

    output[out_pos] = '\0'; // Enforce strict trailing null terminator for safe string conversions
    *out_len = out_pos;
    return output;
}

unsigned char* convert_svg_to_rgb(const char *svg_text, int w, int h) {
    NSVGimage *image = nsvgParse((char*)svg_text, "px", 96.0f);
    if (!image) return NULL;

    // Allocate RGBA buffer for NanoSVG rasterization
    unsigned char *rgba = malloc(w * h * 4);
    if (!rgba) {
        nsvgDelete(image);
        return NULL;
    }

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0, 1.0f, rgba, w, h, w * 4);

    // Convert RGBA to RGB (3 channels) to match your existing stb pipeline
    unsigned char *rgb = malloc(w * h * 3);
    if (rgb) {
        for (int i = 0; i < w * h; i++) {
            rgb[i * 3]     = rgba[i * 4];     // R
            rgb[i * 3 + 1] = rgba[i * 4 + 1]; // G
            rgb[i * 3 + 2] = rgba[i * 4 + 2]; // B
        }
    }

    free(rgba);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return rgb;
}


// Helper function to get or create PNG file
const char* get_or_create_png(const char *src_path, int target_width, int target_height) {
    if (!src_path || src_path[0] == '\0') {
        return NULL;
    }

    // =========================================================================
    // 1. UNIVERZALNI ŠTIT ZA SVE UGRAĐENE INLINE DATA-URI SLIKE (SVI FORMATI)
    // =========================================================================
    if (strncmp(src_path, "data:image/", 11) == 0) {
        static char inline_png_path[512];
        
        // Kreiramo jedinstveno ime fajla na osnovu dužine ovog masivnog stringa i dimenzija
        snprintf(inline_png_path, sizeof(inline_png_path), "inline_img_%zu_%dx%d.png", 
                 strlen(src_path), target_width, target_height);
                 
        // Ako je ova ugrađena slika već jednom rasterizovana i snimljena lokalno, samo je vrati
        FILE *cache_check = fopen(inline_png_path, "rb");
        if (cache_check) {
            fclose(cache_check);
            return inline_png_path;
        }

        printf("🖼️ [Universal Image Pipeline] Detektovana ugrađena inline slika. Analiziram format...\n");

        const char *data_start = strchr(src_path, ',');
        if (data_start) {
            data_start++; // Pomeramo se tačno na početak binarnih/Base64 podataka
            
            unsigned char *decoded_data = NULL;
            size_t decoded_len = 0;

            // A) Ako su podaci Base64 kodirani, čistimo string i dekodiramo ga
            if (strstr(src_path, ";base64") != NULL) {
                char *sanitized_b64 = strdup(data_start);
                if (sanitized_b64) {
                    char *p = sanitized_b64;
                    char *dst = sanitized_b64;
                    while (*p) {
                        if (*p == '%' && *(p+1) && *(p+2)) {
                            char hex[3] = { *(p+1), *(p+2), '\0' };
                            *dst++ = (char)strtoul(hex, NULL, 16);
                            p += 3;
                        } else if (*p == '+') {
                            *dst++ = '+'; p++;
                        } else {
                            *dst++ = *p++;
                        }
                    }
                    *dst = '\0';
                    
                    // Pozivamo naš stabilni Base64 dekoder
                    decoded_data = decode_base64_data((const unsigned char*)sanitized_b64, 
                                                       strlen(sanitized_b64), &decoded_len);
                    free(sanitized_b64);
                }
            } 
            // B) Ako su podaci sirovi tekst (čest slučaj kod nekih SVG-ova)
            else {
                decoded_data = (unsigned char*)strdup(data_start);
                if (decoded_data) {
                    decoded_len = strlen((const char*)decoded_data);
                }
            }

            if (decoded_data && decoded_len > 0) {
                unsigned char *rgb_pixels = NULL;
                int img_width = 0, img_height = 0, channels = 0;
                int use_stb_load_from_memory = 0;

                // === POD-PROVERA FORMATA 1: Ako je u pitanju ugrađeni SVG ===
                if (strstr(src_path, "image/svg+xml") != NULL) {
                    // Osiguravamo nulu na kraju tekstualnog niza za NanoSVG
                    char *svg_text = malloc(decoded_len + 1);
                    if (svg_text) {
                        memcpy(svg_text, decoded_data, decoded_len);
                        svg_text[decoded_len] = '\0';
                        
                        rgb_pixels = convert_svg_to_rgb(svg_text, target_width, target_height);
                        free(svg_text);
                    }
                } 
                // === POD-PROVERA FORMATA 2: Za sve piksel formate (PNG, JPEG, BMP, GIF...) ===
                else {
                    use_stb_load_from_memory = 1;
                    // Koristimo stbi_load_from_memory jer slika ne postoji na disku, već u ram memoriji!
                    rgb_pixels = stbi_load_from_memory(decoded_data, (int)decoded_len, 
                                                       &img_width, &img_height, &channels, 3);
                }

                free(decoded_data);

                if (rgb_pixels) {
                    unsigned char *final_pixels = rgb_pixels;
                    
                    // Ako je u pitanju standardna slika iz memorije, moramo je skalirati na target_width/height
                    if (use_stb_load_from_memory && (img_width != target_width || img_height != target_height)) {
                        unsigned char *scaled_data = malloc(target_width * target_height * 3);
                        if (scaled_data) {
                            // Nearest neighbor skaliranje piksela iz memorije
                            for (int y = 0; y < target_height; y++) {
                                for (int x = 0; x < target_width; x++) {
                                    int src_x = (x * img_width) / target_width;
                                    int src_y = (y * img_height) / target_height;
                                    int src_idx = (src_y * img_width + src_x) * 3;
                                    int dst_idx = (y * target_width + x) * 3;
                                    
                                    scaled_data[dst_idx]     = rgb_pixels[src_idx];
                                    scaled_data[dst_idx + 1] = rgb_pixels[src_idx + 1];
                                    scaled_data[dst_idx + 2] = rgb_pixels[src_idx + 2];
                                }
                            }
                            stbi_image_free(rgb_pixels);
                            final_pixels = scaled_data;
                        }
                    }

                    // Upisujemo jedinstveni lokalni PNG u radni direktorijum
                    int write_rc = stbi_write_png(inline_png_path, target_width, target_height, 
                                                  3, final_pixels, target_width * 3);
                    
                    if (use_stb_load_from_memory && final_pixels != rgb_pixels) {
                        free(final_pixels); // Oslobađamo naš scaled_data bafer
                    } else {
                        if (use_stb_load_from_memory) stbi_image_free(rgb_pixels);
                        else free(rgb_pixels); // Za NanoSVG RGB bafer
                    }
                    
                    if (write_rc) {
                        printf("✅ [Universal Image Pipeline] Inline slika uspešno konvertovana u: %s\n", inline_png_path);
                        return inline_png_path;
                    }
                } else {
                    printf("❌ [Universal Image Pipeline] Dekodiranje piksela iz memorije neuspešno\n");
                }
            }
        }
        return NULL; 
    }

     // =========================================================================
    // 2. SCENARIO ZA EKSTERNE SLIKE (UČITAVANJE SA LOKALNOG DISKA ILI KATALOGA)
    // =========================================================================
    char temp_path[512];
    if (strlen(src_path) >= sizeof(temp_path)) return NULL;
    strcpy(temp_path, src_path);
    
    char *dot = strrchr(temp_path, '.');
    if (dot) *dot = '\0';
    
    // ISPRAVKA: Deklaracija i koriscenje lokalnog png_path bafera
    static char png_path[512];
    snprintf(png_path, sizeof(png_path), "%s_%dx%d.png", temp_path, target_width, target_height);
    
    FILE *f = fopen(png_path, "rb");
    if (f) {
        fclose(f);
        return png_path;
    }
    
    printf("📓 Standardna STB konverzija sa diska za: %s\n", src_path);
    
    int img_width, img_height, channels;
    unsigned char *img_data = stbi_load(src_path, &img_width, &img_height, &channels, 3);
    
    if (!img_data) {
        printf("📓 Failed to load: %s\n", stbi_failure_reason());
        return NULL;
    }
    
    unsigned char *scaled_data = malloc(target_width * target_height * 3);
    if (!scaled_data) {
        stbi_image_free(img_data);
        return NULL;
    }
    
    // Nearest neighbor skaliranje eksternih fajlova sa diska
    for (int y = 0; y < target_height; y++) {
        for (int x = 0; x < target_width; x++) {
            int src_x = (x * img_width) / target_width;
            int src_y = (y * img_height) / target_height;
            int src_idx = (src_y * img_width + src_x) * 3;
            int dst_idx = (y * target_width + x) * 3;
            
            scaled_data[dst_idx]     = img_data[src_idx];
            scaled_data[dst_idx + 1] = img_data[src_idx + 1];
            scaled_data[dst_idx + 2] = img_data[src_idx + 2];
        }
    }
    
    stbi_image_free(img_data);
    
    int result = stbi_write_png(png_path, target_width, target_height, 3, scaled_data, target_width * 3);
    free(scaled_data);
    
    if (result) {
        return png_path;
    }
    return NULL;
}




unsigned char* render_svg_string_to_pixels(const char* svg_str, int width, int height) {
    // Parse the SVG text
    NSVGimage* image = nsvgParse((char*)svg_str, "px", 96.0f);
    if (!image) return NULL;

    // Allocate an RGBA pixel buffer (4 bytes per pixel)
    unsigned char* pixels = malloc(width * height * 4);
    if (!pixels) {
        nsvgDelete(image);
        return NULL;
    }

    // Create the rasterizer and draw the shapes into the pixels
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, image, 0, 0, 1.0f, pixels, width, height, width * 4);

    // Clean up vector structures
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return pixels; // Ready for stbi_write_png or direct display!
}
