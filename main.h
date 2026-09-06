#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/css/css.h>
#include <cjson.h>
#include "css_parser.h"
#include "js_executor_quickjs.h"

#define DEBUG 1

// Safe string copy from Lexbor string to C string
char* lexbor_to_cstr(const lxb_char_t *lb_str, size_t len) {
    if (!lb_str || len == 0) return strdup("");
    char *cstr = malloc(len + 1);
    if (!cstr) return NULL;
    memcpy(cstr, lb_str, len);
    cstr[len] = '\0';
    return cstr;
}

// Write JSON to file
void write_json_to_file(const char *filename, cJSON *json) {
    FILE *f = fopen(filename, "wb");
    if (!f) { perror("Failed to open output file"); return; }
    char *json_str = cJSON_Print(json);
    if (json_str) {
        fputs(json_str, f);
        free(json_str);
    }
    fclose(f);
}

// Trim whitespace helper
char* trim_whitespace(char *str) {
    if (!str) return NULL;
    while (*str == ' ' || *str == '\n' || *str == '\t' || *str == '\r') str++;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\n' || *end == '\t' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return str;
}

// Get element text (concatenate all text nodes inside element)
char* get_element_text(lxb_dom_element_t *elem) {
    if (!elem) return strdup("");
    lxb_dom_node_t *node = lxb_dom_interface_node(elem);
    if (!node) return strdup("");

    const lxb_char_t *text = lxb_dom_node_text_content(node, NULL);
    if (!text) return strdup("");

    size_t len = strlen((char*)text);
    char *cstr = malloc(len + 1);
    if (!cstr) return strdup("");
    memcpy(cstr, text, len);
    cstr[len] = '\0';
    return cstr;
}

// Convert element attributes to JSON object
cJSON* element_attributes_to_json(lxb_dom_element_t *elem) {
    if (!elem) return NULL;
    lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(elem);
    if (!attr) return NULL;

    cJSON *attrs = cJSON_CreateObject();
    int has_attrs = 0;
    size_t len;

    while (attr) {
        const lxb_char_t *name = lxb_dom_attr_qualified_name(attr, &len);
        const lxb_char_t *value = lxb_dom_attr_value(attr, NULL);
        if (name && value) {
            char *name_str = lexbor_to_cstr(name, len);
            char *value_str = lexbor_to_cstr(value, strlen((char*)value));
            if (name_str && value_str) {
                char *val = trim_whitespace(value_str);
                cJSON_AddStringToObject(attrs, name_str, val);
                has_attrs = 1;
            }
            free(name_str);
            free(value_str);
        }
        attr = lxb_dom_element_next_attribute(attr);
    }

    if (!has_attrs) {
        cJSON_Delete(attrs);
        return NULL;
    }
    return attrs;
}

// Convert single element to JSON
cJSON* element_to_json(lxb_dom_element_t *elem) {
    if (!elem) return NULL;
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;

    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &len);
    if (tag_name && len > 0) {
        char *tag = lexbor_to_cstr(tag_name, len);
        if (tag) { cJSON_AddStringToObject(json, "tag", tag); free(tag); }
    }

    const lxb_char_t *id = lxb_dom_element_id(elem, &len);
    if (id && len > 0) {
        char *id_str = lexbor_to_cstr(id, len);
        if (id_str) { cJSON_AddStringToObject(json, "id", id_str); free(id_str); }
    }

    const lxb_char_t *cls = lxb_dom_element_class(elem, &len);
    if (cls && len > 0) {
        char *cls_str = lexbor_to_cstr(cls, len);
        if (cls_str) { cJSON_AddStringToObject(json, "class", cls_str); free(cls_str); }
    }

    cJSON *attrs = element_attributes_to_json(elem);
    if (attrs) cJSON_AddItemToObject(json, "attrs", attrs);

    char *text = get_element_text(elem);
    if (text) {
        char *trimmed = trim_whitespace(text);
        if (strlen(trimmed) > 0) cJSON_AddStringToObject(json, "text", trimmed);
        free(text);
    }

    return json;
}

