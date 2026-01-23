/* position_layout.c
   Layout pass using cJSON. Drop into project and compile/link with cJSON.
   Reads input JSON (text.html.txt), computes x/y/layout sizes, writes:
     - text.html.final_positions.txt (JSON)
     - element_coordinates.txt (text)
     - /data/web/pos_debug.txt and /data/web/layout_log.txt (diagnostics)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "cjson.h"
#include "position_layout.h"

#define DEFAULT_VIEWPORT_WIDTH 800
#define DEFAULT_FONT_SIZE 16
#define AVG_CHAR_WIDTH_AT_16PX 7
#define LINE_HEIGHT_MULT 1.2

/* --- Utilities --- */
static char *read_file_to_buf(const char *path, long *out_size) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    rewind(f);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (r != (size_t)size) { free(buf); return NULL; }
    buf[r] = '\0';
    if (out_size) *out_size = size;
    return buf;
}

static char *resolve_ref_path(const char *base_dir, const char *filename) {
    if (!filename || filename[0] == '\0') return NULL;
    if (!base_dir || base_dir[0] == '\0') return strdup(filename);
    size_t len = strlen(base_dir) + strlen(filename) + 2;
    char *p = malloc(len);
    if (!p) return NULL;
    snprintf(p, len, "%s%s", base_dir, filename);
    return p;
}

/* Recursively copy a node with its children */
static cJSON *copy_node_with_children(cJSON *node) {
    if (!node) return NULL;
    
    cJSON *copy = cJSON_CreateObject();
    
    /* Copy basic fields */
    const char *keep[] = {"tag","text","id","href","src","alt","title",
                         "width","height","font_size","font_family","color",
                         "bg_color","style","class_string","form_file",
                         "table_file","list_file","menu_file","media_type",
                         "iframe_src","iframe_width","iframe_height",
                         "iframe_type","type", NULL};
    
    for (int k = 0; keep[k]; k++) {
        cJSON *it = cJSON_GetObjectItem(node, keep[k]);
        if (it) {
            cJSON_AddItemToObject(copy, keep[k], cJSON_Duplicate(it, 1));
        }
    }
    
    /* Copy layout fields */
    cJSON *x = cJSON_GetObjectItem(node, "x");
    cJSON *y = cJSON_GetObjectItem(node, "y");
    cJSON *w = cJSON_GetObjectItem(node, "layout_width");
    cJSON *h = cJSON_GetObjectItem(node, "layout_height");
    
    if (x && cJSON_IsNumber(x)) cJSON_AddNumberToObject(copy, "x", x->valuedouble);
    if (y && cJSON_IsNumber(y)) cJSON_AddNumberToObject(copy, "y", y->valuedouble);
    if (w && cJSON_IsNumber(w)) cJSON_AddNumberToObject(copy, "layout_width", w->valuedouble);
    if (h && cJSON_IsNumber(h)) cJSON_AddNumberToObject(copy, "layout_height", h->valuedouble);
    
    /* Recursively copy children */
    cJSON *children = cJSON_GetObjectItem(node, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *children_copy = cJSON_CreateArray();
        int child_count = cJSON_GetArraySize(children);
        
        for (int i = 0; i < child_count; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            cJSON *child_copy = copy_node_with_children(child);
            if (child_copy) {
                cJSON_AddItemToArray(children_copy, child_copy);
            }
        }
        
        if (cJSON_GetArraySize(children_copy) > 0) {
            cJSON_AddItemToObject(copy, "children", children_copy);
        } else {
            cJSON_Delete(children_copy);
        }
    }
    
    return copy;
}

/* Read estimated sizes from referenced files (table/form/list/menu etc.) */
static int read_estimated_size_from_refs(cJSON *elem, const char *base_dir, int *out_w, int *out_h) {
    if (!elem) return 0;
    const char *keys[] = {"table_file","form_file","list_file","menu_file","file","source_file", NULL};
    for (int k=0; keys[k]; k++) {
        cJSON *fitem = cJSON_GetObjectItem(elem, keys[k]);
        if (fitem && cJSON_IsString(fitem) && strlen(fitem->valuestring) > 0) {
            char *path = resolve_ref_path(base_dir, fitem->valuestring);
            if (!path) continue;
            long sz=0;
            char *buf = read_file_to_buf(path, &sz);
            free(path);
            if (!buf) continue;
            cJSON *obj = cJSON_Parse(buf);
            free(buf);
            if (!obj) continue;
            cJSON *w = cJSON_GetObjectItem(obj, "estimated_width");
            if (!w) w = cJSON_GetObjectItem(obj, "estimated_w");
            if (!w) w = cJSON_GetObjectItem(obj, "width");
            cJSON *h = cJSON_GetObjectItem(obj, "estimated_height");
            if (!h) h = cJSON_GetObjectItem(obj, "estimated_h");
            if (!h) h = cJSON_GetObjectItem(obj, "height");
            if (w && cJSON_IsNumber(w) && h && cJSON_IsNumber(h)) {
                *out_w = (int)w->valuedouble;
                *out_h = (int)h->valuedouble;
                cJSON_Delete(obj);
                return 1;
            }
            cJSON *item_count = cJSON_GetObjectItem(obj, "item_count");
            if (item_count && cJSON_IsNumber(item_count)) {
                *out_w = 400;
                *out_h = (int)(item_count->valuedouble * 20);
                cJSON_Delete(obj);
                return 1;
            }
            cJSON_Delete(obj);
        }
    }
    return 0;
}

