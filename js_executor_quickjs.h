#ifndef JS_EXECUTOR_QUICKJS_H
#define JS_EXECUTOR_QUICKJS_H

#include "quickjs.h"
#include "quickjs-libc.h"
#include "cjson.h"
#include <lexbor/html/html.h>


typedef struct {
    size_t max_memory_bytes;
    int max_execution_time_ms;
    int allow_network_apis;
    int allow_file_apis;
    int allow_dom_apis;
    int enable_console;
} SecurityPolicy;


// QuickJS engine functions - SAME API as Duktape for easy replacement
JSContext* js_engine_init(void);
void js_set_document(JSContext *ctx, lxb_html_document_t *document);
void js_engine_cleanup(JSContext *ctx);
void js_execute_code(JSContext *ctx, const char *script);
void js_execute_script_elements(JSContext *ctx, lxb_html_document_t *document);
void js_execute_script_elements_recursive(JSContext *ctx, lxb_dom_node_t *node, int *script_count);
// DOM API registration
void js_register_dom(JSContext *ctx);
void js_register_dom_api(JSContext *ctx);

SecurityPolicy js_default_security_policy(void);
void js_set_security_policy(JSContext *ctx, SecurityPolicy policy);

//racunamo pozicije
cJSON* get_computed_layout_from_js(JSContext *ctx);

void init_js_modifications(void);
cJSON* get_js_modifications(void);
void cleanup_js_modifications(void);
void js_register_element_modification(JSContext *ctx, 
    const char *element_id,
    const char *property,
    JSValue callback);



    
#endif