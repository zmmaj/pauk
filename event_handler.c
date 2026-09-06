// event_handler.c - Simple and correct implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include "main.h"
#include "event_handler.h"
#include "js_executor_quickjs.h"


#define MAX_EVENT_HANDLERS 100
#define MAX_ELEMENTS_TO_PROCESS 500  
#define MAX_ATTRIBUTES_PER_ELEMENT 20 

static EventHandler event_handlers[MAX_EVENT_HANDLERS];
static int handler_count = 0;
static int elements_processed = 0;  
static int total_handlers_found = 0; 
int g_site_complexity_score = 0;
int g_max_events_limit = 500;

// ===== OPTIMIZED: Check if element likely has events =====
static int element_likely_has_events(lxb_dom_element_t *elem) {
    // Quick check for common event attributes without iterating all attributes
    // This is a fast check that avoids full attribute iteration
    return (lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onclick", 7) ||
            lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onchange", 8) ||
            lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onsubmit", 8) ||
            lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onfocus", 7) ||
            lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onblur", 6) ||
            lxb_dom_element_has_attribute(elem, (lxb_char_t*)"onkeydown", 9));
}

// ===== OPTIMIZED: process_events_recursive with depth limit =====
void process_events_recursive(lxb_dom_node_t *node) {
    if (!node) return;
    
    static int recursion_depth = 0;
    static int total_nodes_processed = 0;
    
    // Depth limit
    if (++recursion_depth > 100) {
        printf("⚠️ Event recursion depth limit reached (100)\n");
        recursion_depth--;
        return;
    }
    
    // Total node limit
    if (total_nodes_processed > 2000) {
        if (total_nodes_processed == 2001) {
            printf("⚠️ Reached maximum node processing limit (2000)\n");
        }
        recursion_depth--;
        return;
    }
    
    while (node && total_nodes_processed <= 2000) {
        total_nodes_processed++;
        
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *elem = lxb_dom_interface_element(node);
            process_element_events(elem);
        }
        
        // Process children recursively
        lxb_dom_node_t *child = lxb_dom_node_first_child(node);
        if (child) {
            process_events_recursive(child);
        }
        
        // Move to next sibling
        node = lxb_dom_node_next(node);
    }
    
    recursion_depth--;
    
    // Print summary
    if (recursion_depth == 0 && total_nodes_processed > 0) {
        printf("📊 Event processing complete: %d nodes scanned, %d event handlers found\n",
               total_nodes_processed, handler_count);
        total_nodes_processed = 0;  // Reset for next page
    }
}


// Check if attribute name is an event attribute (starts with "on" and has valid event name)
static int is_event_attribute(const char *attr_name) {
    // Must start with "on" and be at least 3 chars ("on" + at least 1 char)
    if (!attr_name || strlen(attr_name) < 3 || 
        attr_name[0] != 'o' || attr_name[1] != 'n') {
        return 0;
    }
    
    // Common HTML event names (simplified list)
    const char *event_names[] = {
        "click", "dblclick", "mousedown", "mouseup", "mouseover", 
        "mouseout", "mousemove", "keydown", "keyup", "keypress",
        "submit", "change", "focus", "blur", "load", "unload",
        "resize", "scroll", "contextmenu", "input", "select",
        "abort", "error", "hashchange", "popstate", "pageshow",
        "pagehide", "online", "offline", NULL
    };
    
    // Skip "on" prefix
    const char *event_part = attr_name + 2;
    
    // Check if it matches a known event
    for (int i = 0; event_names[i] != NULL; i++) {
        if (strcmp(event_part, event_names[i]) == 0) {
            return 1;
        }
    }
    
    return 0;
}

// Get event type from attribute name (remove "on" prefix)
static char* get_event_type(const char *attr_name) {
    if (!attr_name || strlen(attr_name) < 3) return NULL;
    return strdup(attr_name + 2); // Skip "on"
}

// ===== RESET function for each page load =====
void event_handler_reset_limits(void) {
    elements_processed = 0;
    total_handlers_found = 0;
    printf("Event handler limits reset\n");
}


void event_handler_init(void) {
    handler_count = 0;
    event_handler_reset_limits();
}