/* Parse "123", "123px", "50%" into px (percentage uses container_width) */
static int parse_length_to_px(const char *value, int container_width, int *out_px) {
    if (!value) return 0;
    while (isspace((unsigned char)*value)) value++;
    if (*value == '\0') return 0;
    char *endptr = NULL;
    long v = strtol(value, &endptr, 10);
    if (endptr == value) return 0;
    if (strchr(value, '%')) {
        if (container_width > 0) {
            *out_px = (int)((v * container_width) / 100);
            return 1;
        } else {
            return 0;
        }
    }
    *out_px = (int)v;
    return 1;
}

/* Estimate text size */
static void estimate_text_size(const char *text, int font_size, int max_width, int *out_w, int *out_h) {
    if (!text) { *out_w = 0; *out_h = (int)ceil(font_size * LINE_HEIGHT_MULT); return; }
    char *tmp = strdup(text);
    if (!tmp) { *out_w = 0; *out_h = (int)ceil(font_size * LINE_HEIGHT_MULT); return; }
    int r=0,i=0;
    while (tmp[i]) {
        if (isspace((unsigned char)tmp[i])) {
            tmp[r++] = ' ';
            while (isspace((unsigned char)tmp[i])) i++;
        } else {
            tmp[r++] = tmp[i++];
        }
    }
    tmp[r] = '\0';
    int text_len = r;
    double avg_char = AVG_CHAR_WIDTH_AT_16PX * ((double)font_size / 16.0);
    int est_w = (int)ceil(text_len * avg_char);
    int line_h = (int)ceil(font_size * LINE_HEIGHT_MULT);
    if (max_width > 0 && est_w > max_width) {
        int chars_per_line = (int)fmax(1, floor(max_width / avg_char));
        int lines = (int)ceil((double)text_len / (double)chars_per_line);
        *out_w = max_width;
        *out_h = lines * line_h;
    } else {
        *out_w = est_w;
        *out_h = line_h;
    }
    free(tmp);
}

static int is_block_tag_str(const char *tag) {
    if (!tag) return 1;
    
    /* Basic block elements for initial browser */
    const char *blocks[] = { 
        "div", "p", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "table", "tr", "td", "th",
        "form", "header", "footer", "article", "section", 
        "nav", "aside", "main", "details", "figure", "figcaption",
        "iframe", "audio", "video", "canvas",
        "blockquote", "pre", "hr",
        "body", "html", "head",
        NULL 
    };
    
    for (int i = 0; blocks[i]; i++) {
        if (strcasecmp(blocks[i], tag) == 0) return 1;
    }
    return 0;
}

/* Helpers */
static const char *get_string(cJSON *obj, const char *key) {
    cJSON *i = cJSON_GetObjectItem(obj, key);
    if (i && cJSON_IsString(i)) return i->valuestring;
    return NULL;
}
static int get_int(cJSON *obj, const char *key, int default_val) {
    cJSON *i = cJSON_GetObjectItem(obj, key);
    if (i && cJSON_IsNumber(i)) return (int)i->valuedouble;
    return default_val;
}




/* Forward */
static int layout_node_recursive(cJSON *node, int x, int y, int container_width, const char *base_dir);