// Recursively traverse DOM tree
cJSON* traverse_dom_node(lxb_dom_node_t *node) {
    if (!node) return NULL;

    cJSON *json_node = NULL;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        json_node = element_to_json(elem);

        lxb_dom_node_t *child = lxb_dom_node_first_child(node);
        if (child) {
            cJSON *children_array = cJSON_CreateArray();
            while (child) {
                cJSON *child_json = traverse_dom_node(child);
                if (child_json) cJSON_AddItemToArray(children_array, child_json);
                child = lxb_dom_node_next(child);
            }
            if (cJSON_GetArraySize(children_array) > 0)
                cJSON_AddItemToObject(json_node, "children", children_array);
            else
                cJSON_Delete(children_array);
        }
    }
    else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        const lxb_char_t *text = lxb_dom_node_text_content(node, NULL);
        if (text && strlen((char*)text) > 0) {
            cJSON *txt = cJSON_CreateObject();
            cJSON_AddStringToObject(txt, "type", "text");
            char *t = lexbor_to_cstr(text, strlen((char*)text));
            char *trimmed = trim_whitespace(t);
            if (strlen(trimmed) > 0) cJSON_AddStringToObject(txt, "content", trimmed);
            free(t);
            json_node = txt;
        }
    }

    return json_node;
}


// List of HTML inline event attributes
const char* html_event_attrs[] = {
    "onclick","ondblclick","onchange","oninput","onsubmit","onfocus","onblur",
    "onkeydown","onkeyup","onkeypress","onmouseenter","onmouseleave",NULL
};

// Extract events from element
void extract_element_events(lxb_dom_element_t *elem, cJSON *events_array) {
    if (!elem || !events_array) return;

    size_t len;
    const char **ev = html_event_attrs;
    while (*ev) {
        lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)*ev, strlen(*ev));
        if (attr) {
            const lxb_char_t *value = lxb_dom_attr_value(attr, NULL);
            if (value && strlen((char*)value) > 0) {
                cJSON *ev_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(ev_obj, "type", *ev + 2); // strip "on"
                cJSON_AddStringToObject(ev_obj, "code", (char*)value);

                const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &len);
                if (tag) {
                    char *tag_str = lexbor_to_cstr(tag, len);
                    cJSON_AddStringToObject(ev_obj, "element", tag_str);
                    free(tag_str);
                }

                const lxb_char_t *id = lxb_dom_element_id(elem, &len);
                if (id && len > 0) {
                    char *id_str = lexbor_to_cstr(id, len);
                    cJSON_AddStringToObject(ev_obj, "id", id_str);
                    free(id_str);
                }

                cJSON_AddItemToArray(events_array, ev_obj);
            }
        }
        ev++;
    }

    // Recursively extract from children
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elem));
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
            extract_element_events(lxb_dom_interface_element(child), events_array);
        child = lxb_dom_node_next(child);
    }
}


// Convert style attribute string to JSON object
cJSON* parse_inline_style(const char *style_str) {
    if (!style_str || strlen(style_str) == 0) return NULL;

    cJSON *json_style = cJSON_CreateObject();
    char *s = strdup(style_str);
    char *token = strtok(s, ";");
    while (token) {
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = 0;
            char *key = trim_whitespace(token);
            char *value = trim_whitespace(colon + 1);
            if (strlen(key) > 0 && strlen(value) > 0)
                cJSON_AddStringToObject(json_style, key, value);
        }
        token = strtok(NULL, ";");
    }
    free(s);
    return json_style;
}

