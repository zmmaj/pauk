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

/* Block tag detection */
static int is_block_tag_str(const char *tag) {
    if (!tag) return 1;
    const char *blocks[] = { "div","p","h1","h2","h3","h4","h5","h6", "ul","ol","li","table","tr","td","th","form", "header","footer","article","section","nav","aside","main", "details","figure","figcaption","pre","blockquote", "iframe","audio","video","canvas","source","track", NULL };
    for (int i=0; blocks[i]; i++) if (strcasecmp(blocks[i], tag) == 0) return 1;
    return 0;
}

/* layout_inline_children:
   children_arr: cJSON array of children
   start_index: index in children_arr to start from
   total_count: total number of items in children_arr
   start_x, start_y: absolute starting coordinates
   container_width: available width in px
   parent_font_size: font size to use for text nodes (or 0 to use default)
   consumed_out: out param, set to number of child items consumed from start_index
   Returns total height used (px)
*/
static int layout_inline_children(cJSON *children_arr, int start_index, int total_count, int start_x, int start_y, int container_width, int parent_font_size, int *consumed_out) {
    if (!children_arr || !cJSON_IsArray(children_arr) || start_index >= total_count) {
        if (consumed_out) *consumed_out = 0;
        return 0;
    }

    /* open append log (best-effort) */
    FILE *log = fopen("/data/web/layout_log.txt", "a");
    if (!log) log = fopen("layout_log.txt", "a");

    int x = start_x;
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

        cJSON *type_item = cJSON_GetObjectItem(child, "type");
        const char *child_type = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

        if ((child_type && strcmp(child_type, "text") == 0) || (!child_tag || strlen(child_tag) == 0)) {
            const char *text = get_string(child, "text");
            int fs = get_int(child, "font_size", parent_font_size > 0 ? parent_font_size : DEFAULT_FONT_SIZE);
            int tw, th;
            estimate_text_size(text, fs, right_bound - x, &tw, &th);
            if (x + tw > right_bound) { y += line_height; x = start_x; }
            cJSON_ReplaceItemInObject(child, "x", cJSON_CreateNumber(x));
            cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(y));
            cJSON_ReplaceItemInObject(child, "layout_width", cJSON_CreateNumber(tw));
            cJSON_ReplaceItemInObject(child, "layout_height", cJSON_CreateNumber(th));
            if (log) fprintf(log, "  text idx=%d tag=%s -> x=%d y=%d w=%d h=%d\n", i, child_tag?child_tag:"(text)", x, y, tw, th);
            x += tw + 1;
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
            if (x + iw > right_bound) { y += line_height; x = start_x; }
            cJSON_ReplaceItemInObject(child, "x", cJSON_CreateNumber(x));
            cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(y));
            cJSON_ReplaceItemInObject(child, "layout_width", cJSON_CreateNumber(iw));
            cJSON_ReplaceItemInObject(child, "layout_height", cJSON_CreateNumber(ih));
            if (log) fprintf(log, "  elem idx=%d tag=%s -> x=%d y=%d w=%d h=%d (ref=%s)\n",
                             i, child_tag?child_tag:"(elem)", x, y, iw, ih,
                             (get_string(child,"src") ? get_string(child,"src") : "(no-src)"));
            x += iw + 1;
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
    cJSON *xi = cJSON_GetObjectItem(node, "x");
    if (!xi) cJSON_AddNumberToObject(node, "x", x); else cJSON_SetNumberValue(xi, x);
    cJSON *yi = cJSON_GetObjectItem(node, "y");
    if (!yi) cJSON_AddNumberToObject(node, "y", y); else cJSON_SetNumberValue(yi, y);

    int layout_w = 0;
    cJSON *lw = cJSON_GetObjectItem(node, "layout_width");
    if (lw && cJSON_IsNumber(lw) && lw->valuedouble > 0) layout_w = (int)lw->valuedouble;
    else {
        const char *width_str = get_string(node, "width");
        if (width_str) {
            int px;
            if (parse_length_to_px(width_str, container_width, &px)) layout_w = px;
        }
    }
    if (layout_w <= 0) layout_w = container_width > 0 ? container_width : DEFAULT_VIEWPORT_WIDTH;

    int ref_w=0, ref_h=0;
    if (read_estimated_size_from_refs(node, base_dir, &ref_w, &ref_h)) {
        layout_w = ref_w;
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(ref_h));
        return ref_h;
    }

    cJSON *is_img = cJSON_GetObjectItem(node, "is_image");
    if (is_img && cJSON_IsTrue(is_img)) {
        const char *aw = get_string(node, "attr_width");
        const char *ah = get_string(node, "attr_height");
        int ipw=0, iph=0;
        if (aw && ah && parse_length_to_px(aw, container_width, &ipw) && parse_length_to_px(ah, container_width, &iph)) {
            cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(ipw));
            cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(iph));
            return iph;
        }
    }

    const char *tag = get_string(node, "tag");
    cJSON *type_item = cJSON_GetObjectItem(node, "type");
    const char *type_str = (type_item && cJSON_IsString(type_item)) ? type_item->valuestring : NULL;

    if ((type_str && strcmp(type_str, "text") == 0) || (!tag || strlen(tag) == 0)) {
        const char *text = get_string(node, "text");
        int fs = get_int(node, "font_size", DEFAULT_FONT_SIZE);
        int w,h;
        estimate_text_size(text, fs, layout_w, &w, &h);
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(h));
        return h;
    }

    int is_block = is_block_tag_str(tag);

    if (is_block) {
        int cursor_y = y;
        cJSON *children = cJSON_GetObjectItem(node, "children");
        if (children && cJSON_IsArray(children)) {
            int count = cJSON_GetArraySize(children);
            for (int i = 0; i < count; i++) {
                cJSON *child = cJSON_GetArrayItem(children, i);
                if (!child) continue;
                const char *child_tag = get_string(child, "tag");
                if (is_block_tag_str(child_tag)) {
                    int ch = layout_node_recursive(child, x, cursor_y, layout_w, base_dir);
                    /* force child's x/y to parent's flow */
                    cJSON_ReplaceItemInObject(child, "x", cJSON_CreateNumber(x));
                    cJSON_ReplaceItemInObject(child, "y", cJSON_CreateNumber(cursor_y));
                    cursor_y += ch;
                } else {
                    int consumed = 0;
                    int used_h = layout_inline_children(children, i, count, x, cursor_y, layout_w, get_int(node, "font_size", DEFAULT_FONT_SIZE), &consumed);
                    if (consumed > 0) i += (consumed - 1);
                    cursor_y += used_h;
                }
            }
        }
        int used = cursor_y - y;
        if (used <= 0) used = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(used));
        return used;
    }

    /* Inline element */
    cJSON *children = cJSON_GetObjectItem(node, "children");
    if (children && cJSON_IsArray(children)) {
        int total = cJSON_GetArraySize(children);
        int consumed = 0;
        int used_h = layout_inline_children(children, 0, total, x, y, layout_w, get_int(node, "font_size", DEFAULT_FONT_SIZE), &consumed);
        cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
        cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(used_h));
        return used_h;
    }

    int fh = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
    cJSON_ReplaceItemInObject(node, "layout_width", cJSON_CreateNumber(layout_w));
    cJSON_ReplaceItemInObject(node, "layout_height", cJSON_CreateNumber(fh));
    return fh;
}

