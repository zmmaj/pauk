
#include "menus_parser.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Static storage
static MenuToExtract *menus_to_extract = NULL;
static int menu_count = 0;
static int menu_capacity = 0;


// ===== ORIENTATION DETECTION =====
const char* detect_menu_orientation(lxb_dom_element_t *menu_elem) {
    // Check CSS styles first
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(menu_elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            cJSON *display = cJSON_GetObjectItem(styles, "display");
            cJSON *flex_direction = cJSON_GetObjectItem(styles, "flex-direction");
            
            if (display && cJSON_IsString(display)) {
                if (strstr(display->valuestring, "flex") || 
                    strstr(display->valuestring, "inline-flex")) {
                    if (flex_direction && cJSON_IsString(flex_direction)) {
                        if (strstr(flex_direction->valuestring, "column") ||
                            strstr(flex_direction->valuestring, "column-reverse")) {
                            cJSON_Delete(styles);
                            return "vertical";
                        }
                    }
                    // Flex without column direction is usually horizontal
                    cJSON_Delete(styles);
                    return "horizontal";
                }
            }
            
            cJSON_Delete(styles);
        }
    }
    
    // Check common class names
    size_t len;
    const lxb_char_t *class_attr = lxb_dom_element_class(menu_elem, &len);
    if (class_attr && len > 0) {
        char *class_str = malloc(len + 1);
        memcpy(class_str, class_attr, len);
        class_str[len] = '\0';
        
        const char *horizontal_indicators[] = {
            "horizontal", "h-menu", "nav-horizontal", "menu-row",
            "navbar-horizontal", "nav-inline", "menu-inline",
            "flex-row", "row", NULL
        };
        
        const char *vertical_indicators[] = {
            "vertical", "v-menu", "nav-vertical", "menu-column",
            "navbar-vertical", "nav-stacked", "menu-stacked",
            "flex-column", "column", "sidebar", NULL
        };
        
        for (int i = 0; horizontal_indicators[i] != NULL; i++) {
            if (strstr(class_str, horizontal_indicators[i]) != NULL) {
                free(class_str);
                return "horizontal";
            }
        }
        
        for (int i = 0; vertical_indicators[i] != NULL; i++) {
            if (strstr(class_str, vertical_indicators[i]) != NULL) {
                free(class_str);
                return "vertical";
            }
        }
        
        free(class_str);
    }
    
    // Default based on element type
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(menu_elem, &len);
    if (tag_name && len > 0) {
        char *tag = malloc(len + 1);
        memcpy(tag, tag_name, len);
        tag[len] = '\0';
        
        // <nav> is often horizontal, <aside> is often vertical
        if (strcasecmp(tag, "aside") == 0 || strcasecmp(tag, "sidebar") == 0) {
            free(tag);
            return "vertical";
        }
        free(tag);
    }
    
    return "horizontal"; // Default
}


