// rendering_elements/defaults.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "cjson.h"
#include "defaults.h"
#include "../layout_engine.h"

// Initialize ALL defaults in a cJSON object
void init_json_with_all_defaults(cJSON *json, const char *tag) {
    if (!json) return;
    
    // Only the most basic properties that EVERY element needs
    if (tag && tag[0]) {
        set_json_string(json, "tag", tag);
    }
    set_json_string(json, "type", DEFAULT_TYPE);
    set_json_string(json, "display", DEFAULT_DISPLAY);
    
    // Position (will be set during layout)
    set_json_number(json, "x", DEFAULT_X);
    set_json_number(json, "y", DEFAULT_Y);
    
    // Dimensions
    set_json_string(json, "width", DEFAULT_WIDTH);
    set_json_string(json, "height", DEFAULT_HEIGHT);
    
    // Layout flags
    set_json_bool(json, "needs_layout", true);
    set_json_bool(json, "is_visible", true);
    set_json_number(json, "layout_calculated", 0);
    
    // Hierarchy (required for parent/child relationships)
    set_json_number(json, "element_id", DEFAULT_ELEMENT_ID);
    set_json_number(json, "parent_id", DEFAULT_PARENT_ID);

    // ========== BORDERS (with zero defaults) ==========
set_json_string(json, "border_color", "#000000");
set_json_number(json, "border_width", 0);
set_json_string(json, "border_style", "none");
set_json_number(json, "border_radius", 0);
    
    // ===== REMOVED: text, content, font_*, colors, margins, paddings, borders,
    //                src, href, alt, title, boolean flags, float, clear,
    //                id, classes, class_string, sidebar defaults =====
}