/* Ensure every node has numeric x/y/layout_width/layout_height and assign inline X positions
   when layout pass left them unset. */
   static void finalize_positions_recursive(cJSON *node, int parent_x, int parent_y) {
    if (!node) return;

    /* Ensure node has layout fields */
    cJSON *lw = cJSON_GetObjectItem(node, "layout_width");
    cJSON *lh = cJSON_GetObjectItem(node, "layout_height");
    int node_w = lw && cJSON_IsNumber(lw) ? (int)lw->valuedouble : DEFAULT_VIEWPORT_WIDTH;
    int node_h = lh && cJSON_IsNumber(lh) ? (int)lh->valuedouble : (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);

    /* Ensure node has x/y (fallback to parent) */
    cJSON *xj = cJSON_GetObjectItem(node, "x");
    cJSON *yj = cJSON_GetObjectItem(node, "y");
    if (!(xj && cJSON_IsNumber(xj))) cJSON_ReplaceItemInObject(node, "x", cJSON_CreateNumber(parent_x));
    if (!(yj && cJSON_IsNumber(yj))) cJSON_ReplaceItemInObject(node, "y", cJSON_CreateNumber(parent_y));

    int node_x = (int)cJSON_GetObjectItem(node, "x")->valuedouble;
    int node_y = (int)cJSON_GetObjectItem(node, "y")->valuedouble;

    /* If no children, nothing more to do */
    cJSON *children = cJSON_GetObjectItem(node, "children");
    if (!children || !cJSON_IsArray(children)) return;

    /* Prepare inline placement state */
    int count = cJSON_GetArraySize(children);
    int cur_x = node_x;
    int cur_y = node_y;
    int line_height = (int)ceil(DEFAULT_FONT_SIZE * LINE_HEIGHT_MULT);
    int max_width = node_w > 0 ? node_w : DEFAULT_VIEWPORT_WIDTH;

    for (int i = 0; i < count; i++) {
        cJSON *ch = cJSON_GetArrayItem(children, i);
        if (!ch) continue;
        const char *tag = get_string(ch, "tag");

        /* Determine child's layout size (fallbacks) */
        cJSON *cw = cJSON_GetObjectItem(ch, "layout_width");
        cJSON *chh = cJSON_GetObjectItem(ch, "layout_height");
        int child_w = cw && cJSON_IsNumber(cw) ? (int)cw->valuedouble : 0;
        int child_h = chh && cJSON_IsNumber(chh) ? (int)chh->valuedouble : line_height;

        /* If block child -> place at node_x, node_y (stack) */
        if (is_block_tag_str(tag)) {
            /* ensure child's x/y are parent's x and current cursor y */
            cJSON_ReplaceItemInObject(ch, "x", cJSON_CreateNumber(node_x));
            cJSON_ReplaceItemInObject(ch, "y", cJSON_CreateNumber(cur_y));
            /* recurse with the child's coordinates */
            finalize_positions_recursive(ch, node_x, cur_y);
            /* advance vertical cursor by child's height (use computed if present) */
            cJSON *child_lh = cJSON_GetObjectItem(ch, "layout_height");
            int used_h = child_lh && cJSON_IsNumber(child_lh) ? (int)child_lh->valuedouble : child_h;
            cur_y += used_h;
            /* reset line cursor */
            cur_x = node_x;
        } else {
/* Inline child: ensure a usable width */
if (child_w <= 0) {
    /* try to derive width from tag/text */
    if (get_string(ch, "text")) {
        int tw, th;
        estimate_text_size(get_string(ch, "text"),
                           get_int(ch, "font_size", DEFAULT_FONT_SIZE),
                           max_width, &tw, &th);
        child_w = tw;
        child_h = th;
    } else {
        /* fallback width for images/media/unknown inline items */
        child_w = 100;
        /* ensure a reasonable height too */
        if (child_h <= 0) child_h = line_height;
    }
}

            /* Wrap if exceed parent's width */
            if (cur_x + child_w > node_x + max_width) {
                cur_y += line_height;
                cur_x = node_x;
            }

                     /* Force inline child's absolute position to current inline cursor */
                     cJSON_ReplaceItemInObject(ch, "x", cJSON_CreateNumber(cur_x));
                     cJSON_ReplaceItemInObject(ch, "y", cJSON_CreateNumber(cur_y));
         
                     /* Recurse with these coordinates */
                     finalize_positions_recursive(ch, cur_x, cur_y);
         
                     /* Advance horizontal cursor */
                     cur_x += child_w + 1;
            /* update line_height if child's height bigger */
            cJSON *child_lh = cJSON_GetObjectItem(ch, "layout_height");
            if (child_lh && cJSON_IsNumber(child_lh)) {
                int chh_val = (int)child_lh->valuedouble;
                if (chh_val > line_height) line_height = chh_val;
            } else if (child_h > line_height) {
                line_height = child_h;
            }
        }
    }

    /* Update parent's layout height to include used space (if larger than before) */
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
                cJSON *dup = cJSON_Duplicate(ch, 1);
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