static int layout_inline_children(cJSON *children_arr, int start_index, int total_count, int start_x, int start_y, int container_width, int parent_font_size, int *consumed_out) {
    if (!children_arr || !cJSON_IsArray(children_arr) || start_index >= total_count) {
        if (consumed_out) *consumed_out = 0;
        return 0;
    }

    /* open append log (best-effort) */
    FILE *log = fopen("/data/web/layout_log.txt", "a");
    if (!log) log = fopen("layout_log.txt", "a");

    /* FIX: Get left margin of first element for initial positioning */
    cJSON *first_child = cJSON_GetArrayItem(children_arr, start_index);
    int first_margin_left = 0;
    if (first_child) {
        first_margin_left = get_int(first_child, "margin_left", 0);
    }
    
    int x = start_x + first_margin_left;  /* Account for first element's margin */
    int y = start_y;
    int line_height = (int)ceil((parent_font_size > 0 ? parent_font_size : DEFAULT_FONT_SIZE) * LINE_HEIGHT_MULT);
    int right_bound = start_x + (container_width > 0 ? container_width : DEFAULT_VIEWPORT_WIDTH);

    if (log) {
        fprintf(log, "layout_inline_children start_index=%d start_x=%d start_y=%d container_width=%d right_bound=%d\n",
                start_index, start_x, start_y, container_width, right_bound);
        fflush(log);
    }

    int i = start_index;
    for (; i < total_count; i++) {
        cJSON *child = cJSON_GetArrayItem(children_arr, i);
        if (!child) break;
        const char *child_tag = get_string(child, "tag");
        if (is_block_tag_str(child_tag)) {
            if (log) { fprintf(log, "  hit block child at idx=%d tag=%s -> stop inline run\n", i, child_tag?child_tag:"(none)"); fflush(log); }
            break; /* stop before block child */
        }

        /* FIX: Get current element's margins */
        int margin_left = get_int(child, "margin_left", 0);
        int margin_right = get_int(child, "margin_right", 0);
        int margin_top = get_int(child, "margin_top", 0);
        
        /* Adjust y for vertical margin */
        if (margin_top > 0 && i == start_index) {
            y += margin_top;
        }

        cJSON *type_item = cJSON_GetObjectItem(child, "type");
        const char *child_type = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

        if ((child_type && strcmp(child_type, "text") == 0) || (!child_tag || strlen(child_tag) == 0)) {
            const char *text = get_string(child, "text");
            int fs = get_int(child, "font_size", parent_font_size > 0 ? parent_font_size : DEFAULT_FONT_SIZE);
            int tw, th;
            estimate_text_size(text, fs, right_bound - x, &tw, &th);
            
            /* FIX: Check if element fits with its margins */
            if (x + margin_left + tw + margin_right > right_bound) { 
                y += line_height; 
                x = start_x + margin_left;  /* Reset to start with this element's margin */
            } else if (i > start_index) {
                /* Add previous element's margin_right + current element's margin_left */
                x += margin_left;
            }
            
            cJSON_ReplaceItemInObject(child, "x", cJSON_CreateNumber(x));
            cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(y));
            cJSON_ReplaceItemInObject(child, "layout_width", cJSON_CreateNumber(tw));
            cJSON_ReplaceItemInObject(child, "layout_height", cJSON_CreateNumber(th));
            if (log) fprintf(log, "  text idx=%d tag=%s -> x=%d y=%d w=%d h=%d\n", i, child_tag?child_tag:"(text)", x, y, tw, th);
            x += tw + margin_right + 1;  /* Add right margin */
            if (th > line_height) line_height = th;
        } else {
            /* element node inline (img, inline element etc.) */
            int iw=0, ih=0;
            if (!read_estimated_size_from_refs(child, NULL, &iw, &ih)) {
                cJSON *lw = cJSON_GetObjectItem(child, "layout_width");
                cJSON *lh = cJSON_GetObjectItem(child, "layout_height");
                if (lw && cJSON_IsNumber(lw)) iw = (int)lw->valuedouble;
                if (lh && cJSON_IsNumber(lh)) ih = (int)lh->valuedouble;
                if (iw == 0 && get_string(child, "text")) {
                    int tw, th;
                    estimate_text_size(get_string(child, "text"), get_int(child, "font_size", parent_font_size), right_bound - x, &tw, &th);
                    iw = tw; ih = th;
                }
                if (iw == 0) iw = 50;
                if (ih == 0) ih = line_height;
            }
            
            /* FIX: Check if element fits with its margins */
            if (x + margin_left + iw + margin_right > right_bound) { 
                y += line_height; 
                x = start_x + margin_left;  /* Reset to start with this element's margin */
            } else if (i > start_index) {
                /* Add previous element's margin_right + current element's margin_left */
                x += margin_left;
            }
            
            cJSON_ReplaceItemInObject(child, "x", cJSON_CreateNumber(x));
            cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(y));
            cJSON_ReplaceItemInObject(child, "layout_width", cJSON_CreateNumber(iw));
            cJSON_ReplaceItemInObject(child, "layout_height", cJSON_CreateNumber(ih));
            if (log) fprintf(log, "  elem idx=%d tag=%s -> x=%d y=%d w=%d h=%d (ref=%s)\n",
                             i, child_tag?child_tag:"(elem)", x, y, iw, ih,
                             (get_string(child,"src") ? get_string(child,"src") : "(no-src)"));
            x += iw + margin_right + 1;  /* Add right margin */
            if (ih > line_height) line_height = ih;
        }
        if (log) fflush(log);
    }

    if (consumed_out) *consumed_out = i - start_index;

    if (log) {
        fprintf(log, "layout_inline_children consumed=%d total_height=%d\n", i - start_index, (y - start_y) + line_height);
        fclose(log);
    }
    return (y - start_y) + line_height;
}