static lxb_dom_element_t* find_link_in_item(lxb_dom_element_t *item_elem) {
    lxb_dom_node_t *item_node = lxb_dom_interface_node(item_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(item_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "a") == 0) {
                    free(tag);
                    return child_elem;
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    return NULL;
}
/*
static void calculate_menu_dimensions(cJSON *menu_json) {
    if (!menu_json) return;
    
    cJSON *items = cJSON_GetObjectItem(menu_json, "items");
    cJSON *orientation = cJSON_GetObjectItem(menu_json, "orientation");
    
    int item_count = 0;
    if (items && cJSON_IsArray(items)) {
        item_count = cJSON_GetArraySize(items);
    }
    
    // Estimate menu size based on orientation
    int estimated_width, estimated_height;
    
    if (orientation && strcmp(orientation->valuestring, "vertical") == 0) {
        // Vertical menu: narrower but taller
        estimated_width = 200; // Fixed width for vertical menus
        estimated_height = item_count * 40 + 20; // ~40px per item + padding
    } else {
        // Horizontal menu: wider but shorter
        estimated_width = item_count * 120; // ~120px per item
        estimated_height = 50; // Standard menu height
    }
    
    cJSON_AddNumberToObject(menu_json, "estimated_width", estimated_width);
    cJSON_AddNumberToObject(menu_json, "estimated_height", estimated_height);
    
    // ONLY add item_count if it doesn't already exist
    if (!cJSON_GetObjectItem(menu_json, "item_count")) {
        cJSON_AddNumberToObject(menu_json, "item_count", item_count);
    }
}

static int is_nav_element(lxb_dom_element_t *elem) {
    size_t len;
    const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &len);
    if (!tag || len != 3) return 0;
    return strncasecmp((char*)tag, "nav", 3) == 0;
}


static void extract_list_as_menu(lxb_dom_element_t *list_elem, cJSON *items_array, int *item_index, int nesting_level) {
    lxb_dom_node_t *list_node = lxb_dom_interface_node(list_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(list_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t tag_len;
            const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);
            
            if (tag_name && tag_len > 0) {
                char *tag = malloc(tag_len + 1);
                memcpy(tag, tag_name, tag_len);
                tag[tag_len] = '\0';
                
                if (strcasecmp(tag, "li") == 0) {
                    cJSON *item_json = extract_menu_item(child_elem, (*item_index)++, nesting_level);
                    if (item_json) {
                        cJSON_AddItemToArray(items_array, item_json);
                    }
                }
                
                free(tag);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
}

static void extract_nav_content(lxb_dom_node_t *node, cJSON *items_array, 
    int *item_index, int nesting_level) {
// Keep track of visited elements
static lxb_dom_element_t *visited[100];
static int visited_count = 0;
int child_count = 0;
lxb_dom_node_t *child = lxb_dom_node_first_child(node);

while (child) {
if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);

// Check if already visited
int already_visited = 0;
for (int i = 0; i < visited_count; i++) {
if (visited[i] == child_elem) {
already_visited = 1;
break;
}
}

if (already_visited) {
child = lxb_dom_node_next(child);
continue;
}

// Mark as visited
visited[visited_count++] = child_elem;

size_t tag_len;
const lxb_char_t *tag_name = lxb_dom_element_qualified_name(child_elem, &tag_len);

if (tag_name && tag_len > 0) {
char *tag = malloc(tag_len + 1);
memcpy(tag, tag_name, tag_len);
tag[tag_len] = '\0';

printf("  child %d: <%s> at %p\n", child_count, tag, child_elem);

if (strcasecmp(tag, "ul") == 0 || strcasecmp(tag, "ol") == 0) {
// Only extract from top-level lists
if (nesting_level == 0) {
extract_list_as_menu(child_elem, items_array, item_index, nesting_level);
} else {
// For nested lists, still process but don't extract items
extract_list_as_menu(child_elem, items_array, item_index, nesting_level + 1);
}
}
else if (strcasecmp(tag, "a") == 0) {
// Only add top-level links
if (nesting_level == 0) {
cJSON *item_json = extract_menu_item(child_elem, (*item_index)++, nesting_level);
if (item_json) {
cJSON_AddItemToArray(items_array, item_json);
}
}
}
else if (strcasecmp(tag, "div") == 0 || strcasecmp(tag, "section") == 0) {
// Continue recursion at same nesting level
extract_nav_content(child, items_array, item_index, nesting_level);
}

free(tag);
}
}

child = lxb_dom_node_next(child);
}
}

static void extract_menu_content(lxb_dom_node_t *node, cJSON *items_array, int *item_index, int nesting_level) {
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            
            // <menu> should contain <li> or <menuitem> elements
            cJSON *item_json = extract_menu_item(child_elem, (*item_index)++, nesting_level);
            if (item_json) {
                cJSON_AddItemToArray(items_array, item_json);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
}

*/

// ===== GETTER FUNCTIONS =====
int get_menu_count(void) {
    return menu_count;
}

const char* get_menu_filename(int index) {
    if (index < 0 || index >= menu_count) return NULL;
    return menus_to_extract[index].filename;
}

lxb_dom_element_t* get_menu_element(int index) {
    if (index < 0 || index >= menu_count) return NULL;
    return menus_to_extract[index].menu_elem;
}

const char* get_menu_type(int index) {
    if (index < 0 || index >= menu_count) return NULL;
    return menus_to_extract[index].menu_type;
}
// ============================
/*
// ===== STYLE HELPER FUNCTIONS =====
static void add_default_menu_styles(cJSON *styles, const char *menu_type) {
    if (!cJSON_GetObjectItem(styles, "display")) {
        cJSON_AddStringToObject(styles, "display", "flex");
    }
    if (!cJSON_GetObjectItem(styles, "flex-direction")) {
        cJSON_AddStringToObject(styles, "flex-direction", "row");
    }
    if (!cJSON_GetObjectItem(styles, "list-style")) {
        cJSON_AddStringToObject(styles, "list-style", "none");
    }
    if (!cJSON_GetObjectItem(styles, "margin")) {
        cJSON_AddStringToObject(styles, "margin", "0");
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "10px");
    }
    if (!cJSON_GetObjectItem(styles, "background-color")) {
        cJSON_AddStringToObject(styles, "background-color", "#333333");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#ffffff");
    }
}

static cJSON* create_default_menu_styles(const char *menu_type) {
    cJSON *styles = cJSON_CreateObject();
    
    cJSON_AddStringToObject(styles, "display", "flex");
    cJSON_AddStringToObject(styles, "flex-direction", "row");
    cJSON_AddStringToObject(styles, "list-style", "none");
    cJSON_AddStringToObject(styles, "margin", "0");
    cJSON_AddStringToObject(styles, "padding", "10px");
    cJSON_AddStringToObject(styles, "background-color", "#333333");
    cJSON_AddStringToObject(styles, "color", "#ffffff");
    cJSON_AddStringToObject(styles, "font-family", "Arial, sans-serif");
    cJSON_AddStringToObject(styles, "font-size", "14px");
    
    return styles;
}
*/
static void add_default_menu_item_styles(cJSON *styles, int nesting_level) {
    if (!cJSON_GetObjectItem(styles, "margin")) {
        if (nesting_level == 0) {
            cJSON_AddStringToObject(styles, "margin", "0 15px");
        } else {
            cJSON_AddStringToObject(styles, "margin", "5px 0");
        }
    }
    if (!cJSON_GetObjectItem(styles, "padding")) {
        cJSON_AddStringToObject(styles, "padding", "8px 12px");
    }
    if (!cJSON_GetObjectItem(styles, "cursor")) {
        cJSON_AddStringToObject(styles, "cursor", "pointer");
    }
    if (!cJSON_GetObjectItem(styles, "text-decoration")) {
        cJSON_AddStringToObject(styles, "text-decoration", "none");
    }
    if (!cJSON_GetObjectItem(styles, "color")) {
        cJSON_AddStringToObject(styles, "color", "#ffffff");
    }
}

static cJSON* create_default_menu_item_styles(int nesting_level) {
    cJSON *styles = cJSON_CreateObject();
    
    if (nesting_level == 0) {
        cJSON_AddStringToObject(styles, "margin", "0 15px");
    } else {
        cJSON_AddStringToObject(styles, "margin", "5px 0");
    }
    
    cJSON_AddStringToObject(styles, "padding", "8px 12px");
    cJSON_AddStringToObject(styles, "cursor", "pointer");
    cJSON_AddStringToObject(styles, "text-decoration", "none");
    cJSON_AddStringToObject(styles, "color", "#ffffff");
    cJSON_AddStringToObject(styles, "display", "block");
    
    // Hover effect for top-level items
    if (nesting_level == 0) {
        cJSON_AddStringToObject(styles, ":hover_background", "#555555");
        cJSON_AddStringToObject(styles, ":hover_color", "#ffffff");
    }
    
    return styles;
}
// ===============================

void store_menu_for_extraction(lxb_dom_element_t *menu_elem, const char *filename, const char *menu_type) {
    if (!menu_elem || !filename || !menu_type) return;
    
    // Resize array if needed
    if (menu_count >= menu_capacity) {
        menu_capacity = menu_capacity == 0 ? 4 : menu_capacity * 2;
        menus_to_extract = realloc(menus_to_extract, menu_capacity * sizeof(MenuToExtract));
    }
    
    // Detect orientation and create combined type
    const char *orientation = detect_menu_orientation(menu_elem);
    char full_type[128];
    snprintf(full_type, sizeof(full_type), "%s_%s", orientation, menu_type);
    
    // Store menu
    menus_to_extract[menu_count].menu_elem = menu_elem;
    menus_to_extract[menu_count].filename = strdup(filename);
    menus_to_extract[menu_count].menu_type = strdup(full_type); // Use combined type
    menus_to_extract[menu_count].menu_index = menu_count + 1;
    menu_count++;
    
    printf("DEBUG: Stored menu %s as %s\n", full_type, filename);
}

cJSON *extract_menu_fast(lxb_dom_element_t *nav_elem) {
    if (!nav_elem) return NULL;

    cJSON *menu = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    cJSON_AddStringToObject(menu, "type", "menu");
    cJSON_AddItemToObject(menu, "items", items);

    lxb_dom_node_t *node = lxb_dom_interface_node(nav_elem)->first_child;

    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            const lxb_char_t *tag; size_t len;
            tag = lxb_dom_element_tag_name(el,&len);

            if (len==2 && memcmp(tag,"ul",2)==0) {
                lxb_dom_node_t *li = node->first_child;
                while (li) {
                    if (li->type==LXB_DOM_NODE_TYPE_ELEMENT) {
                        lxb_dom_element_t *li_el = lxb_dom_interface_element(li);
                        const lxb_char_t *li_tag; size_t li_len;
                        li_tag = lxb_dom_element_tag_name(li_el,&li_len);
                        if (li_len==2 && memcmp(li_tag,"li",2)==0) {
                            lxb_dom_node_t *a = li->first_child;
                            while(a){
                                if(a->type==LXB_DOM_NODE_TYPE_ELEMENT){
                                    lxb_dom_element_t *a_el = lxb_dom_interface_element(a);
                                    const lxb_char_t *atag; size_t alen;
                                    atag = lxb_dom_element_tag_name(a_el,&alen);
                                    if(alen==1 && atag[0]=='a'){
                                        cJSON *item = cJSON_CreateObject();

                                        /* href */
                                        const lxb_char_t *href =
                                          lxb_dom_element_get_attribute(a_el,(lxb_char_t*)"href",4,NULL);
                                        if(href) cJSON_AddStringToObject(item,"href",(char*)href);

                                        /* text */
                                        lxb_dom_node_t *t = a->first_child;
                                        if(t && t->type==LXB_DOM_NODE_TYPE_TEXT){
                                            lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(t);
                                            size_t len = cd->data.length;
                                            if(len>255) len=255;
                                            char buffer[256];
                                            memcpy(buffer,cd->data.data,len);
                                            buffer[len]=0;
                                            cJSON_AddStringToObject(item,"text",buffer);
                                        }

                                        if(cJSON_GetObjectItem(item,"text") || cJSON_GetObjectItem(item,"href"))
                                            cJSON_AddItemToArray(items,item);
                                        else
                                            cJSON_Delete(item);
                                        break; // only first <a> per li
                                    }
                                }
                                a=a->next;
                            }
                        }
                    }
                    li = li->next;
                }
            }
        }
        node=node->next;
    }

    return menu;
}