void event_handler_cleanup(void) {
    for (int i = 0; i < handler_count; i++) {
        if (event_handlers[i].event_type) {
            free(event_handlers[i].event_type);
        }
        if (event_handlers[i].handler_code) {
            free(event_handlers[i].handler_code);
        }
    }
    handler_count = 0;
}

// Get event count - ADD THIS FUNCTION
int get_event_handler_count() {
    return handler_count;
}

// ===== OPTIMIZED: Process events with limits =====
void process_element_events(lxb_dom_element_t *elem) {
    if (!elem) return;
    
    // ===== ADD LIMIT: Stop after processing X elements =====
    if (elements_processed >= MAX_ELEMENTS_TO_PROCESS) {
        if (elements_processed == MAX_ELEMENTS_TO_PROCESS) {
            printf("⚠️ Reached element processing limit (%d), stopping\n", MAX_ELEMENTS_TO_PROCESS);
            g_site_complexity_score = 100;
            elements_processed++;
        }
        return;
    }
    
    // ===== OPTIMIZATION: Skip if no event attributes likely exist =====
    if (!element_likely_has_events(elem)) {
        elements_processed++;
        return;
    }
    
    // Get all attributes
    lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(elem);
    int attr_count = 0;
    
    while (attr && attr_count < MAX_ATTRIBUTES_PER_ELEMENT) {
        attr_count++;
        
        size_t attr_name_len;
        const lxb_char_t *attr_name = lxb_dom_attr_qualified_name(attr, &attr_name_len);
        
        if (attr_name && attr_name_len > 0 && attr_name_len < 32) { // Skip long attribute names
            // Quick check for "on" prefix without malloc
            if (attr_name_len >= 2 && attr_name[0] == 'o' && attr_name[1] == 'n') {
                
                // Convert to C string only for potential event attributes
                char *attr_name_str = malloc(attr_name_len + 1);
                if (attr_name_str) {
                    memcpy(attr_name_str, attr_name, attr_name_len);
                    attr_name_str[attr_name_len] = '\0';
                    
                    // Check if it's an event attribute
                    if (is_event_attribute(attr_name_str)) {
                        // Get attribute value
                        size_t value_len;
                        const lxb_char_t *attr_value = lxb_dom_attr_value(attr, &value_len);
                        
                        if (attr_value && value_len > 0 && value_len < 4096) { // Limit handler size
                            // Store handler
                            if (handler_count < MAX_EVENT_HANDLERS) {
                                event_handlers[handler_count].element = elem;
                                event_handlers[handler_count].event_type = get_event_type(attr_name_str);
                                
                                // Copy handler code with limit
                                size_t copy_len = value_len < 1024 ? value_len : 1024;
                                event_handlers[handler_count].handler_code = malloc(copy_len + 1);
                                if (event_handlers[handler_count].handler_code) {
                                    memcpy(event_handlers[handler_count].handler_code,
                                           attr_value, copy_len);
                                    event_handlers[handler_count].handler_code[copy_len] = '\0';
                                    
                                    total_handlers_found++;
                                    if (total_handlers_found <= 20) { // Only show first 20
                                        printf("Found event: %s -> %.50s\n",
                                               event_handlers[handler_count].event_type,
                                               event_handlers[handler_count].handler_code);
                                    }
                                    handler_count++;
                                }
                            }
                        }
                    }
                    free(attr_name_str);
                }
            }
        }
        attr = lxb_dom_element_next_attribute(attr);
    }
    
    elements_processed++;
}

void event_handler_calculate_complexity(void) {
    // Calculate based on handlers found vs limit
    if (handler_count >= 100) {
        g_site_complexity_score = 100;
        printf("📊 Site complexity: EXTREME (%d handlers)\n", handler_count);
    } else if (handler_count >= 50) {
        g_site_complexity_score = 80;
        printf("📊 Site complexity: HIGH (%d handlers)\n", handler_count);
    } else if (handler_count >= 20) {
        g_site_complexity_score = 60;
        printf("📊 Site complexity: MEDIUM-HIGH (%d handlers)\n", handler_count);
    } else if (handler_count >= 10) {
        g_site_complexity_score = 40;
        printf("📊 Site complexity: MEDIUM (%d handlers)\n", handler_count);
    } else if (handler_count >= 5) {
        g_site_complexity_score = 20;
        printf("📊 Site complexity: LOW (%d handlers)\n", handler_count);
    } else {
        g_site_complexity_score = 0;
        printf("📊 Site complexity: SIMPLE (%d handlers)\n", handler_count);
    }
    
    // If we hit the element limit, override to HIGH
    if (elements_processed >= MAX_ELEMENTS_TO_PROCESS) {
        g_site_complexity_score = 100;
        printf("📊 Site complexity: VERY HIGH (hit element limit)\n");
    }
    
    printf("   Elements processed: %d\n", elements_processed);
    printf("   Event handlers found: %d\n", handler_count);
}