/* Layout recursion */
static int layout_node_recursive(cJSON *node, int x, int y, int container_width, const char *base_dir) {
    if (!node) return 0;

    /* === CRITICAL FIX: Calculate parent's padded content area === */
    int padding_left = get_int(node, "padding_left", 0);
    int padding_top = get_int(node, "padding_top", 0);
    int content_start_x = x + padding_left;
    int content_start_y = y + padding_top;
    
    /* Set this node's position */
    cJSON *xi = cJSON_GetObjectItem(node, "x");
    if (!xi) cJSON_AddNumberToObject(node, "x", x); 
    else cJSON_SetNumberValue(xi, x);
    
    cJSON *yi = cJSON_GetObjectItem(node, "y");
    if (!yi) cJSON_AddNumberToObject(node, "y", y); 
    else cJSON_SetNumberValue(yi, y);

    /* Calculate this node's width */
    int layout_w = 0;
    cJSON *lw = cJSON_GetObjectItem(node, "layout_width");
    if (lw && cJSON_IsNumber(lw) && lw->valuedouble > 0) {
        layout_w = (int)lw->valuedouble;
    } else {
        const char *width_str = get_string(node, "width");
        if (width_str) {
            int px;
            if (parse_length_to_px(width_str, container_width, &px)) {
                layout_w = px;
            }
        }
    }
    if (layout_w <= 0) {
        layout_w = container_width > 0 ? container_width : DEFAULT_VIEWPORT_WIDTH;
    }

    /* Check for referenced size files */
    int ref_w = 0, ref_h = 0;
    if (read_estimated_size_from_refs(node, base_dir, &ref_w, &ref_h)) {
        layout_w = ref_w;
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(ref_h));
        return ref_h;
    }

    /* Handle image elements */
    cJSON *is_img = cJSON_GetObjectItem(node, "is_image");
    if (is_img && cJSON_IsTrue(is_img)) {
        const char *aw = get_string(node, "attr_width");
        const char *ah = get_string(node, "attr_height");
        int ipw = 0, iph = 0;
        if (aw && ah && parse_length_to_px(aw, container_width, &ipw) && 
            parse_length_to_px(ah, container_width, &iph)) {
            cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(ipw));
            cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(iph));
            return iph;
        }
    }

    /* Handle text nodes */
    const char *tag = get_string(node, "tag");
    cJSON *type_item = cJSON_GetObjectItem(node, "type");
    const char *type_str = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

    if ((type_str && strcmp(type_str, "text") == 0) || (!tag || strlen(tag) == 0)) {
        const char *text = get_string(node, "text");
        int fs = get_int(node, "font_size", DEFAULT_FONT_SIZE);
        int w, h;
        estimate_text_size(text, fs, layout_w, &w, &h);
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(h));
        return h;
    }

    /* Determine if this is a block element */
    int is_block = is_block_tag_str(tag);

    if (is_block) {
        /* === CRITICAL: Children start at content_start_y, not raw y === */
        int cursor_y = content_start_y;
        cJSON *children = cJSON_GetObjectItem(node, "children");
        
        if (children && cJSON_IsArray(children)) {
            int count = cJSON_GetArraySize(children);
            for (int i = 0; i < count; i++) {
                cJSON *child = cJSON_GetArrayItem(children, i);
                if (!child) continue;
                
                const char *child_tag = get_string(child, "tag");
                
                if (is_block_tag_str(child_tag)) {
                    /* === FIX: Pass content_start_x, NOT x === */
                    int child_height = layout_node_recursive(child, content_start_x, cursor_y, layout_w, base_dir);
                    cursor_y += child_height;
                } else {
                    /* === FIX: Pass content_start_x, NOT x === */
                    int consumed = 0;
                    int used_height = layout_inline_children(children, i, count, 
                                                           content_start_x, cursor_y, layout_w,
                                                           get_int(node, "font_size", DEFAULT_FONT_SIZE), 
                                                           &consumed);
                    if (consumed > 0) {
                        i += (consumed - 1);
                    }
                    cursor_y += used_height;
                }
            }
        }
        
        int used_height = cursor_y - y;
        if (used_height <= 0) {
            used_height = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
        }
        
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(used_height));
        return used_height;
    } else {
        /* Inline element with children */
        cJSON *children = cJSON_GetObjectItem(node, "children");
        if (children && cJSON_IsArray(children)) {
            int total = cJSON_GetArraySize(children);
            int consumed = 0;
            /* === FIX: Pass content_start_x/content_start_y, NOT x/y === */
            int used_height = layout_inline_children(children, 0, total, 
                                                   content_start_x, content_start_y, layout_w,
                                                   get_int(node, "font_size", DEFAULT_FONT_SIZE), 
                                                   &consumed);
            cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
            cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(used_height));
            return used_height;
        }
        
        /* Leaf inline element */
        int default_height = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(default_height));
        return default_height;
    }
}