cJSON* extract_menu_items(lxb_dom_element_t *menu_elem)
{
    if (!menu_elem) return NULL;

    cJSON *items_array = cJSON_CreateArray();
    int item_index = 0;

    lxb_dom_node_t *root = lxb_dom_interface_node(menu_elem);
    lxb_dom_node_t *node = root;

    while (node) {

        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {

            lxb_dom_element_t *el = lxb_dom_interface_element(node);

            const lxb_char_t *tag;
            size_t tag_len;

            tag = lxb_dom_element_tag_name(el, &tag_len);

            if (tag_len == 1 && tag[0] == 'a') {

                cJSON *item_json = cJSON_CreateObject();

                size_t text_len;
                lxb_char_t *text =
                    lxb_dom_node_text_content(node, &text_len);

                if (text && text_len > 0) {
                    char *tmp = malloc(text_len + 1);
                    memcpy(tmp, text, text_len);
                    tmp[text_len] = 0;

                    cJSON_AddStringToObject(item_json, "text", tmp);

                    free(tmp);
                    lxb_dom_document_destroy_text(node->owner_document, text);
                }

                const lxb_char_t *href =
                    lxb_dom_element_get_attribute(el,
                        (const lxb_char_t *)"href",4,NULL);

                if (href) {
                    cJSON_AddStringToObject(item_json,"href",(char*)href);
                }

                cJSON_AddNumberToObject(item_json,"item_index",item_index++);
                cJSON_AddItemToArray(items_array,item_json);
            }
        }

        if (node->first_child) {
            node = node->first_child;
            continue;
        }

        while (node && node != root && node->next == NULL) {
            node = node->parent;
        }

        if (node == root)
            break;

        node = node->next;
    }

    return items_array;
}