cJSON* get_event_handlers_json(void) {
    cJSON *root = cJSON_CreateArray();
    if (!root) return NULL;
    
    for (int i = 0; i < handler_count; i++) {
        cJSON *handler = cJSON_CreateObject();
        if (!handler) continue;
        
        // Add event type
        if (event_handlers[i].event_type) {
            cJSON_AddStringToObject(handler, "type", event_handlers[i].event_type);
        }
        
        // Add handler code
        if (event_handlers[i].handler_code) {
            cJSON_AddStringToObject(handler, "code", event_handlers[i].handler_code);
        }
        
        // Add element info with enhanced details
        if (event_handlers[i].element) {
            size_t len;
            
            // Get element tag
            const lxb_char_t *tag = lxb_dom_element_qualified_name(
                event_handlers[i].element, &len);
            
            if (tag && len > 0) {
                char *tag_str = malloc(len + 1);
                if (tag_str) {
                    memcpy(tag_str, tag, len);
                    tag_str[len] = '\0';
                    cJSON_AddStringToObject(handler, "element", tag_str);
                    cJSON_AddStringToObject(handler, "element_tag", tag_str);
                    free(tag_str);
                }
            }
            
            // Get element ID
            const lxb_char_t *id = lxb_dom_element_id(event_handlers[i].element, &len);
            if (id && len > 0) {
                char *id_str = malloc(len + 1);
                if (id_str) {
                    memcpy(id_str, id, len);
                    id_str[len] = '\0';
                    cJSON_AddStringToObject(handler, "id", id_str);
                    cJSON_AddStringToObject(handler, "element_id", id_str);
                    free(id_str);
                }
            }
            
            // Get element class
            const lxb_char_t *cls = lxb_dom_element_class(event_handlers[i].element, &len);
            if (cls && len > 0) {
                char *cls_str = malloc(len + 1);
                if (cls_str) {
                    memcpy(cls_str, cls, len);
                    cls_str[len] = '\0';
                    cJSON_AddStringToObject(handler, "element_class", cls_str);
                    free(cls_str);
                }
            }
        }
        
        cJSON_AddItemToArray(root, handler);
    }
    
    return root;
}


// Get events for specific element with enhanced info
cJSON* get_element_events_json(lxb_dom_element_t *elem) {
    if (!elem) return NULL;
    
    cJSON *events_array = cJSON_CreateArray();
    if (!events_array) return NULL;
    
    // Get element info once
    char *element_tag = NULL;
    char *element_id = NULL;
    char *element_class = NULL;
    
    // Get element tag
    size_t len;
    const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &len);
    if (tag_name && len > 0) {
        element_tag = malloc(len + 1);
        if (element_tag) {
            memcpy(element_tag, tag_name, len);
            element_tag[len] = '\0';
        }
    }
    
    // Get element id
    const lxb_char_t *id = lxb_dom_element_id(elem, &len);
    if (id && len > 0) {
        element_id = malloc(len + 1);
        if (element_id) {
            memcpy(element_id, id, len);
            element_id[len] = '\0';
        }
    }
    
    // Get element class
    const lxb_char_t *cls = lxb_dom_element_class(elem, &len);
    if (cls && len > 0) {
        element_class = malloc(len + 1);
        if (element_class) {
            memcpy(element_class, cls, len);
            element_class[len] = '\0';
        }
    }
    
    // Search all registered handlers for this element
    for (int i = 0; i < handler_count; i++) {
        if (event_handlers[i].element == elem) {
            cJSON *event_obj = cJSON_CreateObject();
            if (event_obj) {
                // Basic event info
                if (event_handlers[i].event_type) {
                    cJSON_AddStringToObject(event_obj, "type", event_handlers[i].event_type);
                }
                if (event_handlers[i].handler_code) {
                    cJSON_AddStringToObject(event_obj, "handler", event_handlers[i].handler_code);
                    
                    // Optional: Add a preview for long handlers
                    if (strlen(event_handlers[i].handler_code) > 50) {
                        char preview[60];
                        snprintf(preview, sizeof(preview), "%.50s...", 
                                event_handlers[i].handler_code);
                        cJSON_AddStringToObject(event_obj, "preview", preview);
                    }
                }
                
                // Enhanced: Add element info
                if (element_tag) {
                    cJSON_AddStringToObject(event_obj, "element", element_tag);
                    cJSON_AddStringToObject(event_obj, "element_tag", element_tag);
                }
                
                if (element_id) {
                    cJSON_AddStringToObject(event_obj, "element_id", element_id);
                }
                
                if (element_class) {
                    cJSON_AddStringToObject(event_obj, "element_class", element_class);
                }
                
                cJSON_AddItemToArray(events_array, event_obj);
            }
        }
    }
    
    // Cleanup
    if (element_tag) free(element_tag);
    if (element_id) free(element_id);
    if (element_class) free(element_class);
    
    if (cJSON_GetArraySize(events_array) > 0) {
        return events_array;
    } else {
        cJSON_Delete(events_array);
        return NULL;
    }
}

