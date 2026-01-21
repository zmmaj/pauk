// event_handler.h - Simplified approach
#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include "cjson.h"
#include "js_executor_quickjs.h"

// Simple event handler structure
typedef struct {
    lxb_dom_element_t *element;
    char *event_type;       // "click", "change", etc.
    char *handler_code;     // JavaScript code from onclick="..."
} EventHandler;

// Initialize/cleanup
void event_handler_init(void);
void event_handler_cleanup(void);

// Process element for event attributes
void process_element_events(lxb_dom_element_t *elem);

// Get all event handlers as JSON
cJSON* get_event_handlers_json(void);

// Get event count - ADD THIS LINE
int get_event_handler_count(void);

// Process entire document for events
void process_document_events(lxb_html_document_t *doc);

// Get events for specific element as JSON
cJSON* get_element_events_json(lxb_dom_element_t *elem);

int get_event_handler_count(void) ;
int is_event_attribute_name(const char *attr_name);
void process_events_recursive(lxb_dom_node_t *node);


#endif