// Extract inline styles from element
void extract_element_styles(lxb_dom_element_t *elem, cJSON *stylesheets_array) {
    if (!elem || !stylesheets_array) return;

    size_t len;
    const lxb_char_t *style_attr = lxb_dom_element_attr_value_by_name(elem, (lxb_char_t*)"style", 5, &len);
    if (style_attr && len > 0) {
        char *style_str = lexbor_to_cstr(style_attr, len);
        cJSON *style_json = parse_inline_style(style_str);
        if (style_json) {
            cJSON_AddStringToObject(style_json, "element", lxb_dom_element_qualified_name(elem, &len) ? lexbor_to_cstr(lxb_dom_element_qualified_name(elem, &len), len) : "");
            const lxb_char_t *id = lxb_dom_element_id(elem, &len);
            if (id && len > 0) {
                char *id_str = lexbor_to_cstr(id, len);
                cJSON_AddStringToObject(style_json, "id", id_str);
                free(id_str);
            }
            cJSON_AddItemToArray(stylesheets_array, style_json);
        }
        free(style_str);
    }

    // recursively process children
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(elem));
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
            extract_element_styles(lxb_dom_interface_element(child), stylesheets_array);
        child = lxb_dom_node_next(child);
    }
}

// Extract <style> tag CSS
void extract_style_tags(lxb_dom_node_t *root, cJSON *stylesheets_array) {
    lxb_dom_node_t *child = lxb_dom_node_first_child(root);
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *elem = lxb_dom_interface_element(child);
            size_t len;
            const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &len);
            if (tag && strncmp((char*)tag, "style", 5) == 0) {
                const lxb_char_t *text = lxb_dom_node_text_content(lxb_dom_interface_node(elem), NULL);
                if (text && strlen((char*)text) > 0) {
                    cJSON *style_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(style_json, "css", (char*)text);
                    cJSON_AddItemToArray(stylesheets_array, style_json);
                }
            }
            extract_style_tags(lxb_dom_interface_node(elem), stylesheets_array);
        }
        child = lxb_dom_node_next(child);
    }
}


void execute_scripts(lxb_dom_node_t *root) {
    lxb_dom_node_t *child = lxb_dom_node_first_child(root);
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *elem = lxb_dom_interface_element(child);
            size_t len;
            const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &len);
            if (tag && strncmp((char*)tag, "script", 6) == 0) {
                const lxb_char_t *text = lxb_dom_node_text_content(lxb_dom_interface_node(elem), NULL);
                if (text && strlen((char*)text) > 0) {
                    // Execute script via QuickJS
                    quickjs_execute((char*)text); // implement in your js_executor_quickjs.h
                }
            }
            execute_scripts(lxb_dom_interface_node(elem));
        }
        child = lxb_dom_node_next(child);
    }
}


int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s input.html output.json\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];

    FILE *f = fopen(input_file, "rb");
    if (!f) { perror("Failed to open input file"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *html_content = malloc(size + 1);
    fread(html_content, 1, size, f);
    html_content[size] = '\0';
    fclose(f);

    // Initialize Lexbor HTML parser
    lxb_html_document_t *doc = lxb_html_document_create();
    lxb_status_t status = lxb_html_document_parse(doc, (const lxb_char_t*)html_content, size, true);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "HTML parse failed\n");
        free(html_content);
        return 1;
    }

    lxb_dom_document_t *dom_doc = lxb_html_document_dom_interface(doc);

    // Prepare output JSON
    cJSON *root_json = cJSON_CreateObject();

    // DOM
    lxb_dom_node_t *body = lxb_dom_document_body(dom_doc);
    cJSON *dom_json = traverse_dom_node((lxb_dom_node_t*)body);
    cJSON_AddItemToObject(root_json, "dom", dom_json ? dom_json : cJSON_CreateObject());

    // Stylesheets
    cJSON *stylesheets_json = cJSON_CreateArray();
    if (body) {
        extract_element_styles(lxb_dom_interface_element(body), stylesheets_json);
        extract_style_tags((lxb_dom_node_t*)body, stylesheets_json);
    }
    cJSON_AddItemToObject(root_json, "stylesheets", stylesheets_json);

    // Events
    cJSON *events_json = cJSON_CreateArray();
    if (body) extract_element_events(lxb_dom_interface_element(body), events_json);
    cJSON_AddItemToObject(root_json, "events", events_json);

    // Execute JS
    execute_scripts((lxb_dom_node_t*)body);

    // Write JSON
    write_json_to_file(output_file, root_json);
    cJSON_Delete(root_json);

    free(html_content);
    lxb_html_document_destroy(doc, true);

    return 0;
}