// event_handler.c - Add this function
int is_event_attribute_name(const char *attr_name) {
    // Must start with "on" and be at least 3 chars
    if (!attr_name || strlen(attr_name) < 3 || 
        attr_name[0] != 'o' || attr_name[1] != 'n') {
        return 0;
    }
    
    // Check if it's a known event type
    return is_event_attribute(attr_name);  // Reuse your existing function
}


void execute_event_handler(const char *element_id, const char *event_type) {
    if (!element_id || !event_type) return;

    // 1. Provera starog HTML inline koda (onclick="...")
    for (int i = 0; i < handler_count; i++) {
        if (event_handlers[i].event_type && 
            strcmp(event_handlers[i].event_type, event_type) == 0) {
            
            size_t id_len;
            const lxb_char_t *id = lxb_dom_element_id(event_handlers[i].element, &id_len);
            if (id && id_len > 0 && strncmp((char*)id, element_id, id_len) == 0) {
                printf("⚙️ [Event Engine] Pokrećem tekstualni HTML inline kod: %s\n", event_handlers[i].handler_code);
                js_execute_code(g_js_context, event_handlers[i].handler_code);
                return; // HTML kod izvršen, izlazimo!
            }
        }
    }

    // 2. 🚀 JS FALLBACK: Ako nema HTML koda, pitamo QuickJS niz da okinuti asinhroni callback!
    extern int pozovi_asinhroni_js_callback(const char *el_id, const char *ev_type);
    if (pozovi_asinhroni_js_callback(element_id, event_type)) {
        return; // JS callback uspešno pronađen i izvršen!
    }
}



void event_handler_clear_all(void) {
    for (int i = 0; i < handler_count; i++) {
        if (event_handlers[i].event_type) {
            free(event_handlers[i].event_type);
        }
        if (event_handlers[i].handler_code) {
            free(event_handlers[i].handler_code);
        }
    }
    handler_count = 0;
    elements_processed = 0;
    total_handlers_found = 0;
    g_site_complexity_score = 0;
}


void event_handler_register_js(lxb_dom_element_t *elem, const char *event_type, JSValue callback) {
    if (!elem || !event_type) return;
    
    if (handler_count < 100) { // MAX_EVENT_HANDLERS je 100 u tvom kodu
        event_handlers[handler_count].element = elem;
        event_handlers[handler_count].event_type = strdup(event_type);
        event_handlers[handler_count].handler_code = NULL;
        
        // 🛡️ DUP VALUE SHIELD: Zaključavamo funkciju u QuickJS memoriji da je GC ne obriše!
        event_handlers[handler_count].js_callback = JS_DupValue(g_js_context, callback);
        event_handlers[handler_count].is_js_handler = 1;
        
        if (INFO_MESSAGES_JS) {
            printf("🔗 [Event Engine] Uspešno registrovan JS callback za događaj '%s'\n", event_type);
        }
        handler_count++;
    } else {
        printf("⚠ [Event Engine] Premašen limit od 100 event handlera!\n");
    }
}