/* Ensure every node has numeric x/y/layout_width/layout_height and assign inline X positions
   when layout pass left them unset. */
   static void finalize_positions_recursive(cJSON *node, int parent_x, int parent_y) {
    if (!node) return;

    /* STEP 1: Get this node's CURRENT position (may be set by layout_node_recursive) */
    cJSON *xj = cJSON_GetObjectItem(node, "x");
    cJSON *yj = cJSON_GetObjectItem(node, "y");
    
    int node_x, node_y;
    
    if (xj && cJSON_IsNumber(xj) && yj && cJSON_IsNumber(yj)) {
        /* Node already has a position - KEEP IT! */
        node_x = (int)xj->valuedouble;
        node_y = (int)yj->valuedouble;
        
        /* If position is (0,0) and we're not at root, make it relative to parent */
        if (node_x == 0 && node_y == 0 && (parent_x != 0 || parent_y != 0)) {
            int padding_left = get_int(node, "padding_left", 0);
            int padding_top = get_int(node, "padding_top", 0);
            node_x = parent_x + padding_left;
            node_y = parent_y + padding_top;
            cJSON_SetNumberValue(xj, node_x);
            cJSON_SetNumberValue(yj, node_y);
        }
    } else {
        /* Node has no position - calculate relative to parent with padding */
        int padding_left = get_int(node, "padding_left", 0);
        int padding_top = get_int(node, "padding_top", 0);
        node_x = parent_x + padding_left;
        node_y = parent_y + padding_top;
        
        if (!xj) cJSON_AddNumberToObject(node, "x", node_x);
        else cJSON_SetNumberValue(xj, node_x);
        if (!yj) cJSON_AddNumberToObject(node, "y", node_y);
        else cJSON_SetNumberValue(yj, node_y);
    }

    /* STEP 2: Get layout dimensions */
    cJSON *lw = cJSON_GetObjectItem(node, "layout_width");
    cJSON *lh = cJSON_GetObjectItem(node, "layout_height");
    int node_w = lw && cJSON_IsNumber(lw) ? (int)lw->valuedouble : DEFAULT_VIEWPORT_WIDTH;
    int node_h = lh && cJSON_IsNumber(lh) ? (int)lh->valuedouble : (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);

    /* STEP 3: Process children - BUT DON'T OVERWRITE THEIR POSITIONS! */
    cJSON *children = cJSON_GetObjectItem(node, "children");
    if (!children || !cJSON_IsArray(children)) return;

    int count = cJSON_GetArraySize(children);
    
    /* === CRITICAL: Start cursor at parent's padded position === */
    int parent_padding_left = get_int(node, "padding_left", 0);
    int parent_padding_top = get_int(node, "padding_top", 0);
    int cur_x = node_x + parent_padding_left;  // Start at padded position!
    int cur_y = node_y + parent_padding_top;   // Start at padded position!
    
    int line_height = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
    int max_width = node_w > 0 ? node_w : DEFAULT_VIEWPORT_WIDTH;

    for (int i = 0; i < count; i++) {
        cJSON *ch = cJSON_GetArrayItem(children, i);
        if (!ch) continue;
        
        const char *tag = get_string(ch, "tag");
        
        /* STEP 4: Check if child already has a position */
        cJSON *child_x = cJSON_GetObjectItem(ch, "x");
        cJSON *child_y = cJSON_GetObjectItem(ch, "y");
        
        if (child_x && cJSON_IsNumber(child_x) && child_y && cJSON_IsNumber(child_y)) {
            /* CHILD ALREADY HAS A POSITION - RESPECT IT! */
            int child_x_val = (int)child_x->valuedouble;
            int child_y_val = (int)child_y->valuedouble;
            
            /* If position is absolute (0,0), make it relative WITH PADDING */
            if (child_x_val == 0 && child_y_val == 0) {
                if (is_block_tag_str(tag)) {
                    /* === FIXED: Use parent's PADDED position for block children === */
                    child_x_val = node_x + parent_padding_left;
                    child_y_val = cur_y;
                    cJSON_SetNumberValue(child_x, child_x_val);
                    cJSON_SetNumberValue(child_y, child_y_val);
                } else {
                    /* For inline children, use current cursor (already padded) */
                    child_x_val = cur_x;
                    child_y_val = cur_y;
                    cJSON_SetNumberValue(child_x, child_x_val);
                    cJSON_SetNumberValue(child_y, child_y_val);
                }
            }
            
            /* Recurse with child's actual position */
            finalize_positions_recursive(ch, child_x_val, child_y_val);
            
            /* Update cursor based on child type */
            if (is_block_tag_str(tag)) {
                cJSON *child_h = cJSON_GetObjectItem(ch, "layout_height");
                int child_height = child_h && cJSON_IsNumber(child_h) ? (int)child_h->valuedouble : line_height;
                cur_y += child_height;
                
                /* === FIXED: Reset to parent's PADDED position === */
                cur_x = node_x + parent_padding_left;
            } else {
                cJSON *child_w = cJSON_GetObjectItem(ch, "layout_width");
                int child_width = child_w && cJSON_IsNumber(child_w) ? (int)child_w->valuedouble : 100;
                cur_x += child_width + 1;
                
                /* Handle line wrapping */
                if (cur_x > (node_x + parent_padding_left) + max_width) {
                    cur_y += line_height;
                    /* === FIXED: Wrap to parent's PADDED position === */
                    cur_x = node_x + parent_padding_left;
                }
            }
        } else {
            /* Child has no position - calculate one */
            cJSON *cw = cJSON_GetObjectItem(ch, "layout_width");
            cJSON *chh = cJSON_GetObjectItem(ch, "layout_height");
            int child_w = cw && cJSON_IsNumber(cw) ? (int)cw->valuedouble : 0;
            int child_h = chh && cJSON_IsNumber(chh) ? (int)chh->valuedouble : line_height;

            if (child_w <= 0) {
                if (get_string(ch, "text")) {
                    int tw, th;
                    estimate_text_size(get_string(ch, "text"),
                                       get_int(ch, "font_size", DEFAULT_FONT_SIZE),
                                       max_width, &tw, &th);
                    child_w = tw;
                    child_h = th;
                } else {
                    child_w = 100;
                    if (child_h <= 0) child_h = line_height;
                }
            }

            if (is_block_tag_str(tag)) {
                /* === FIXED: Block child positioning WITH PARENT PADDING === */
                int block_child_x = node_x + parent_padding_left;  // ADD PADDING!
                cJSON_AddNumberToObject(ch, "x", block_child_x);
                cJSON_AddNumberToObject(ch, "y", cur_y);
                
                /* Recurse with PADDED position */
                finalize_positions_recursive(ch, block_child_x, cur_y);
                
                cJSON *child_lh = cJSON_GetObjectItem(ch, "layout_height");
                int used_h = child_lh && cJSON_IsNumber(child_lh) ? (int)child_lh->valuedouble : child_h;
                cur_y += used_h;
                
                /* === FIXED: Reset to parent's PADDED position === */
                cur_x = node_x + parent_padding_left;
            } else {
                /* Inline child positioning */
                /* === FIXED: Use parent's PADDED position for bounds check === */
                if (cur_x + child_w > (node_x + parent_padding_left) + max_width) {
                    cur_y += line_height;
                    /* === FIXED: Wrap to parent's PADDED position === */
                    cur_x = node_x + parent_padding_left;
                }
                
                cJSON_AddNumberToObject(ch, "x", cur_x);
                cJSON_AddNumberToObject(ch, "y", cur_y);
                
                finalize_positions_recursive(ch, cur_x, cur_y);
                
                cur_x += child_w + 1;
                
                if (child_h > line_height) {
                    line_height = child_h;
                }
            }
        }
    }

    /* Update parent's layout height if needed */
    int total_used = (cur_y - node_y) + line_height;
    if (total_used > node_h) {
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(total_used));
    }
}