cJSON* extract_menu_item(lxb_dom_element_t *item_elem, int item_index, int nesting_level) {
    static int call_count = 0;
    call_count++;
    printf("🔍 extract_menu_item called #%d for element %p, index=%d, level=%d\n", 
           call_count, item_elem, item_index, nesting_level);
   
    cJSON *item_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(item_json, "item_index", item_index);
    cJSON_AddNumberToObject(item_json, "nesting_level", nesting_level);
    
    // Get item type
    size_t tag_len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(item_elem, &tag_len);
    if (tag_name && tag_len > 0) {
        char *tag = malloc(tag_len + 1);
        memcpy(tag, tag_name, tag_len);
        tag[tag_len] = '\0';
        cJSON_AddStringToObject(item_json, "tag", tag);
        free(tag);
    }
    
    // Get item attributes
    size_t len;
    const lxb_char_t *item_id = lxb_dom_element_id(item_elem, &len);
    if (item_id && len > 0) {
        char *id_str = malloc(len + 1);
        memcpy(id_str, item_id, len);
        id_str[len] = '\0';
        cJSON_AddStringToObject(item_json, "id", id_str);
        free(id_str);
    }
    
    const lxb_char_t *item_class = lxb_dom_element_class(item_elem, &len);
    if (item_class && len > 0) {
        char *class_str = malloc(len + 1);
        memcpy(class_str, item_class, len);
        class_str[len] = '\0';
        cJSON_AddStringToObject(item_json, "class", class_str);
        free(class_str);
    }
    
    // Check if this is a link
    lxb_dom_element_t *link_elem = find_link_in_item(item_elem);
    if (link_elem) {
        const lxb_char_t *href = lxb_dom_element_get_attribute(
            link_elem, (lxb_char_t*)"href", 4, &len);
        if (href && len > 0) {
            char *href_str = malloc(len + 1);
            memcpy(href_str, href, len);
            href_str[len] = '\0';
            cJSON_AddStringToObject(item_json, "href", href_str);
            free(href_str);
        }
        
        // Get link text
        char *link_text = get_element_text_simple(link_elem);
        if (link_text && strlen(link_text) > 0) {
            cJSON_AddStringToObject(item_json, "text", link_text);
        }
        if (link_text) free(link_text);
    } else {
        // Get regular text content
        char *item_text = get_element_text_simple(item_elem);
        if (item_text && strlen(item_text) > 0) {
            cJSON_AddStringToObject(item_json, "text", item_text);
        }
        if (item_text) free(item_text);
    }
    
    // Get item styles
    lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_id(item_elem, LXB_DOM_ATTR_STYLE);
    if (style_attr) {
        cJSON *styles = parse_inline_styles_simple(style_attr);
        if (styles) {
            add_default_menu_item_styles(styles, nesting_level);
            cJSON_AddItemToObject(item_json, "style", styles);
        } else {
            cJSON *styles = create_default_menu_item_styles(nesting_level);
            cJSON_AddItemToObject(item_json, "style", styles);
        }
    } else {
        cJSON *styles = create_default_menu_item_styles(nesting_level);
        cJSON_AddItemToObject(item_json, "style", styles);
    }
    
    // Check for nested menus/submenus
    cJSON *submenus = cJSON_CreateArray();
    int submenu_count = 0;
    
    lxb_dom_node_t *item_node = lxb_dom_interface_node(item_elem);
    lxb_dom_node_t *child = lxb_dom_node_first_child(item_node);
    
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *child_elem = lxb_dom_interface_element(child);
            size_t child_tag_len;
            const lxb_char_t *child_tag = lxb_dom_element_qualified_name(child_elem, &child_tag_len);
            
            if (child_tag && child_tag_len > 0) {
                char *child_tag_str = malloc(child_tag_len + 1);
                memcpy(child_tag_str, child_tag, child_tag_len);
                child_tag_str[child_tag_len] = '\0';
                
                if (strcasecmp(child_tag_str, "ul") == 0 || 
                    strcasecmp(child_tag_str, "ol") == 0 ||
                    strcasecmp(child_tag_str, "menu") == 0) {
                    // Found a submenu
                    cJSON *submenu = extract_menu_structure(child_elem);
                    if (submenu) {
                        cJSON_AddNumberToObject(submenu, "parent_item_index", item_index);
                        cJSON_AddNumberToObject(submenu, "nesting_level", nesting_level + 1);
                        cJSON_AddItemToArray(submenus, submenu);
                        submenu_count++;
                    }
                }
                
                free(child_tag_str);
            }
        }
        
        child = lxb_dom_node_next(child);
    }
    
    if (submenu_count > 0) {
        cJSON_AddItemToObject(item_json, "submenus", submenus);
        cJSON_AddNumberToObject(item_json, "submenu_count", submenu_count);
    } else {
        cJSON_Delete(submenus);
    }
    
    return item_json;
}



void cleanup_menus_storage(void) {
    for (int i = 0; i < menu_count; i++) {
        if (menus_to_extract[i].filename) {
            free(menus_to_extract[i].filename);
        }
        if (menus_to_extract[i].menu_type) {
            free((char*)menus_to_extract[i].menu_type);
        }
    }
    free(menus_to_extract);
    menus_to_extract = NULL;
    menu_count = menu_capacity = 0;
}
