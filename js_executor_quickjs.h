#ifndef JS_EXECUTOR_QUICKJS_H
#define JS_EXECUTOR_QUICKJS_H

#include "quickjs.h"
#include "quickjs-libc.h"
#include "cjson.h"
#include "gui.h"
#include <lexbor/html/html.h>


typedef struct {
    size_t max_memory_bytes;
    int max_execution_time_ms;
    int allow_network_apis;
    int allow_file_apis;
    int allow_dom_apis;
    int enable_console;
} SecurityPolicy;

// Callback storage structure
typedef struct {
    char *element_id;
    JSValue callback;
} JSCallback;

// ===== MAPA JS OBJEKAT → cJSON =====
typedef struct {
    JSValue js_obj;
    int element_id;
} js_cjson_ref_t;

extern JSCallback *js_callbacks;
extern int js_callback_count;

extern JSContext *g_js_context;

extern int g_render_needs_layout; 


// QuickJS engine functions - SAME API as Duktape for easy replacement
JSContext* js_engine_init(void);
void js_set_document(JSContext *ctx, lxb_html_document_t *document);
void js_engine_cleanup(JSContext *ctx);
void js_execute_code(JSContext *ctx, const char *script);
void js_execute_script_elements(JSContext *ctx, lxb_html_document_t *document);
// DOM API registration
void js_register_dom(JSContext *ctx);
void js_register_dom_api(JSContext *ctx);

void js_execute_onclick_handler(JSContext *ctx, const char *onclick_code);
void js_execute_onclick(const char *element_id);

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

void js_trigger_event(JSContext *ctx, const char *element_id, const char *event_type);
void js_trigger_click(JSContext *ctx, const char *element_id);   

char* fetch_external_script(const char *url);
void js_set_base_url(const char *url);
char* resolve_script_url(const char *base_url, const char *relative_url);
char* get_element_text_content(const char *element_id);

void js_execute_click_callback(const char *element_id);
void set_style_via_native(const char *element_id, const char *property, const char *value);
void clear_all_callbacks(void);

//tajmeri i vreme
void timer_callback(void *arg);
JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
void js_register_timers(JSContext *ctx);

//nadji element po ID
cJSON* find_element_by_id_in_json(cJSON *root, const char *id);


void js_register_dom_element_class(JSContext *ctx) ;

JSValue js_element_set_innerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);


JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_response_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_response_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

void js_cleanup_all_timers(void);
void js_reset_all_state(void);
void inicijalizuj_moderni_js_stit(JSContext *ctx);

JSValue js_element_appendChild(JSContext *ctx, JSValue this_val,
    int argc, JSValue *argv);
JSValue js_document_createElement(JSContext *ctx, JSValue this_val,
        int argc, JSValue *argv);
void process_pending_append_requests(pauk_ui_t *pauk_ui, JSContext *ctx);
int is_valid_dom_tree(cJSON *root);
JSValue js_document_querySelector(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv);
JSValue js_document_querySelectorAll(JSContext *ctx, JSValueConst this_val, 
        int argc, JSValueConst *argv);
JSValue create_js_element_from_cjson(JSContext *ctx, cJSON* element);
cJSON* find_element_by_class_recursive(cJSON* element, const char* class_name);
cJSON* find_element_by_tag_recursive(cJSON* element, const char* tag_name);

JSValue js_element_toString(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv);
    JSValue js_url_search_params_toString(JSContext *ctx, JSValueConst this_val, 
        int argc, JSValueConst *argv);
JSValue js_url_search_params(JSContext *ctx, JSValueConst this_val, 
            int argc, JSValueConst *argv);
JSValue js_element_classList(JSContext *ctx, JSValueConst this_val, 
                int argc, JSValueConst *argv);
 cJSON* find_element_by_element_id(cJSON *root, const char *element_id_str);
 void process_pending_remove_requests(pauk_ui_t *pauk_ui, JSContext *ctx);

 int js_execute_callback_if_exists(const char *element_id, const char *event_type);
 int pozovi_asinhroni_js_callback(const char *el_id, const char *ev_type);


#endif