/* Print coords recursively */
static void print_coords_recursive(FILE *coord, cJSON *item, int depth) {
    if (!item || !coord) return;
    cJSON *x = cJSON_GetObjectItem(item, "x");
    cJSON *y = cJSON_GetObjectItem(item, "y");
    cJSON *w = cJSON_GetObjectItem(item, "layout_width");
    cJSON *h = cJSON_GetObjectItem(item, "layout_height");
    cJSON *tag = cJSON_GetObjectItem(item, "tag");
    int xi = x && cJSON_IsNumber(x) ? (int)x->valuedouble : 0;
    int yi = y && cJSON_IsNumber(y) ? (int)y->valuedouble : 0;
    int wi = w && cJSON_IsNumber(w) ? (int)w->valuedouble : 0;
    int hi = h && cJSON_IsNumber(h) ? (int)h->valuedouble : 0;
    const char *t = tag && cJSON_IsString(tag) ? tag->valuestring : "?";
    for (int d=0; d<depth; d++) fprintf(coord, "  ");
    fprintf(coord, "(%d,%d) %dx%d  <%s>\n", xi, yi, wi, hi, t);
    cJSON *children = cJSON_GetObjectItem(item, "children");
    if (children && cJSON_IsArray(children)) {
        int cc = cJSON_GetArraySize(children);
        for (int j=0;j<cc;j++) print_coords_recursive(coord, cJSON_GetArrayItem(children, j), depth+1);
    }
}