// Set element-specific defaults after basic init
void set_element_specific_defaults(cJSON *json, const char *tag) {
    if (!json || !tag) return;
    
    if (strcasecmp(tag, "body") == 0) {
        set_json_number(json, "margin_top", 0);
        set_json_number(json, "margin_bottom", 0);
        set_json_number(json, "padding_top", 0);
        set_json_number(json, "padding_bottom", 0);
    }
    else if (strcasecmp(tag, "img") == 0 || strcasecmp(tag, "image") == 0) {
        set_json_string(json, "type", "image");
        set_json_bool(json, "is_image", 1);
        set_json_bool(json, "is_clickable", 0);
        set_json_string(json, "display", "inline-block");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "src", "");
        set_json_string(json, "alt", "");
        set_json_string(json, "srcset", "");
        set_json_string(json, "sizes", "");
        set_json_string(json, "loading", "eager");
        set_json_string(json, "decoding", "auto");
        set_json_number(json, "margin_top", 10);
        set_json_number(json, "margin_bottom", 20);
        set_json_number(json, "margin_left", 6);
        set_json_number(json, "margin_right", 6);
    }
    else if (strcasecmp(tag, "a") == 0) {
        set_json_string(json, "type", "inline");
        set_json_string(json, "display", "inline");
        set_json_bool(json, "is_link", 1);
        set_json_bool(json, "is_inline", 1);
        set_json_bool(json, "is_clickable", 1);
        set_json_string(json, "color", "#0000FF");
        set_json_string(json, "text_decoration", "underline");
        set_json_number(json, "margin_top", 0);
        set_json_number(json, "margin_bottom", 0);
    }
    else if (strcasecmp(tag, "abbr") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "text_decoration", "underline dotted");
    }
    else if (strcasecmp(tag, "bdi") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "bdo") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "cite") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_style", "italic");
    }
    else if (strcasecmp(tag, "code") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_family", "monospace");
        set_json_string(json, "bg_color", "#f5f5f5");
        set_json_number(json, "padding_left", 2);
        set_json_number(json, "padding_right", 2);
        set_json_number(json, "border_radius", 3);
    }
    else if (strcasecmp(tag, "data") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "dfn") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_style", "italic");
    }
    else if (strcasecmp(tag, "em") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_style", "italic");
    }
    else if (strcasecmp(tag, "kbd") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_family", "monospace");
        set_json_string(json, "bg_color", "#f0f0f0");
        set_json_number(json, "padding_left", 2);
        set_json_number(json, "padding_right", 2);
        set_json_number(json, "border_radius", 3);
    }
    else if (strcasecmp(tag, "label") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "mark") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "bg_color", "#ffff00");
        set_json_string(json, "color", "#000000");
        set_json_number(json, "padding_left", 2);
        set_json_number(json, "padding_right", 2);
    }
    else if (strcasecmp(tag, "output") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "bg_color", "#f0f0f0");
    }
    else if (strcasecmp(tag, "q") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "ruby") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "rp") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "rt") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "strike") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "text_decoration", "line-through");
    }
    else if (strcasecmp(tag, "s") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "text_decoration", "line-through");
    }
    else if (strcasecmp(tag, "samp") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_family", "monospace");
        set_json_string(json, "bg_color", "#f0f0f0");
    }
    else if (strcasecmp(tag, "small") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_number(json, "font_size", 13);
        set_json_string(json, "color", "#000000");
    }
    else if (strcasecmp(tag, "span") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "sub") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "vertical_align", "sub");
        set_json_number(json, "font_size", 12);
    }
    else if (strcasecmp(tag, "sup") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "vertical_align", "super");
        set_json_number(json, "font_size", 12);
    }
    else if (strcasecmp(tag, "time") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "var") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_style", "italic");
    }
    else if (strcasecmp(tag, "wbr") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "big") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "tt") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
        set_json_string(json, "font_family", "monospace");
    }
    else if (strcasecmp(tag, "font") == 0) {
        set_json_string(json, "display", "inline");
        set_json_string(json, "type", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "center") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "type", "block");
        set_json_string(json, "text_align", "center");
    }
    else if (strcasecmp(tag, "h1") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 32);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 2);
        set_json_number(json, "margin_bottom", 10);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 1);
    }
    else if (strcasecmp(tag, "h2") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 24);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 2);
        set_json_number(json, "margin_bottom", 10);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 2);
    }
    else if (strcasecmp(tag, "h3") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 19);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 18);
        set_json_number(json, "margin_bottom", 18);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 3);
    }
    else if (strcasecmp(tag, "h4") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 16);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 21);
        set_json_number(json, "margin_bottom", 21);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 4);
    }
    else if (strcasecmp(tag, "h5") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 13);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 22);
        set_json_number(json, "margin_bottom", 22);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 5);
    }
    else if (strcasecmp(tag, "h6") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "font_size", 11);
        set_json_string(json, "font_weight", "bold");
        set_json_number(json, "margin_top", 24);
        set_json_number(json, "margin_bottom", 24);
        set_json_bool(json, "is_heading", 1);
        set_json_number(json, "heading_level", 6);
    }
    else if (strcasecmp(tag, "p") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_top", 2);
        set_json_number(json, "margin_bottom", 2);
        set_json_bool(json, "is_paragraph", 1);
    }
    else if (strcasecmp(tag, "b") == 0) {
        set_json_string(json, "font_weight", "bold");
        set_json_string(json, "display", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "strong") == 0) {
        set_json_string(json, "font_weight", "bold");
        set_json_string(json, "display", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "i") == 0) {
        set_json_string(json, "font_style", "italic");
        set_json_string(json, "display", "inline");
        set_json_bool(json, "is_inline", 1);
    }
    else if (strcasecmp(tag, "u") == 0) {
        set_json_string(json, "text_decoration", "underline");
    }
    else if (strcasecmp(tag, "ul") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_top", 10);
        set_json_number(json, "margin_bottom", 10);
        set_json_number(json, "padding_left", 20);
        set_json_string(json, "list_style_type", "disc");
    }
    else if (strcasecmp(tag, "ol") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_top", 10);
        set_json_number(json, "margin_bottom", 10);
        set_json_number(json, "padding_left", 20);
        set_json_string(json, "list_style_type", "decimal");
    }
    else if (strcasecmp(tag, "li") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_top", 2);
        set_json_number(json, "margin_bottom", 2);
        set_json_string(json, "bullet_char", "");
        set_json_number(json, "bullet_x", 0);
        set_json_number(json, "bullet_y", 0);
        set_json_number(json, "item_index", 0);
    }
    else if (strcasecmp(tag, "table") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "type", "block");
        set_json_bool(json, "is_table", 1);
        set_json_bool(json, "is_menu", 0);
        set_json_number(json, "border", 1);
        set_json_number(json, "cellspacing", 2);
        set_json_number(json, "cellpadding", 1);
        set_json_string(json, "border_collapse", "separate");
        set_json_string(json, "width", "auto");
        set_json_number(json, "margin_top", 16);
        set_json_number(json, "margin_bottom", 16);
        set_json_string(json, "border_color", "#808080");
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
    }
    else if (strcasecmp(tag, "caption") == 0) {
        set_json_string(json, "display", "table-caption");
        set_json_string(json, "type", "table-caption");
        set_json_bool(json, "is_caption", 1);
        set_json_string(json, "caption_side", "top");
        set_json_string(json, "text_align", "center");
        set_json_string(json, "font_weight", "normal");
        set_json_number(json, "margin_bottom", 2);
    }
    else if (strcasecmp(tag, "thead") == 0) {
        set_json_string(json, "display", "table-header-group");
        set_json_string(json, "type", "table-header-group");
        set_json_bool(json, "is_thead", 1);
        set_json_string(json, "font_weight", "bold");
        set_json_string(json, "bg_color", "#f0f0f0");
    }
    else if (strcasecmp(tag, "tbody") == 0) {
        set_json_string(json, "display", "table-row-group");
        set_json_string(json, "type", "table-row-group");
        set_json_bool(json, "is_tbody", 1);
    }
    else if (strcasecmp(tag, "tfoot") == 0) {
        set_json_string(json, "display", "table-footer-group");
        set_json_string(json, "type", "table-footer-group");
        set_json_bool(json, "is_tfoot", 1);
        set_json_string(json, "font_style", "italic");
    }
    else if (strcasecmp(tag, "tr") == 0) {
        set_json_string(json, "display", "table-row");
        set_json_string(json, "type", "table-row");
        set_json_bool(json, "is_table_row", 1);
        set_json_string(json, "height", "auto");
    }
    else if (strcasecmp(tag, "td") == 0) {
        set_json_string(json, "display", "table-cell");
        set_json_string(json, "type", "table-cell");
        set_json_bool(json, "is_table_cell", 1);
        set_json_number(json, "padding_top", 1);
        set_json_number(json, "padding_right", 1);
        set_json_number(json, "padding_bottom", 1);
        set_json_number(json, "padding_left", 1);
        set_json_string(json, "text_align", "left");
        set_json_string(json, "vertical_align", "middle");
        set_json_number(json, "colspan", 1);
        set_json_number(json, "rowspan", 1);
    }
    else if (strcasecmp(tag, "th") == 0) {
        set_json_string(json, "display", "table-cell");
        set_json_string(json, "type", "table-cell");
        set_json_bool(json, "is_table_cell", 1);
        set_json_bool(json, "is_header_cell", 1);
        set_json_number(json, "padding_top", 1);
        set_json_number(json, "padding_right", 1);
        set_json_number(json, "padding_bottom", 1);
        set_json_number(json, "padding_left", 1);
        set_json_string(json, "font_weight", "bold");
        set_json_string(json, "text_align", "center");
        set_json_string(json, "vertical_align", "middle");
        set_json_string(json, "bg_color", "#f0f0f0");
        set_json_number(json, "colspan", 1);
        set_json_number(json, "rowspan", 1);
    }
    else if (strcasecmp(tag, "nav") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "type", "block");
       
        set_json_bool(json, "is_nav", 1);
        set_json_string(json, "bg_color", "#333333");
        set_json_string(json, "color", "#ffffff");
        set_json_number(json, "padding_top", 20);
        set_json_number(json, "padding_bottom", 20);
        set_json_number(json, "padding_left", 10);
        set_json_number(json, "padding_right", 10);
        set_json_number(json, "margin_bottom", 20);
    }
    else if (strcasecmp(tag, "menu") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "type", "block");
        set_json_bool(json, "is_nav", 0);
        set_json_string(json, "bg_color", "#FFFFFF");
        set_json_string(json, "color", "#000000");
        set_json_number(json, "padding_top", 0);
        set_json_number(json, "padding_bottom", 0);
        set_json_number(json, "padding_left", 0);
        set_json_number(json, "padding_right", 0);
        set_json_number(json, "margin_bottom", 20);
    }
    else if (strcasecmp(tag, "header") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_bottom", 5);
        set_json_number(json, "margin_top", 5);
    }
    else if (strcasecmp(tag, "article") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_bottom", 5);
        set_json_number(json, "margin_top", 5);
    }
    else if (strcasecmp(tag, "aside") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_bottom", 5);
        set_json_number(json, "margin_top", 5);
    }
    else if (strcasecmp(tag, "footer") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_bottom", 5);
        set_json_number(json, "margin_top", 5);
    }
    else if (strcasecmp(tag, "figure") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "text_align", "center");
    }
    else if (strcasecmp(tag, "figcaption") == 0) {
        set_json_string(json, "display", "block");
        set_json_string(json, "font_style", "italic");
        set_json_number(json, "font_size", 14);
    }
    else if (strcasecmp(tag, "iframe") == 0) {
        set_json_string(json, "display", "inline-block");
        set_json_string(json, "bg_color", "#F0F0F0");
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#CCCCCC");
        set_json_number(json, "width", 300);
        set_json_number(json, "height", 150);
    }
    else if (strcasecmp(tag, "audio") == 0) {
        set_json_string(json, "display", "inline-block");
        set_json_string(json, "bg_color", "#F0F0F0");
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#CCCCCC");
        set_json_number(json, "width", 300);
        set_json_number(json, "height", 80);
        set_json_bool(json, "is_audio", 1);
    }
    else if (strcasecmp(tag, "video") == 0) {
        set_json_string(json, "display", "inline-block");
        set_json_string(json, "bg_color", "#F0F0F0");
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#CCCCCC");
        set_json_number(json, "width", 320);
        set_json_number(json, "height", 240);
        set_json_bool(json, "is_video", 1);
    }
    else if (strcasecmp(tag, "canvas") == 0) {
        set_json_string(json, "display", "inline-block");
        set_json_string(json, "bg_color", "#FFFFFF");
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#000000");
        set_json_number(json, "width", 150);
        set_json_number(json, "height", 100);
        set_json_bool(json, "is_canvas", 1);
    }
    else if (strcasecmp(tag, "section") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_bottom", 10);
    }
    else if (strcasecmp(tag, "form") == 0) {
        set_json_string(json, "display", "block");
        set_json_number(json, "margin_top", 10);
        set_json_number(json, "margin_bottom", 10);
    }
    else if (strcasecmp(tag, "button") == 0) {
        set_json_bool(json, "is_clickable", 1);
        set_json_bool(json, "is_button", 1);
    }
    else if (strcasecmp(tag, "input") == 0) {
        set_json_bool(json, "is_clickable", 1);
        set_json_bool(json, "is_input", 1);
        set_json_bool(json, "required", 0);
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#cccccc");
        set_json_number(json, "border_radius", 4);
    }
    else if (strcasecmp(tag, "textarea") == 0) {
        set_json_string(json, "display", "inline-block");
        set_json_bool(json, "is_textarea", 1);
        set_json_bool(json, "is_form_element", 1);
        set_json_bool(json, "required", 0);
        set_json_number(json, "width", 400);
        set_json_number(json, "height", 80);
        set_json_number(json, "margin_bottom", 10);
        set_json_string(json, "border_style", "solid");
        set_json_number(json, "border_width", 1);
        set_json_string(json, "border_color", "#cccccc");
        set_json_number(json, "border_radius", 4);
        set_json_number(json, "padding", 8);
    }
}


// Get appropriate spacing between elements
int get_spacing_between_elements(const char *parent_tag, const char *child_tag,
    const char *parent_type, const char *child_type) {

// If parent is list and child is list item
if (parent_tag && child_tag && 
(strcasecmp(parent_tag, "ul") == 0 || strcasecmp(parent_tag, "ol") == 0) &&
strcasecmp(child_tag, "li") == 0) {
return 4; // Tight spacing for list items
}

// If parent is table and child is table cell
if (parent_tag && child_tag &&
(strcasecmp(parent_tag, "tr") == 0) &&
(strcasecmp(child_tag, "td") == 0 || strcasecmp(child_tag, "th") == 0)) {
return 2; // Very tight for table cells
}

// If both are block elements
if (parent_type && child_type &&
strcmp(parent_type, "block") == 0 && 
strcmp(child_type, "block") == 0) {
return 2; // 12px
}

// If child i`s inline
if (child_type && strcmp(child_type, "inline") == 0) {
return DEFAULT_INLINE_SPACING; // 2px
}

// Default parent-child spacing
return DEFAULT_PARENT_INDENT; // 16px
}