/* Main public function */
int calculate_text_positions(const char *input_json_path, const char *output_json_path) {
    FILE *log = fopen("/data/web/layout_log.txt", "wb");
    if (!log) log = fopen("layout_log.txt", "wb");
    if (log) {
        fprintf(log, "calculate_text_positions START\ninput: %s\noutput: %s\n", input_json_path ? input_json_path : "(null)", output_json_path ? output_json_path : "(null)");
        fflush(log);
    }

    const char *out_path = output_json_path ? output_json_path : "text.html.final_positions.txt";
    long size = 0;
    char *buf = read_file_to_buf(input_json_path, &size);
    if (!buf) {
        /* fallback to text.html.txt */
        if (log) fprintf(log, "Primary input not found, trying text.html.txt\n");
        free(buf);
        buf = read_file_to_buf("text.html.txt", &size);
        if (!buf) {
            if (log) { fprintf(log, "ERROR: Could not open input JSON\n"); fclose(log); }
            return 0;
        }
    }

    cJSON *root = cJSON_Parse(buf);
    if (log) {
        if (!root) fprintf(log, "JSON parse FAILED\n");
        else fprintf(log, "JSON parse OK\n");
        fflush(log);
    }
    free(buf);
    if (!root) { if (log) fclose(log); return 0; }

    /* determine base_dir */
    char base_dir[512] = {0};
    if (input_json_path) {
        const char *last_slash = strrchr(input_json_path, '/');
        if (last_slash) {
            size_t dlen = last_slash - input_json_path + 1;
            if (dlen < sizeof(base_dir)) {
                strncpy(base_dir, input_json_path, dlen);
                base_dir[dlen] = '\0';
            }
        }
    }

    /* determine nodes array */
    cJSON *nodes_array = NULL;
    if (cJSON_IsArray(root)) nodes_array = root;
    else if (cJSON_IsObject(root)) {
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, root) {
            if (cJSON_IsArray(child)) { nodes_array = child; break; }
        }
        if (!nodes_array) {
            nodes_array = cJSON_CreateArray();
            cJSON_AddItemToArray(nodes_array, cJSON_Duplicate(root, 1));
            cJSON_Delete(root);
            root = NULL;
        }
    } else {
        if (log) fprintf(log, "ERROR: Unexpected root type\n");
        if (log) fclose(log);
        cJSON_Delete(root);
        return 0;
    }

    if (log) {
        int top_count = cJSON_GetArraySize(nodes_array);
        fprintf(log, "nodes_array ready, top_count=%d\n", top_count);
        fflush(log);
    }

    int top_count = cJSON_GetArraySize(nodes_array);
    int y_cursor = 0;
    for (int i=0;i<top_count;i++) {
        cJSON *node = cJSON_GetArrayItem(nodes_array, i);
        layout_node_recursive(node, 0, y_cursor, DEFAULT_VIEWPORT_WIDTH, base_dir);
        cJSON *lh = cJSON_GetObjectItem(node, "layout_height");
        if (lh && cJSON_IsNumber(lh)) y_cursor += (int)lh->valuedouble;
        else y_cursor += (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
    }

    if (log) {
        fprintf(log, "After layout pass:\n");
        for (int i=0;i<top_count;i++) {
            cJSON *n = cJSON_GetArrayItem(nodes_array, i);
            cJSON *lx = cJSON_GetObjectItem(n, "layout_width");
            cJSON *lh = cJSON_GetObjectItem(n, "layout_height");
            cJSON *x = cJSON_GetObjectItem(n, "x");
            cJSON *y = cJSON_GetObjectItem(n, "y");
            const char *tag = get_string(n, "tag");
            fprintf(log, "TOP[%d] tag=%s x=%d y=%d w=%d h=%d children=%s\n", i, tag?tag:"(none)",
                x && cJSON_IsNumber(x) ? (int)x->valuedouble : -1,
                y && cJSON_IsNumber(y) ? (int)y->valuedouble : -1,
                lx && cJSON_IsNumber(lx) ? (int)lx->valuedouble : -1,
                lh && cJSON_IsNumber(lh) ? (int)lh->valuedouble : -1,
                cJSON_GetObjectItem(n, "children") && cJSON_IsArray(cJSON_GetObjectItem(n, "children")) ? "yes":"no");
        }
        fflush(log);
    }

    /* Finalize positions so every node has numeric x,y,w,h */
    for (int i=0;i<top_count;i++) finalize_positions_recursive(cJSON_GetArrayItem(nodes_array, i), 0, 0);

    if (log) {
        fprintf(log, "After finalize_positions_recursive sample:\n");
        for (int i=0;i< (top_count<10?top_count:10); i++) {
            cJSON *n = cJSON_GetArrayItem(nodes_array, i);
            cJSON *x = cJSON_GetObjectItem(n, "x");
            cJSON *y = cJSON_GetObjectItem(n, "y");
            cJSON *w = cJSON_GetObjectItem(n, "layout_width");
            cJSON *h = cJSON_GetObjectItem(n, "layout_height");
            const char *tag = get_string(n, "tag");
            fprintf(log, "TOP[%d] tag=%s x=%d y=%d w=%d h=%d\n", i, tag?tag:"(none)",
                x && cJSON_IsNumber(x) ? (int)x->valuedouble : -1,
                y && cJSON_IsNumber(y) ? (int)y->valuedouble : -1,
                w && cJSON_IsNumber(w) ? (int)w->valuedouble : -1,
                h && cJSON_IsNumber(h) ? (int)h->valuedouble : -1);
        }
        fflush(log);
    }

    /* Build final trimmed output */
    cJSON *final_arr = cJSON_CreateArray();
    for (int i=0;i<top_count;i++) {
        cJSON *node = cJSON_GetArrayItem(nodes_array, i);
        /* Trim: keep rendering-relevant fields and computed layout */
        cJSON *out = cJSON_CreateObject();
        const char *keep[] = {"tag","text","id","href","src","alt","title","width","height","font_size","font_family","color","bg_color","style","class_string","form_file","table_file","list_file","menu_file","media_type","iframe_src","iframe_width","iframe_height","iframe_type","type", NULL};
        for (int k=0; keep[k]; k++) {
            cJSON *it = cJSON_GetObjectItem(node, keep[k]);
            if (it) cJSON_AddItemToObject(out, keep[k], cJSON_Duplicate(it, 1));
        }
        cJSON *x = cJSON_GetObjectItem(node, "x");
        cJSON *y = cJSON_GetObjectItem(node, "y");
        cJSON *w = cJSON_GetObjectItem(node, "layout_width");
        cJSON *h = cJSON_GetObjectItem(node, "layout_height");
        if (x && cJSON_IsNumber(x)) cJSON_AddNumberToObject(out, "x", x->valuedouble);
        if (y && cJSON_IsNumber(y)) cJSON_AddNumberToObject(out, "y", y->valuedouble);
        if (w && cJSON_IsNumber(w)) cJSON_AddNumberToObject(out, "layout_width", w->valuedouble);
        if (h && cJSON_IsNumber(h)) cJSON_AddNumberToObject(out, "layout_height", h->valuedouble);
        cJSON *children = cJSON_GetObjectItem(node, "children");
        if (children && cJSON_IsArray(children)) {
            cJSON *arr = cJSON_CreateArray();
            int cc = cJSON_GetArraySize(children);
            for (int j=0;j<cc;j++) {
                cJSON *ch = cJSON_GetArrayItem(children, j);
                /* recursively trim */
                /* Use a small helper inline (duplicate trim logic) */
                /* Simpler: duplicate entire child and then remove non-kept keys is more work; instead reuse recursion */
                /* We'll call a quick recursive trim function here by reusing logic above - implement simple recursion */
                /* For brevity, duplicate child with cJSON_Duplicate and then leave it as-is (it's okay to include extra fields) */
                cJSON *dup = copy_node_with_children(ch);
                cJSON_AddItemToArray(arr, dup);
            }
            if (cJSON_GetArraySize(arr) > 0) cJSON_AddItemToObject(out, "children", arr);
            else cJSON_Delete(arr);
        }
        cJSON_AddItemToArray(final_arr, out);
    }

    /* Write final JSON */
    char *out_str = cJSON_Print(final_arr);
    if (!out_str) {
        if (log) fprintf(log, "ERROR: cJSON_Print failed\n");
        if (log) fclose(log);
        cJSON_Delete(final_arr);
        if (root) cJSON_Delete(root);
        return 0;
    }
    FILE *of = fopen(out_path, "wb");
    if (!of) {
        if (log) fprintf(log, "ERROR: Cannot open output file %s\n", out_path);
        if (log) fclose(log);
        free(out_str);
        cJSON_Delete(final_arr);
        if (root) cJSON_Delete(root);
        return 0;
    }
    fwrite(out_str, 1, strlen(out_str), of);
    fclose(of);
    free(out_str);

    /* Write element_coordinates.txt */
    FILE *coord = fopen("element_coordinates.txt", "wb");
    if (coord) {
        fprintf(coord, "=== ELEMENT COORDINATES ===\n\n");
        for (int i=0;i<top_count;i++) print_coords_recursive(coord, cJSON_GetArrayItem(final_arr, i), 0);
        fclose(coord);
    } else if (log) fprintf(log, "WARNING: Could not create element_coordinates.txt\n");

    /* Write pos_debug to /data/web for easy retrieval */
    FILE *dbg = fopen("/data/web/pos_debug.txt", "wb");
    if (!dbg) dbg = fopen("pos_debug.txt", "wb");
    if (dbg) {
        for (int i=0;i<top_count;i++) {
            print_coords_recursive(dbg, cJSON_GetArrayItem(final_arr, i), 0);
            fprintf(dbg, "----\n");
        }
        fclose(dbg);
    }

    if (log) {
        fprintf(log, "Wrote %s and element_coordinates.txt and pos_debug.txt\n", out_path);
        fflush(log);
        fclose(log);
    }

    cJSON_Delete(final_arr);
    if (root) cJSON_Delete(root);
    return 1;
}
