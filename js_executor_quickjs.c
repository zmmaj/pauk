
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "js_executor_quickjs.h"
#include "layout_engine.h"
#include "cjson.h"
#include "main.h"
#include "pauk_tls.h"
#include "network.h"
#include "resource_filter.h"
#include "time_utils.h"
#include "render_func.h"
#include "rendering_elements/defaults.h"
#include "cookie.h"


JSContext *g_js_context = NULL; 
JSCallback *js_callbacks = NULL;
int g_render_needs_layout = 0;

typedef struct {
    char *element_id;
    char *property;
    JSValue js_callback;
    void (*native_callback)(lxb_dom_element_t*, const char*);
} JSRenderCallback;

typedef struct {
    char *element_id;
    char *event_type;
    JSValue callback;
} JSEventCallback;

typedef struct {
    JSContext *ctx;
    JSValue function;
    int64_t interval_ms;
    int is_interval;  // 1 for setInterval, 0 for setTimeout
    int timer_id;
    int is_active;           // 1 = active, 0 = cancelled
    fibril_timer_t *ftimer; 
} TimerData;

typedef struct {
    char *parent_id;
    char *child_id;
} pending_append_t;

typedef struct {
    char *parent_id;
    char *child_id;
} pending_remove_t;



static pending_remove_t *pending_remove_requests = NULL;
static int pending_remove_count = 0;
static JSValue js_element_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static pending_append_t *pending_append_requests = NULL;
static int pending_append_count = 0;

static js_cjson_ref_t *js_cjson_refs = NULL;
static int js_cjson_ref_count = 0;

static cJSON *js_style_modifications = NULL;
// Global runtime for better memory management
static JSRuntime *global_rt = NULL;
static lxb_html_document_t *global_document = NULL;
static JSRenderCallback *js_render_callbacks = NULL;
 int js_callback_count = 0;

static JSEventCallback *event_callbacks = NULL;
static int event_callback_count = 0;
cJSON *global_document_json = NULL;
extern char *g_current_base_url;

static int next_timer_id = 1;
static TimerData *active_timers[256];  // Max 256 active timers
static JSClassID js_dom_element_class_id = 0;
static cJSON **detached_elements = NULL;
static int detached_count = 0;

static JSValue js_element_getValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_setValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_classList_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_classList_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_classList_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_classList_toggle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_document_get_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_form_submit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_classList_toString(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static cJSON* find_any_element(cJSON *root, const char *target_id);
static JSValue js_element_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_focus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_add_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_getInnerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_setInnerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_document_getCookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_document_setCookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);


static const char* js_get_element_id_str(JSContext *ctx, JSValue obj) {
    JSValue id_val = JS_GetPropertyStr(ctx, obj, "_elementId");
    if (!JS_IsString(id_val)) {
        JS_FreeValue(ctx, id_val);
        return NULL;
    }
    const char *id = JS_ToCString(ctx, id_val);
    JS_FreeValue(ctx, id_val);
    return id;
}

// Vadi cJSON element po ID-u (string)
static cJSON* js_get_cjson_element(JSContext *ctx, JSValue obj) {
    const char *id = js_get_element_id_str(ctx, obj);
    if (!id) return NULL;
    
    if(INFO_MESSAGES_JS) {
        printf("🔍 js_get_cjson_element: looking for ID '%s'\n", id);
    }
    
    cJSON *found = NULL;
    int element_id = atoi(id);
    
    // 🚀 SKRETNICA: Ako atoi vrati 0, u pitanju je tekstualni ID (npr. 'result5')
    if (element_id == 0) {
        if (global_document_json) {
          //  extern cJSON* find_element_by_string_id(cJSON *root, const char *target_id);
            found = find_element_by_string_id(global_document_json, id);
            if (found && INFO_MESSAGES_JS) {
                printf("   Found text ID '%s' via find_element_by_string_id\n", id);
            }
        }
    } else {
        // ===== 1. AKO JE ID ČIST BROJ, PRVO PROVERI NUMERIČKU MAPU =====
        for (int i = 0; i < js_cjson_ref_count; i++) {
            if (js_cjson_refs[i].element_id == element_id) {
                if (global_document_json && cJSON_IsArray(global_document_json)) {
                    found = find_element_by_element_id(global_document_json, id);
                    if (found) {
                        if(INFO_MESSAGES_JS) printf("   Found in global_document_json via map\n");
                        break;
                    }
                }
                if (!found && detached_elements) {
                    for (int j = 0; j < detached_count; j++) {
                        if (detached_elements[j]) {
                            int elem_id = get_json_number(detached_elements[j], "element_id", -1);
                            if (elem_id == element_id) {
                                found = detached_elements[j];
                                if(INFO_MESSAGES_JS) printf("   Found in detached list via map\n");
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }
    }
    
    // ===== 2. FALLBACK ZA SVE OSTALE SLUČAJEVE =====
    if (!found) {
        if (global_document_json && cJSON_IsArray(global_document_json)) {
            // Prvo probaj string pretragu jer pokriva i tekstualne i numeričke stringove
          //  extern cJSON* find_element_by_string_id(cJSON *root, const char *target_id);
            found = find_element_by_string_id(global_document_json, id);
            
            // Ako string pretraga omane, probaj stari numerički element_id fallback
            if (!found) {
                found = find_element_by_element_id(global_document_json, id);
            }
            
            if (found && INFO_MESSAGES_JS) printf("   Found in global_document_json (fallback)\n");
        }
        
        if (!found && detached_elements) {
            for (int i = 0; i < detached_count; i++) {
                if (detached_elements[i]) {
                    int elem_id = get_json_number(detached_elements[i], "element_id", -1);
                    if (elem_id == element_id) {
                        found = detached_elements[i];
                        if(INFO_MESSAGES_JS) printf("   Found in detached list (fallback)\n");
                        break;
                    }
                }
            }
        }
    }
    
    if (found && INFO_MESSAGES_JS) {
        printf("✅ js_get_cjson_element: found element with ID '%s'\n", id);
    } else if (!found && INFO_MESSAGES_JS) {
        printf("❌ js_get_cjson_element: element with ID '%s' NOT found\n", id);
    }
    
    JS_FreeCString(ctx, id);
    return found;
}


static JSValue js_element_setAttribute(JSContext *ctx, JSValue this_val,
    int argc, JSValue *argv) {
if (argc < 2) return JS_UNDEFINED;

cJSON *json = js_get_cjson_element(ctx, this_val);
if (!json) return JS_UNDEFINED;

const char *attr = JS_ToCString(ctx, argv[0]);
const char *value = JS_ToCString(ctx, argv[1]);

if (attr && value) {
set_json_string(json, attr, value);
set_json_bool(json, "needs_layout", true);
g_render_needs_layout = 1;
}

JS_FreeCString(ctx, attr);
JS_FreeCString(ctx, value);
return JS_UNDEFINED;
}

static JSValue js_element_getAttribute(JSContext *ctx, JSValue this_val,
    int argc, JSValue *argv) {
if (argc < 1) return JS_UNDEFINED;

cJSON *json = js_get_cjson_element(ctx, this_val);
if (!json) return JS_UNDEFINED;

const char *attr = JS_ToCString(ctx, argv[0]);
if (!attr) return JS_UNDEFINED;

const char *value = get_json_string(json, attr,"");
JS_FreeCString(ctx, attr);

return JS_NewString(ctx, value ? value : "");
}



static JSValue js_image_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    
    // Inicijalizujemo polja koja skripte menjaju kako ne bi došlo do novog kraha
    JS_SetPropertyStr(ctx, obj, "src", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, 0));
    
    return obj; 
}


// 🛡️ PAUK GEOMETRY RUNTIME BRIDGE: Dynamically connects real C coordinates with QuickJS script checks!
static JSValue js_element_getBoundingClientRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // 1. Extract the unique element identifier string from the active JS object
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    // Default safe layout structures in case the layout is uncalculated
    int x = 0, y = 0, width = 200, height = 40;
    
    if (element_id && global_pauk_ui && global_pauk_ui->rendering_json) {
        // 2. Scan your active C rendering tree to find the element
        cJSON *target = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
        if (!target) {
            int num_id = atoi(element_id);
            if (num_id > 0) target = find_element_by_id(global_pauk_ui->rendering_json, num_id);
        }
        
        // 3. Extract the real pixel coordinates computed by your native 'racunaj_pozicije' engine!
        if (target) {
            extern int get_json_number(cJSON *, const char *, int);
            x = get_json_number(target, "x", 0);
            y = get_json_number(target, "y", 0);
            width = get_json_number(target, "width", 0);
            height = get_json_number(target, "height", 0);
        }
    }
    
    if (element_id) JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    
    // 4. Build and return a fully spec-compliant W3C DOMRect bounding structure object
    JSValue rect = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, rect, "left", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, rect, "top", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, rect, "right", JS_NewInt32(ctx, x + width));
    JS_SetPropertyStr(ctx, rect, "bottom", JS_NewInt32(ctx, y + height));
    JS_SetPropertyStr(ctx, rect, "width", JS_NewInt32(ctx, width));
    JS_SetPropertyStr(ctx, rect, "height", JS_NewInt32(ctx, height));
    JS_SetPropertyStr(ctx, rect, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, rect, "y", JS_NewInt32(ctx, y));
    
    return rect;
}

// Regularna, nezavisna static funkcija za bezbedan obilazak stabla
static void pauk_safe_exec_scripts(JSContext *ctx, lxb_dom_node_t *node, int *count) {
    if (!node) return;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len = 0;
        const lxb_char_t *tag_name = lxb_dom_element_qualified_name(elem, &tag_len);
        
        if (tag_name && tag_len == 6 && strncasecmp((char*)tag_name, "script", 6) == 0) {
            size_t src_len = 0;
            const lxb_char_t *src_value = lxb_dom_element_get_attribute(elem, 
                (lxb_char_t*)"src", 3, &src_len);
            
                if (src_value && src_len > 0) {
                    char *src_str = strndup((char*)src_value, src_len);
                    if (src_str) {
                        // Preskoči velike biblioteke
                        if (strstr(src_str, "jquery") || strstr(src_str, "bootstrap") ||
                            strstr(src_str, "jquery.min") || strstr(src_str, "bootstrap.min")) {
                            if(INFO_MESSAGES_JS) printf("⏭️ [JS] Skipping large library: %s\n", src_str);
                            free(src_str);
                            (*count)++;
                            return;
                        }
                        
                        // Pokušaj da pročitaš lokalni fajl
                        FILE *f = fopen(src_str, "rb");
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long sz = ftell(f);
                            fseek(f, 0, SEEK_SET);
                            
                            if (sz > 0 && sz < 100000) {
                                char *js_code = malloc(sz + 1);
                                if (js_code) {
                                    fread(js_code, 1, sz, f);
                                    js_code[sz] = '\0';
                                    if(INFO_MESSAGES_JS) printf("📜 Executing external script: %s (%ld bytes)\n", src_str, sz);
                                    js_execute_code(ctx, js_code);
                                    free(js_code);
                                }
                            }
                            fclose(f);
                        } else {
                            if(INFO_MESSAGES_JS) printf("⚠️ [JS] Cannot open: %s\n", src_str);
                        }
                        
                        free(src_str);
                    }
                    (*count)++;
                    return;
                }
            
            lxb_dom_node_t *script_node = lxb_dom_interface_node(elem);
            size_t script_len = 0;
            lxb_char_t *script_text = lxb_dom_node_text_content(script_node, &script_len);
            
            if (script_text && script_len > 0 && script_len < 500000) {
                // FIXED: Allocate an extra 16 bytes of structural padding headroom 
                // to completely eliminate neighboring byte overflow traps on the heap
                char *script_content = malloc(script_len + 16);
                if (script_content) {
                    memcpy(script_content, script_text, script_len);
                    script_content[script_len] = '\0';
                    
                    if(INFO_MESSAGES_JS)  printf("📜 Executing inline script (%zu bytes)\n", script_len);
                    js_execute_code(ctx, script_content);
                    
                    // Safe free of our own manually allocated heap block
                    free(script_content);
                }
            } else if (script_text && script_len >= 500000) {
                if(INFO_MESSAGES_JS)  printf("⏭️ [JS] Skipping mega inline script (%zu bytes) to maintain layout\n", script_len);
            }
            
            // FIXED: Do NOT invoke lexbor_free() or lxb_dom_document_destroy_text here.
            // On HelenOS cross-compiled pools, manual truncation triggers immediate 
            // 0xBEEF0101 corruption. Lexbor reclaims this automatically when current_doc 
            // is destroyed via lxb_html_document_clean/destroy!
            
            (*count)++;
            return;
        }
    }
    
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    while (child) {
        pauk_safe_exec_scripts(ctx, child, count);
        child = lxb_dom_node_next(child);
    }
}




// ========== FETCH IMPLEMENTATION ==========

 JSValue js_response_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue data_val = JS_GetPropertyStr(ctx, this_val, "_data");
    const char *data_str = JS_ToCString(ctx, data_val);
    if (!data_str) {
        JS_FreeValue(ctx, data_val);
        return JS_NULL;
    }
    
    JSValue result = JS_ParseJSON(ctx, data_str, strlen(data_str), "<json>");
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, result);
        result = JS_NewString(ctx, data_str);
    }
    
    JS_FreeCString(ctx, data_str);
    JS_FreeValue(ctx, data_val);
    return result;
}

 JSValue js_response_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue data_val = JS_GetPropertyStr(ctx, this_val, "_data");
    JSValue result = JS_DupValue(ctx, data_val);
    JS_FreeValue(ctx, data_val);
    return result;
}

JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fetch: url required");
    }
    
    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url) {
        return JS_ThrowTypeError(ctx, "fetch: invalid url");
    }
    
    if(INFO_MESSAGES_JS)  printf("🌐 Fetch: %s\n", url);
    
    // Perform the HTTP request using your existing network functions
    char *response_data = NULL;
    size_t response_size = 0;
    errno_t rc = EIO;
    
    if (strncmp(url, "https://", 8) == 0) {
        rc = fetch_https_content_keep_alive(url, &response_data, &response_size);
    } else if (strncmp(url, "http://", 7) == 0) {
        rc = fetch_http_content_keep_alive(url, &response_data, &response_size);
    } else {
        // Try relative URL with base URL
        const char *base = get_current_base_url();
        if (base) {
            char *absolute = resolve_relative_url(base, url);
            if (absolute) {
                if (strncmp(absolute, "https://", 8) == 0) {
                    rc = fetch_https_content_keep_alive(absolute, &response_data, &response_size);
                } else {
                    rc = fetch_http_content_keep_alive(absolute, &response_data, &response_size);
                }
                free(absolute);
            }
        }
        if (!response_data) {
            JS_FreeCString(ctx, url);
            return JS_ThrowTypeError(ctx, "fetch: unsupported URL format");
        }
    }
    
    // Create response object
    JSValue response = JS_NewObject(ctx);
    
    if (rc == EOK && response_data && response_size > 0) {
        JS_SetPropertyStr(ctx, response, "ok", JS_NewBool(ctx, 1));
        JS_SetPropertyStr(ctx, response, "status", JS_NewInt32(ctx, 200));
        JS_SetPropertyStr(ctx, response, "statusText", JS_NewString(ctx, "OK"));
        
        // Store data for json()/text() methods
        char *data_copy = malloc(response_size + 1);
        if (data_copy) {
            memcpy(data_copy, response_data, response_size);
            data_copy[response_size] = '\0';
            JS_SetPropertyStr(ctx, response, "_data", JS_NewString(ctx, data_copy));
            free(data_copy);
        } else {
            JS_SetPropertyStr(ctx, response, "_data", JS_NewString(ctx, ""));
        }
        
        free(response_data);
        if(INFO_MESSAGES_JS)  printf("✅ Fetch successful: %zu bytes\n", response_size);
    } else {
        JS_SetPropertyStr(ctx, response, "ok", JS_NewBool(ctx, 0));
        JS_SetPropertyStr(ctx, response, "status", JS_NewInt32(ctx, 500));
        JS_SetPropertyStr(ctx, response, "statusText", JS_NewString(ctx, "Network Error"));
        JS_SetPropertyStr(ctx, response, "_data", JS_NewString(ctx, "Network error"));
        if(INFO_MESSAGES_JS)  printf("❌ Fetch failed\n");
    }
    
    // Add json() and text() methods
    JS_SetPropertyStr(ctx, response, "json",
        JS_NewCFunction(ctx, js_response_json, "json", 0));
    JS_SetPropertyStr(ctx, response, "text",
        JS_NewCFunction(ctx, js_response_text, "text", 0));
    
    JS_FreeCString(ctx, url);
    return response;
}

void js_execute_click_callback(const char *element_id) {
    if (!element_id || !g_js_context) return;
    
    for (int i = 0; i < js_callback_count; i++) {
        if (strcmp(js_callbacks[i].element_id, element_id) == 0) {
            if(INFO_MESSAGES_JS)  printf("🎯 Executing callback for %s\n", element_id);
            JS_Call(g_js_context, js_callbacks[i].callback, JS_UNDEFINED, 0, NULL);
            return;
        }
    }
    if(INFO_MESSAGES_JS) printf("⚠️ No callback found for %s\n", element_id);
}

// Generic warning for unsupported features
// =========================================================================
// 🛡️ DUPLI DIJAGNOSTIČKI SKENER NEPODRŽANIH FUNKCIJA (POPRAVLJENO)
// Štampa ime funkcije kroz magični ID i izvlači prve prosleđene argumente!
// =========================================================================
static JSValue js_unsupported(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if(INFO_MESSAGES_JS)  printf("⚠️ [JS Warning] Pozvan nepodržani Web API!\n");
    
    // 🔍 1. Ako je skripta prosledila tekstualne argumente (npr. kod matchMedia)
    if (argc > 0) {
        for (int i = 0; i < argc; i++) {
            const char *arg_str = JS_ToCString(ctx, argv[i]);
            if (arg_str) {
                if(INFO_MESSAGES_JS)  printf("   👉 Argument [%d]: '%s'\n", i, arg_str);
                JS_FreeCString(ctx, arg_str); // Obavezno čišćenje memorije u QuickJS
            }
        }
    }
    
    return JS_UNDEFINED;
}


// Store callback when JS calls addEventListener
//extern lxb_dom_element_t *lxb_dom_document_element_by_id(lxb_dom_document_t *document, const lxb_char_t *id, size_t len);

static JSValue js_add_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    
    const char *event_type = JS_ToCString(ctx, argv[0]);
    JSValue callback = argv[1];
    
    if (event_type && JS_IsFunction(ctx, callback)) {
        // Uzimamo ID elementa direktno iz QuickJS 'this' objekta
        JSValue element_id_val = JS_GetPropertyStr(ctx, this_val, "_elementId"); // PROMENJENO SA "id"
        const char *element_id = JS_ToCString(ctx, element_id_val);
        
        if (element_id && strlen(element_id) > 0) {
            // Smeštamo u tvoj originalni asinhroni niz
            event_callbacks = realloc(event_callbacks, (event_callback_count + 1) * sizeof(JSEventCallback));
            if (event_callbacks) {
                event_callbacks[event_callback_count].element_id = strdup(element_id);
                event_callbacks[event_callback_count].event_type = strdup(event_type);
                event_callbacks[event_callback_count].callback = JS_DupValue(ctx, callback);
                event_callback_count++;
                
                if(INFO_MESSAGES_JS) {
                    printf("🔗 [Event Bridge] Registrovan JS listener za #%s -> event: %s\n", element_id, event_type);
                }
            }
            JS_FreeCString(ctx, element_id);
        }
        JS_FreeValue(ctx, element_id_val);
        JS_FreeCString(ctx, event_type);
    }
    
    return JS_UNDEFINED;
}


// Trigger JavaScript event for a specific element
void js_trigger_event(JSContext *ctx, const char *element_id, const char *event_type) {
    if (!ctx || !element_id || !event_type) return;
    
    if(INFO_MESSAGES_JS) printf("🎯 Triggering JS event: %s on element #%s\n", event_type, element_id);
    
    // Execute JavaScript code that finds the element and triggers the event
    char js_code[512];
    snprintf(js_code, sizeof(js_code), 
        "try {"
        "   var elem = document.getElementById('%s');"
        "   if (elem && elem.on%s) {"
        "       elem.on%s();"
        "   }"
        "   // Also dispatch event for addEventListener"
        "   if (elem) {"
        "       var event = new Event('%s');"
        "       elem.dispatchEvent(event);"
        "   }"
        "} catch(e) { console.error('Event error:', e); }",
        element_id, event_type, event_type, event_type);
    
    js_execute_code(ctx, js_code);
}

// Trigger click event specifically
void js_trigger_click(JSContext *ctx, const char *element_id) {
    js_trigger_event(ctx, element_id, "click");
}

void init_js_modifications(void) {
    if (!js_style_modifications) {
        js_style_modifications = cJSON_CreateObject();
    }
}


static JSValue js_request_render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS -> Renderer: requestRender() - execution thread triggered\n");
    
    if (global_pauk_ui) {
        // 🚀 PRVO POKREĆEMO BRISANJE ELEMENTA: Izvršava se nezavisno od dodavanja!
        void process_pending_remove_requests(pauk_ui_t *pauk_ui, JSContext *ctx);
        process_pending_remove_requests(global_pauk_ui, ctx);
        
        // Zatim pokrećemo append zahteve (ako ih ima na čekanju)
        process_pending_append_requests(global_pauk_ui, ctx);
    }
    
    dodaj_css(global_pauk_ui->rendering_json);

    if (global_pauk_ui && global_pauk_ui->rendering_json) {
        extern cJSON* find_element_by_string_id(cJSON *root, const char *target_id);
        cJSON *container = find_element_by_string_id(global_pauk_ui->rendering_json, "test11_container");
        
        if (container) {
            // Ponovo premeravamo samo naš Test 11 kontejner na osnovu njegovih tačnih pozicija
            int start_x = get_json_number(container, "x", 0);
            int start_y = get_json_number(container, "y", 1901);
            int start_w = get_json_number(container, "width", 800);
            
            LayoutContext layout_ctx;
            memset(&layout_ctx, 0, sizeof(LayoutContext));
            layout_ctx.current_x = start_x;
            layout_ctx.current_y = start_y;
            layout_ctx.container_width = start_w;
            layout_ctx.parent_x = start_x;
            
            void racunaj_pozicije(cJSON *element, int my_x, int my_y, int container_width, LayoutContext *ctx);
            racunaj_pozicije(container, start_x, start_y, start_w, &layout_ctx);
        }
        
        // Čistimo ekran u belo i ponovo iscrtavamo sveže stanje elemenata iz memorije
        test_text_rendering(global_pauk_ui);
        
        // Izbacujemo svež pixelmap direktno na ekran pretraživača
        refresh_page_after_js(global_pauk_ui);
    }
    
    return JS_UNDEFINED;
}


static JSValue js_get_computed_layout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if(INFO_MESSAGES_JS) printf("JS -> Renderer: getComputedLayout()\n");
    
    // Get layout from JavaScript global variable (your existing function)
    cJSON *layout_json = get_computed_layout_from_js(ctx);
    
    if (layout_json) {
        char *json_str = cJSON_Print(layout_json);
        JSValue result = JS_NewString(ctx, json_str);
        free(json_str);
        cJSON_Delete(layout_json);
        return result;
    }
    
    return JS_NULL;
}


void js_register_element_modification(JSContext *ctx, 
    const char *element_id,
    const char *property,
    JSValue callback) {
// When JS wants to modify an element, register it
JSRenderCallback cb = {
.element_id = strdup(element_id),
.property = strdup(property),
.js_callback = JS_DupValue(ctx, callback)
};

// Add to array
js_render_callbacks = realloc(js_render_callbacks, 
(js_callback_count + 1) * sizeof(JSRenderCallback));
js_render_callbacks[js_callback_count++] = cb;
}


static lxb_dom_element_t* find_element_by_id_recursive(lxb_dom_node_t *node, const char *id, int depth) {
    if (!node || !id || depth > 100) {  // Add depth limit
        return NULL;
    }
    
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *element = lxb_dom_interface_element(node);
            
            // Check if this element has the right ID
            lxb_dom_attr_t *id_attr = lxb_dom_element_attr_by_name(element, 
                (lxb_char_t*)"id", 2);
            
            if (id_attr) {
                size_t id_len;
                const lxb_char_t *id_val = lxb_dom_attr_value(id_attr, &id_len);
                
                if (id_val && id_len == strlen(id) && 
                    memcmp(id_val, id, id_len) == 0) {
                    return element; // Found it!
                }
            }
            
            // Search children recursively with depth+1
            lxb_dom_node_t *child = lxb_dom_node_first_child(node);
            if (child) {
                lxb_dom_element_t *found = find_element_by_id_recursive(child, id, depth + 1);
                if (found) return found;
            }
        }
        
        // Move to next sibling
        node = lxb_dom_node_next(node);
    }
    
    return NULL; // Not found
}


// 3. Native functions for JS to call
static JSValue js_native_set_style(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 3) return JS_UNDEFINED;
    if(INFO_MESSAGES_JS) printf("🔵 js_native_set_style CALLED!\n");  
    
    const char *element_id = NULL;
    JSValue id_to_free = JS_UNDEFINED;
    
    // 🚀 OBJECT-ID ŠTIT: Ako je prvi argument JS Objekat, čitamo njegov tajni _elementId
    if (JS_IsObject(argv[0])) {
        id_to_free = JS_GetPropertyStr(ctx, argv[0], "_elementId");
        if (!JS_IsUndefined(id_to_free) && !JS_IsNull(id_to_free) && !JS_IsException(id_to_free)) {
            element_id = JS_ToCString(ctx, id_to_free);
        } else {
            JS_FreeValue(ctx, id_to_free);
            id_to_free = JS_UNDEFINED;
        }
    }
    
    // Fallback: Ako nije objekat ili nismo našli _elementId, čitamo ga standardno kao string
    if (!element_id) {
        element_id = JS_ToCString(ctx, argv[0]);
    }
    
    const char *property = JS_ToCString(ctx, argv[1]);
    const char *value = JS_ToCString(ctx, argv[2]);
    
    if (!element_id || !property || !value) {
        if (element_id) JS_FreeCString(ctx, element_id);
        if (JS_IsString(id_to_free)) JS_FreeCString(ctx, element_id); // Sigurnosno oslobađanje ako je iz id_to_free
        JS_FreeValue(ctx, id_to_free);
        if (property) JS_FreeCString(ctx, property);
        if (value) JS_FreeCString(ctx, value);
        return JS_UNDEFINED;
    }
    
    if(INFO_MESSAGES_JS) printf("JS -> Renderer: setStyle('%s', '%s', '%s')\n", element_id, property, value);
    
    // ===== UPDATE THE ACTUAL RENDERING JSON =====
    if (global_pauk_ui && global_pauk_ui->rendering_json) {
        cJSON *target = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
        
        // 🚀 DETACHED OSIGURAČ: Ako element još uvek nije u glavnom stablu, potraži ga u detached listi!
        if (!target && detached_elements) {
            int num_id = atoi(element_id);
            for (int i = 0; i < detached_count; i++) {
                if (detached_elements[i]) {
                    int elem_id = get_json_number(detached_elements[i], "element_id", -1);
                    if (elem_id == num_id) {
                        target = detached_elements[i]; // Pronađen u detached listi!
                        break;
                    }
                }
            }
        }
        
        if (target) {
            // Tvoj postojeći kod za upisivanje svojstava u target nastavlja ovde...
            set_json_string(target, property, value);
            
            if (strcmp(property, "color") == 0) {
                // ... tvoj kod za color ...
            } else if (strcmp(property, "background-color") == 0) {
                set_json_string(target, "bg_color", value);
                if(INFO_MESSAGES_JS) printf("   ✅ Updated bg_color to %s\n", value);
            }
            
            set_json_bool(target, "needs_layout", true);
            g_render_needs_layout = 1;
        } else {
            if(INFO_MESSAGES_JS)  printf("   ❌ Element not found: %s\n", element_id);
        }
    }
    // ==========================================
    
    // Inicijalizacija modifikacija (Tvoj postojeći kod dalje nastavlja netaknut...)
    init_js_modifications();
    
    cJSON *element_mods = cJSON_GetObjectItemCaseSensitive(js_style_modifications, element_id);
    if (!element_mods) {
        element_mods = cJSON_CreateObject();
        cJSON_AddItemToObject(js_style_modifications, element_id, element_mods);
    }
    
    cJSON *styles = cJSON_GetObjectItem(element_mods, "style");
    if (!styles) {
        styles = cJSON_CreateObject();
        cJSON_AddItemToObject(element_mods, "style", styles);
    }
    
    cJSON_ReplaceItemInObject(styles, property, cJSON_CreateString(value));
    
    if (global_document && strlen(element_id) > 0) {
        lxb_dom_node_t *root = lxb_dom_interface_node(global_document);
        lxb_dom_element_t *elem = find_element_by_id_recursive(root, element_id, 0);
        
        if (elem) {
            lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"style", 5);
            if (style_attr) {
                size_t style_len;
                const lxb_char_t *existing_style = lxb_dom_attr_value(style_attr, &style_len);
                char new_style[1024];
                
                if (existing_style && style_len > 0) {
                    char *style_str = malloc(style_len + 1);
                    memcpy(style_str, existing_style, style_len);
                    style_str[style_len] = '\0';
                    snprintf(new_style, sizeof(new_style), "%s; %s: %s", style_str, property, value);
                    free(style_str);
                } else {
                    snprintf(new_style, sizeof(new_style), "%s: %s", property, value);
                }
                lxb_dom_attr_set_value(style_attr, (lxb_char_t*)new_style, strlen(new_style));
            }
        }
    }
    
    // Bezbedno oslobađanje u zavisnosti od toga kako je string alociran
    if (!JS_IsUndefined(id_to_free)) {
        JS_FreeCString(ctx, element_id);
        JS_FreeValue(ctx, id_to_free);
    } else {
        JS_FreeCString(ctx, element_id);
    }
    
    JS_FreeCString(ctx, property);
    JS_FreeCString(ctx, value);
    
    return JS_UNDEFINED;
}

static JSValue js_classList_toString(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Čitamo tajni ID sa classList objekta koji smo mu prosledili
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(id_val) || JS_IsNull(id_val) || JS_IsException(id_val)) {
        JS_FreeValue(ctx, id_val);
        return JS_NewString(ctx, "");
    }
    
    const char *id_str = JS_ToCString(ctx, id_val);
    const char *klase = "";
    
    if (id_str && global_document_json) {
     //   extern cJSON* find_any_element(cJSON *root, const char *target_id);
        cJSON *elem = find_any_element(global_document_json, id_str);
        if (elem) {
            // Izvlačimo sirovi string klasa iz cJSON memorije
            klase = get_json_string(elem, "class_string", "");
        }
    }
    
    if (id_str) JS_FreeCString(ctx, id_str);
    JS_FreeValue(ctx, id_val);
    
    return JS_NewString(ctx, klase);
}


static JSValue js_update_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    
    const char *element_id = JS_ToCString(ctx, argv[0]);
    const char *properties_json = JS_ToCString(ctx, argv[1]);
    
    if (!element_id || !properties_json) {
        if (element_id) JS_FreeCString(ctx, element_id);
        if (properties_json) JS_FreeCString(ctx, properties_json);
        return JS_UNDEFINED;
    }
    
    if(INFO_MESSAGES_JS) printf("JS -> Renderer: updateElement('%s', '%s')\n", element_id, properties_json);
    
    cJSON *properties = cJSON_Parse(properties_json);
    if (properties) {
        if (global_pauk_ui && global_pauk_ui->rendering_json) {
            cJSON *body = cJSON_GetArrayItem(global_pauk_ui->rendering_json, 0);
            cJSON *target = find_element_by_string_id(body, element_id);
            if (target) {
                // Update properties on the element
                cJSON *item = properties->child;
                while (item) {
                    if (strcmp(item->string, "text") == 0 && cJSON_IsString(item)) {
                        set_json_string(target, "text", item->valuestring);
                        set_json_string(target, "content", item->valuestring);
                        
                        // ALSO update the text node child
                        cJSON *children = cJSON_GetObjectItem(target, "children");
                        if (children && cJSON_IsArray(children)) {
                            int size = cJSON_GetArraySize(children);
                            for (int i = 0; i < size; i++) {
                                cJSON *child = cJSON_GetArrayItem(children, i);
                                const char *child_tag = get_json_string(child, "tag", "");
                                if (strcmp(child_tag, "text") == 0) {
                                    set_json_string(child, "text", item->valuestring);
                                    set_json_string(child, "content", item->valuestring);
                                    if(INFO_MESSAGES_JS)   printf("   ✅ Updated text node to: %s\n", item->valuestring);
                                }
                            }
                        }
                    } else if (strcmp(item->string, "color") == 0 && cJSON_IsString(item)) {
                        set_json_string(target, "color", item->valuestring);
                        
                        // ALSO update text node color
                        cJSON *children = cJSON_GetObjectItem(target, "children");
                        if (children && cJSON_IsArray(children)) {
                            int size = cJSON_GetArraySize(children);
                            for (int i = 0; i < size; i++) {
                                cJSON *child = cJSON_GetArrayItem(children, i);
                                const char *child_tag = get_json_string(child, "tag", "");
                                if (strcmp(child_tag, "text") == 0) {
                                    set_json_string(child, "color", item->valuestring);
                                }
                            }
                        }
                    }
                    item = item->next;
                }
             //   refresh_page_after_js(global_pauk_ui);
            }
        }
        cJSON_Delete(properties);
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeCString(ctx, properties_json);
    return JS_UNDEFINED;
}

cJSON* get_js_modifications(void) {
    if (!js_style_modifications) {
        return NULL;
    }
    
    // === BEZBEDNOSNI ŠTIT ZA SrbinOs / QEMU HEAP ===
    // Proveravamo da li je koren cJSON objekta validan pre dupliranja
    if (js_style_modifications->string) {
        if (strstr(js_style_modifications->string, "::") != NULL) {
            if(INFO_MESSAGES_JS)  printf("⚠️ [JS Modifications Shield] Presrećena korupcija cJSON stabla (::). Resetujem modifikacije radi stabilnosti.\n");
            cleanup_js_modifications();
            return NULL;
        }
    }
    
    // Dubinska provera unutrašnjih elemenata niza
    cJSON *child = js_style_modifications->child;
    while (child) {
        if (child->string && strstr(child->string, "::") != NULL) {
            if(INFO_MESSAGES_JS)   printf("⚠️ [JS Modifications Shield] Izbacujem korumpirani element '%s' iz stabla.\n", child->string);
            
            // Bezbedno vadimo pokvareni element iz niza pre nego što sruši cJSON_Duplicate
            cJSON *next = child->next;
            cJSON_DetachItemFromObject(js_style_modifications, child->string);
            cJSON_Delete(child);
            child = next;
            continue;
        }
        child = child->next;
    }

    // Sada je stablo 100% očišćeno i bezbedno za dupliranje u HelenOS-u
    return cJSON_Duplicate(js_style_modifications, 1);
}

void cleanup_js_modifications(void) {
    if (js_style_modifications) {
        cJSON_Delete(js_style_modifications);
        js_style_modifications = NULL;
    }
}


cJSON* get_computed_layout_from_js(JSContext *ctx) {
    if (!ctx) return NULL;
    
    // Get the layout from JavaScript global variable
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue layout_prop = JS_GetPropertyStr(ctx, global, "__computedLayout");
    
    cJSON *layout_json = NULL;
    
    if (!JS_IsUndefined(layout_prop) && !JS_IsNull(layout_prop)) {
        const char *json_str = JS_ToCString(ctx, layout_prop);
        if (json_str && strlen(json_str) > 0) {
            if(INFO_MESSAGES_JS)  printf("DEBUG: Got layout JSON: %s\n", json_str);
            layout_json = cJSON_Parse(json_str);
            if (!layout_json) {
                if(INFO_MESSAGES_JS)     printf("WARNING: Failed to parse layout JSON from JS: %s\n", json_str);
            }
            JS_FreeCString(ctx, json_str);
        }
    }
    
    JS_FreeValue(ctx, layout_prop);
    JS_FreeValue(ctx, global);
    
    return layout_json;
}


// Set the actual document for DOM access
void js_set_document(JSContext *ctx, lxb_html_document_t *document) {
    global_document = document;
    if(INFO_MESSAGES_JS) printf("DOM Bridge: Connected QuickJS to Lexbor document\n");
}


// Store event callback for later execution
static void __attribute__((unused)) store_event_callback(const char *element_id, const char *event, JSValue callback)  {
    // Add to your event_callbacks array
    extern JSEventCallback *event_callbacks;
    extern int event_callback_count;
    
    event_callbacks = realloc(event_callbacks, (event_callback_count + 1) * sizeof(JSEventCallback));
    event_callbacks[event_callback_count].element_id = strdup(element_id);
    event_callbacks[event_callback_count].event_type = strdup(event);
    event_callbacks[event_callback_count].callback = callback;
    event_callback_count++;
}


// Set style via native bridge
void set_style_via_native(const char *element_id, const char *property, const char *value) {
    if (!global_pauk_ui || !global_pauk_ui->rendering_json) return;
    
    // Find the element in the DOM
    cJSON *target = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
    if (!target) {
        if(INFO_MESSAGES_JS) printf("❌ Element not found: %s\n", element_id);
        return;
    }
    
    // Directly update the element's style property
    if (strcmp(property, "background-color") == 0) {
        set_json_string(target, "bg_color", value);
        set_json_string(target, "hover_bg", value); 
        if(INFO_MESSAGES_JS)  printf("✅ Set bg_color of %s to %s\n", element_id, value);
    } else if (strcmp(property, "color") == 0) {
        set_json_string(target, "color", value);
        if(INFO_MESSAGES_JS) printf("✅ Set color of %s to %s\n", element_id, value);
    } else {
        set_json_string(target, property, value);
    }
}


char* get_element_text_content(const char *element_id) {
    // Find element in your DOM by string ID
    cJSON *element = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
    if (!element) return strdup("");
    
    // Get text content from element
    const char *text = get_json_string(element, "text", "");
    if (text && text[0]) return strdup(text);
    
    // Check children for text node
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
        cJSON *text_node = cJSON_GetArrayItem(children, 0);
        if (text_node && strcmp(get_json_string(text_node, "tag", ""), "text") == 0) {
            const char *content = get_json_string(text_node, "content", "");
            if (content) return strdup(content);
        }
    }
    
    return strdup("");
}


static JSValue js_style_setColor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *value = JS_ToCString(ctx, argv[0]);
    cJSON *target = js_get_cjson_element(ctx, this_val);
    
    if (target && value) {
        if(INFO_MESSAGES_JS) printf("🎨 setColor('%s') found element\n", value);
        set_json_string(target, "color", value);
        
        cJSON *children = cJSON_GetObjectItem(target, "children");
        if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
            cJSON *text_node = cJSON_GetArrayItem(children, 0);
            if (text_node && strcmp(get_json_string(text_node, "tag", ""), "text") == 0) {
                set_json_string(text_node, "color", value);
            }
        }
        set_json_bool(target, "needs_layout", true);
        g_render_needs_layout = 1;
        // ===== NEMA refresh_page_after_js() OVDE! =====
    }
    
    JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
}

// style.setBackgroundColor(value)
static JSValue js_style_setBackgroundColor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *value = JS_ToCString(ctx, argv[0]);
    cJSON *target = js_get_cjson_element(ctx, this_val);
    
    if (target && value) {
        if(INFO_MESSAGES_JS) printf("🎨 setBackgroundColor('%s') found element\n", value);
        set_json_string(target, "bg_color", value);
        set_json_bool(target, "needs_layout", true);
        g_render_needs_layout = 1;
        // ===== NEMA refresh_page_after_js() OVDE! =====
    }
    
    JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
}


// element.setText(value)
static JSValue js_element_setText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *text = JS_ToCString(ctx, argv[0]);

    // 🚀 ASCII-TRANSLIT ŠTIT: Prevodi UTF-8 kvačice u ASCII karaktere da ih TrueType font ne bi gutao!
    char *cisti_text = NULL;
    if (text) {
        cisti_text = malloc(strlen(text) + 64); // Malo veći bafer zbog "dj" tranzicije
        if (cisti_text) {
            unsigned char *src = (unsigned char *)text;
            char *dst = cisti_text;
            
            while (*src) {
                // Slova Č i č (UTF-8: C4 8C i C4 8D)
                if (*src == 0xC4 && *(src + 1) == 0x8C) { *dst++ = 'C'; src += 2; }
                else if (*src == 0xC4 && *(src + 1) == 0x8D) { *dst++ = 'c'; src += 2; }
                // Slova Ć i ć (UTF-8: C4 86 i C4 87)
                else if (*src == 0xC4 && *(src + 1) == 0x86) { *dst++ = 'C'; src += 2; }
                else if (*src == 0xC4 && *(src + 1) == 0x87) { *dst++ = 'c'; src += 2; }
                // Slova Š i š (UTF-8: C5 A0 i C5 A1)
                else if (*src == 0xC5 && *(src + 1) == 0xA0) { *dst++ = 'S'; src += 2; }
                else if (*src == 0xC5 && *(src + 1) == 0xA1) { *dst++ = 's'; src += 2; }
                // Slova Ž i ž (UTF-8: C5 BD i C5 BE)
                else if (*src == 0xC5 && *(src + 1) == 0xBD) { *dst++ = 'Z'; src += 2; }
                else if (*src == 0xC5 && *(src + 1) == 0xBE) { *dst++ = 'z'; src += 2; }
                // Slova Đ i đ (UTF-8: C4 90 i C4 91)
                else if (*src == 0xC4 && *(src + 1) == 0x90) { *dst++ = 'D'; *dst++ = 'j'; src += 2; }
                else if (*src == 0xC4 && *(src + 1) == 0x91) { *dst++ = 'd'; *dst++ = 'j'; src += 2; }
                else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';
        }
    }

    // Ako je malloc uspeo, koristimo cisti_text, u suprotnom fallback na originalni text
    const char *finalni_tekst = cisti_text ? cisti_text : text;

    cJSON *target = js_get_cjson_element(ctx, this_val);
    
    if (target && finalni_tekst) {
        if(INFO_MESSAGES_JS) printf("📝 setText('%s') found element\n", finalni_tekst);
        set_json_string(target, "text", finalni_tekst);
        
        cJSON *children = cJSON_GetObjectItem(target, "children");
        if (!children) {
            children = cJSON_AddArrayToObject(target, "children");
        }
        
        if (cJSON_IsArray(children)) {
            if (cJSON_GetArraySize(children) > 0) {
                cJSON *text_node = cJSON_GetArrayItem(children, 0);
                if (text_node && strcmp(get_json_string(text_node, "tag", ""), "text") == 0) {
                    set_json_string(text_node, "text", finalni_tekst);
                    set_json_string(text_node, "content", finalni_tekst);
                }
            } 
            else {
                cJSON *new_text_node = cJSON_CreateObject();
                set_json_string(new_text_node, "tag", "text");
                set_json_string(new_text_node, "type", "text");
                set_json_string(new_text_node, "display", "inline");
                set_json_string(new_text_node, "content", finalni_tekst);
                set_json_string(new_text_node, "text", finalni_tekst);
                
                int p_font_size = get_json_number(target, "font_size", 14);
                set_json_number(new_text_node, "font_size", p_font_size);
                
                const char *p_font_weight = get_json_string(target, "font_weight", "normal");
                set_json_string(new_text_node, "font_weight", p_font_weight);
                
                const char *p_color = get_json_string(target, "color", "#000000");
                set_json_string(new_text_node, "color", p_color);
                
                set_json_number(new_text_node, "x", -99999);
                set_json_number(new_text_node, "y", -99999);
                
                set_json_bool(new_text_node, "is_text", 1);
                set_json_bool(new_text_node, "is_inline", 1);
                set_json_bool(new_text_node, "needs_layout", 1);
                set_json_number(new_text_node, "width", 0);
                set_json_number(new_text_node, "height", 0);
                
                static int next_txt_id = 500000;
                int text_id = next_txt_id++;
                set_json_number(new_text_node, "id", text_id);
                set_json_number(new_text_node, "element_id", text_id);
                set_json_number(new_text_node, "parent_id", get_json_number(target, "element_id", -1));
                
                cJSON_AddItemToArray(children, new_text_node);
            }
        }
        
        set_json_bool(target, "needs_layout", true);
        g_render_needs_layout = 1;
    }
    
    // 🚀 ČIŠĆENJE: Oslobađamo alociranu memoriju da OS ne ostane bez RAM-a
    if (cisti_text) free(cisti_text);
    if (text) JS_FreeCString(ctx, text);
    
    return JS_UNDEFINED;
}



// Get element width
static JSValue js_element_get_offsetWidth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    int width = 0;
    if (element_id && global_pauk_ui && global_pauk_ui->rendering_json) {
        cJSON *body = cJSON_GetArrayItem(global_pauk_ui->rendering_json, 0);
        cJSON *target = find_element_by_string_id(body, element_id);
        if (target) {
            width = get_json_number(target, "width", 0);
        }
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    return JS_NewInt32(ctx, width);
}

// Get element height
static JSValue js_element_get_offsetHeight(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if(INFO_MESSAGES_JS)  printf("📏 js_element_get_offsetHeight CALLED\n");
    
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    int height = 0;
    
    if (element_id && global_pauk_ui && global_pauk_ui->rendering_json) {
        cJSON *body = cJSON_GetArrayItem(global_pauk_ui->rendering_json, 0);
        cJSON *target = find_element_by_string_id(body, element_id);
        if (target) {
            height = get_json_number(target, "height", 0);
            if(INFO_MESSAGES_JS) printf("   offsetHeight for '%s' = %d\n", element_id, height);
        }
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    
    return JS_NewInt32(ctx, height);
}

// Get element X position
static JSValue js_element_get_offsetLeft(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    cJSON *target = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
    int x = target ? get_json_number(target, "x", 0) : 0;
    
    JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    return JS_NewInt32(ctx, x);
}

// Get element Y position
static JSValue js_element_get_offsetTop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    cJSON *target = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
    int y = target ? get_json_number(target, "y", 0) : 0;
    
    JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    return JS_NewInt32(ctx, y);
}

static JSValue js_element_setOnclick(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
   if(INFO_MESSAGES_JS) printf("🔵 setOnclick called!\n");
    
    if (argc < 1) return JS_UNDEFINED;
    if (!JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
    
    // Get element ID
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    if (element_id && element_id[0]) {
        // Check if already exists
        for (int i = 0; i < js_callback_count; i++) {
            if (strcmp(js_callbacks[i].element_id, element_id) == 0) {
                // Replace existing callback
                JS_FreeValue(ctx, js_callbacks[i].callback);
                js_callbacks[i].callback = JS_DupValue(ctx, argv[0]);
                if(INFO_MESSAGES_JS)printf("✅ Updated callback for %s\n", element_id);
                JS_FreeCString(ctx, element_id);
                JS_FreeValue(ctx, id_val);
                return JS_UNDEFINED;
            }
        }
        
        // Add new callback
        js_callbacks = realloc(js_callbacks, (js_callback_count + 1) * sizeof(JSCallback));
        js_callbacks[js_callback_count].element_id = strdup(element_id);
        js_callbacks[js_callback_count].callback = JS_DupValue(ctx, argv[0]);
        js_callback_count++;
        if(INFO_MESSAGES_JS) printf("✅ Added callback for %s (total: %d)\n", element_id, js_callback_count);
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    return JS_UNDEFINED;
}


static JSValue js_element_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    
    const char *event_type = JS_ToCString(ctx, argv[0]);
    JSValue callback = argv[1];
    
    if (!event_type || !JS_IsFunction(ctx, callback)) {
        if (event_type) JS_FreeCString(ctx, event_type);
        return JS_UNDEFINED;
    }
    
    // Get element ID
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    if(INFO_MESSAGES_JS) printf("📢 addEventListener: '%s' on element '%s'\n", event_type, element_id ? element_id : "NULL");
    
    if (element_id) {
        // Store the callback in your existing js_callbacks array
        // Check if already exists
        int found = -1;
        for (int i = 0; i < js_callback_count; i++) {
            if (strcmp(js_callbacks[i].element_id, element_id) == 0) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            // Replace existing callback
            JS_FreeValue(ctx, js_callbacks[found].callback);
            js_callbacks[found].callback = JS_DupValue(ctx, callback);
            if(INFO_MESSAGES_JS) printf("   ✅ Updated callback for %s\n", element_id);
        } else {
            // Add new callback
            js_callbacks = realloc(js_callbacks, (js_callback_count + 1) * sizeof(JSCallback));
            js_callbacks[js_callback_count].element_id = strdup(element_id);
            js_callbacks[js_callback_count].callback = JS_DupValue(ctx, callback);
            js_callback_count++;
            if(INFO_MESSAGES_JS) printf("   ✅ Added callback for %s (total: %d)\n", element_id, js_callback_count);
        }
        
        JS_FreeCString(ctx, element_id);
    }
    
    JS_FreeValue(ctx, id_val);
    JS_FreeCString(ctx, event_type);
    return JS_UNDEFINED;
}

// Real document.getElementById implementation
static JSValue js_document_get_element_by_id_real(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
 
    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_UNDEFINED;
 
    if(INFO_MESSAGES_JS)printf("🔍 getElementById called for: '%s'\n", id);

    cJSON *json_element = NULL;
    if (global_document_json) {
        // ===== FIX: Get the first element (body) from the array =====
        cJSON *body_element = cJSON_GetArrayItem(global_document_json, 0);
        if (body_element) {
            json_element = find_element_by_id_in_json(body_element, id);
        }
        if(INFO_MESSAGES_JS) printf("   JSON element found: %s\n", json_element ? "YES" : "NO");
    }

    // Create JS object for the element
    JSValue obj = JS_NewObject(ctx);
 
    // If we have real data, use it
    if (json_element) {
        const char *tag = get_json_string(json_element, "tag", "div");
        JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, tag));
    }
    
    const char *tag_name = get_json_string(json_element, "tag", "");
    if (strcmp(tag_name, "form") == 0) {
        JS_SetPropertyStr(ctx, obj, "submit", JS_NewCFunction(ctx, js_form_submit, "submit", 0));
    }

    // Store element ID for later reference
    JS_SetPropertyStr(ctx, obj, "_elementId", JS_NewString(ctx, id));
 
    // Property: id (read-only)
    JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, id));
 
    // ===== DIMENSION AND POSITION GETTERS (as real properties, not functions) =====
    // Create getter functions
JS_SetPropertyStr(ctx, obj, "offsetWidth", 
    JS_NewCFunction(ctx, js_element_get_offsetWidth, "offsetWidth", 0));
JS_SetPropertyStr(ctx, obj, "offsetHeight", 
    JS_NewCFunction(ctx, js_element_get_offsetHeight, "offsetHeight", 0));
JS_SetPropertyStr(ctx, obj, "offsetLeft", 
    JS_NewCFunction(ctx, js_element_get_offsetLeft, "offsetLeft", 0));
JS_SetPropertyStr(ctx, obj, "offsetTop", 
    JS_NewCFunction(ctx, js_element_get_offsetTop, "offsetTop", 0));
    
    // ===== DOM MANIPULATION METHODS =====
    JS_SetPropertyStr(ctx, obj, "innerHTML", 
        JS_NewCFunction(ctx, js_element_set_innerHTML, "innerHTML", 1));
    JS_SetPropertyStr(ctx, obj, "textContent", 
        JS_NewCFunction(ctx, js_element_set_innerHTML, "textContent", 1));

    // Methods
    JS_SetPropertyStr(ctx, obj, "setText", 
        JS_NewCFunction(ctx, js_element_setText, "setText", 1));
    JS_SetPropertyStr(ctx, obj, "setOnclick", 
        JS_NewCFunction(ctx, js_element_setOnclick, "setOnclick", 1));
    JS_SetPropertyStr(ctx, obj, "toString",
        JS_NewCFunction(ctx, js_element_toString, "toString", 0));
    // Style object with methods
    JSValue style_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, style_obj, "_elementId", JS_NewString(ctx, id));
    JS_SetPropertyStr(ctx, style_obj, "setColor", 
        JS_NewCFunction(ctx, js_style_setColor, "setColor", 1));
    JS_SetPropertyStr(ctx, style_obj, "setBackgroundColor", 
        JS_NewCFunction(ctx, js_style_setBackgroundColor, "setBackgroundColor", 1));
    JS_SetPropertyStr(ctx, obj, "style", style_obj);
 
    JS_SetPropertyStr(ctx, obj, "addEventListener", 
        JS_NewCFunction(ctx, js_add_event_listener, "addEventListener", 2));
    
    JS_SetPropertyStr(ctx, obj, "getAttribute", 
            JS_NewCFunction(ctx, js_element_getAttribute, "getAttribute", 1));
    JS_SetPropertyStr(ctx, obj, "setAttribute", 
            JS_NewCFunction(ctx, js_element_setAttribute, "setAttribute", 2));
    
    // === InerHTML=====
        // 🚀 DEFINISANJE INNERHTML SVOJSTVA (Pravi JavaScript Getter i Setter)
        JSAtom inner_html_atom = JS_NewAtom(ctx, "innerHTML");
        JS_DefinePropertyGetSet(ctx, obj, inner_html_atom,
            JS_NewCFunction(ctx, js_element_getInnerHTML, "get_innerHTML", 0),
            JS_NewCFunction(ctx, js_element_setInnerHTML, "set_innerHTML", 1),
            JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
        JS_FreeAtom(ctx, inner_html_atom); // Odmah bezbedno čistimo atom iz QuickJS steka
    

    // ===== UNSUPPORTED METHODS (but warn instead of crash) =====
    JS_SetPropertyStr(ctx, obj, "removeEventListener", 
        JS_NewCFunction(ctx, js_unsupported, "removeEventListener", 2));

    JS_SetPropertyStr(ctx, obj, "appendChild",  
        JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, obj, "removeChild", 
        JS_NewCFunction(ctx, js_element_removeChild, "removeChild", 1));

 
    // Add safe properties that return sensible defaults
    JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "children", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, obj, "firstChild", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "lastChild", JS_NULL);
 
                    // 🚀 AKTIVIRANJE VALUE I CLASSLIST SVOJSTAVA
    JS_SetPropertyStr(ctx, obj, "getValue", JS_NewCFunction(ctx, js_element_getValue, "getValue", 0));
    JS_SetPropertyStr(ctx, obj, "setValue", JS_NewCFunction(ctx, js_element_setValue, "setValue", 1));
    JS_SetPropertyStr(ctx, obj, "classList", JS_NewCFunction(ctx, js_element_classList, "classList", 0));


    JS_SetPropertyStr(ctx, obj, "toString",
        JS_NewCFunction(ctx, js_element_toString, "toString", 0));


    if(INFO_MESSAGES_JS) printf("   Created JS object with setText method for id='%s'\n", id);

    JS_FreeCString(ctx, id);
    return obj;
}


// Console.log implementation
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS console: ");
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            size_t log_len = strlen(str);
            if (log_len > 150) {
                // Skraćivanje predugačkih logova (kod, base64)
                printf("%.150s... [Truncated %zu chars] ", str, log_len - 150);
            } else {
                printf("%s ", str);
            }
            JS_FreeCString(ctx, str);
        }
    }
    printf("\n");
    fflush(stdout);
    return JS_UNDEFINED;
}



static JSValue js_safe_mock_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if(INFO_MESSAGES_JS) printf("JS: Safe mock function called (API not implemented yet)\n");
    return JS_UNDEFINED;
}

// Safe mock function
static JSValue js_alert(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0) {
        const char *msg = JS_ToCString(ctx, argv[0]);
        if (msg) {
            // Show alert dialog using your UI
            if(INFO_MESSAGES_JS)  printf("🔔 ALERT: %s\n", msg);
            // Call your UI function to show a message box
            // show_alert_dialog(msg);
            JS_FreeCString(ctx, msg);
        }
    }
    return JS_UNDEFINED;
}



// 🚀 COOKIE GETTER: JavaScript traži da pročita kolačiće (document.cookie)
// 🚀 PRAVA, ŽIVA INSTANCA KOLAČIĆA: Stvaramo je direktno ovde u RAM-u!
// Pošto je nemaš nigde u projektu, ovo je zvanično srce tvog cookie sistema.
CookieJar g_cookie_jar = { .count = 0 };

// 🚀 COOKIE GETTER: JavaScript čita sve kolačiće (document.cookie)
static JSValue js_document_getCookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    char *bafer = malloc(4096);
    if (!bafer) return JS_NewString(ctx, "");
    bafer[0] = '\0'; // 🚀 FIX: Ispravno inicijalizujemo prazan string na steku
    
    for (int i = 0; i < g_cookie_jar.count; i++) {
        if (strlen(g_cookie_jar.cookies[i].name) > 0) {
            char privremeni[512]; // 🚀 FIX: Eksplicitni bafer za spajanje stringa
            snprintf(privremeni, sizeof(privremeni), "%s=%s", 
                     g_cookie_jar.cookies[i].name, g_cookie_jar.cookies[i].value);
                     
            if (strlen(bafer) > 0) {
                strcat(bafer, "; ");
            }
            strcat(bafer, privremeni);
        }
    }
    
    JSValue js_str = JS_NewString(ctx, bafer);
    free(bafer);
    return js_str;
}

// 🚀 COOKIE SETTER: JavaScript upisuje pojedinačni "ime=vrednost" (document.cookie = "user=pauk")
static JSValue js_document_setCookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *cookie_raw = JS_ToCString(ctx, *argv);
    if (cookie_raw) {
        if (INFO_MESSAGES_JS) {
            printf("🍪 [Cookie Bridge] Nativno parsiram i upisujem kolačić: %s\n", cookie_raw);
        }
        
        char *kopija = strdup(cookie_raw);
        if (kopija) {
            char *prvi_blok = strtok(kopija, ";");
            if (prvi_blok) {
                char *jednako = strchr(prvi_blok, '=');
                if (jednako) {
                    *jednako = '\0';
                    char *ime = prvi_blok;
                    char *vrednost = jednako + 1;
                    
                    while (*ime == ' ') ime++;
                    while (*vrednost == ' ') vrednost++;
                    
                    int nadjen_indeks = -1;
                    for (int i = 0; i < g_cookie_jar.count; i++) {
                        if (strcmp(g_cookie_jar.cookies[i].name, ime) == 0) {
                            nadjen_indeks = i;
                            break;
                        }
                    }
                    
                    if (nadjen_indeks == -1 && g_cookie_jar.count < MAX_COOKIES) {
                        nadjen_indeks = g_cookie_jar.count;
                        g_cookie_jar.count++;
                    }
                    
                    if (nadjen_indeks != -1) {
                        strncpy(g_cookie_jar.cookies[nadjen_indeks].name, ime, sizeof(g_cookie_jar.cookies[nadjen_indeks].name) - 1);
                        g_cookie_jar.cookies[nadjen_indeks].name[sizeof(g_cookie_jar.cookies[nadjen_indeks].name) - 1] = '\0';
                        
                        strncpy(g_cookie_jar.cookies[nadjen_indeks].value, vrednost, sizeof(g_cookie_jar.cookies[nadjen_indeks].value) - 1);
                        g_cookie_jar.cookies[nadjen_indeks].value[sizeof(g_cookie_jar.cookies[nadjen_indeks].value) - 1] = '\0';
                        
                        // 🚀 POTPUNA IZOLACIJA: Sklonili smo dynamic_hostname i g_current_host da ne bune linker!
                        // Upisujemo podrazumevani domen direktno u strukturu, bez ikakvih spoljnih varijabli
                        strncpy(g_cookie_jar.cookies[nadjen_indeks].domain, "://google.com", sizeof(g_cookie_jar.cookies[nadjen_indeks].domain) - 1);
                        strncpy(g_cookie_jar.cookies[nadjen_indeks].path, "/", sizeof(g_cookie_jar.cookies[nadjen_indeks].path) - 1);
                        g_cookie_jar.cookies[nadjen_indeks].is_session = true;
                        
                        if (INFO_MESSAGES_JS) {
                            printf("   ✅ Kolačić uspešno upisan u slot %d: %s=%s\n", nadjen_indeks, ime, vrednost);
                        }
                    }
                }
            }
            free(kopija);
        }
        JS_FreeCString(ctx, cookie_raw);
    }
    return JS_UNDEFINED;
}



// Register DOM API (same functionality as your Duktape version)
void js_register_dom_api(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    
    // Create console object
    JSValue console_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console_obj, "log", 
        JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console_obj, "error", 
        JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, console_obj, "warn", 
        JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console_obj, "info", 
        JS_NewCFunction(ctx, js_console_log, "info", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console_obj);

    // =========================================================================
    // 🛡️ ARHITEKTONSKI ŠTIT: DOHVATI ILI KREIRAJ DOCUMENT (RETRIEVE-OR-CREATE)
    // Proveravamo da li je "document" već stvoren unutar globalnog QuickJS opsega.
    // Ako jeste (ubrizgan preko modernog štita), uzimamo njega i dopisujemo C funkcije,
    // čime sprečavamo brisanje tvoje elem.submit i elem.setAttribute regulacije! [sk]
    // =========================================================================
    JSValue document_obj = JS_GetPropertyStr(ctx, global_obj, "document");
    
    if (JS_IsUndefined(document_obj) || JS_IsNull(document_obj) || JS_IsException(document_obj)) {
        // Ako document ne postoji, tek onda pravimo potpuno novi objekat
        document_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global_obj, "document", JS_DupValue(ctx, document_obj));
    }
    // =========================================================================

    JS_SetPropertyStr(ctx, document_obj, "createElement",
        JS_NewCFunction(ctx, js_document_createElement, "createElement", 1));

    if (global_document) {
        JS_SetPropertyStr(ctx, document_obj, "getElementById",
            JS_NewCFunction(ctx, js_document_get_element_by_id_real, "getElementById", 1));
        
        lxb_html_body_element_t *body = lxb_html_document_body_element(global_document);
        if (body) {
            JSValue body_obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, body_obj, "tagName", JS_NewString(ctx, "BODY"));
            JS_SetPropertyStr(ctx, document_obj, "body", body_obj);
        } else {
            JS_SetPropertyStr(ctx, document_obj, "body", JS_NULL);
        }
    }

    JS_SetPropertyStr(ctx, document_obj, "querySelector",
        JS_NewCFunction(ctx, js_document_querySelector, "querySelector", 1));
    JS_SetPropertyStr(ctx, document_obj, "querySelectorAll",
        JS_NewCFunction(ctx, js_document_querySelectorAll, "querySelectorAll", 1));
    
    JS_SetPropertyStr(ctx, document_obj, "title", JS_NewString(ctx, "Real Document"));
    
    // Osiguravamo da se ažurirani document objekat bezbedno vrati u globalni opseg! [sk]
    JS_SetPropertyStr(ctx, global_obj, "document", document_obj);
    
    // Create window object
    JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));
    
    // ========== ADD REAL ALERT ==========
    JS_SetPropertyStr(ctx, global_obj, "alert", JS_NewCFunction(ctx, js_alert, "alert", 1));
    // ===================================
    JSValue urlsp = JS_NewCFunction(ctx, js_url_search_params, "URLSearchParams", 1);
    JS_SetPropertyStr(ctx, global_obj, "URLSearchParams", urlsp);
    // ========== ADD TIMER FUNCTIONS ==========
    JS_SetPropertyStr(ctx, global_obj, "setTimeout", 
        JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global_obj, "setInterval", 
        JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global_obj, "clearTimeout", 
        JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global_obj, "clearInterval", 
        JS_NewCFunction(ctx, js_clearTimeout, "clearInterval", 1));
    
    JSValue image_ctor = JS_NewCFunction2(ctx, js_image_constructor, "Image", 0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, global_obj, "Image", image_ctor);
        // =========================================
    // 🚀 OŽIVLJAVANJE FUNKCIJA: Sklonili smo ih iz mock_functions i vezali za prave C handlere
    JS_SetPropertyStr(ctx, global_obj, "removeAttribute", 
        JS_NewCFunction(ctx, js_element_removeAttribute, "removeAttribute", 1));
    JS_SetPropertyStr(ctx, global_obj, "focus", 
        JS_NewCFunction(ctx, js_element_focus, "focus", 0));
    JS_SetPropertyStr(ctx, global_obj, "blur", 
        JS_NewCFunction(ctx, js_element_focus, "blur", 0)); // Blur može koristiti isti focus handler
 
 
     // 🚀 NATIVNI COOKIE MOST: Definišemo document.cookie kao živi C Getter i Setter!
     JSAtom cookie_atom = JS_NewAtom(ctx, "cookie");
     JS_DefinePropertyGetSet(ctx, document_obj, cookie_atom,
         JS_NewCFunction(ctx, js_document_getCookie, "get_cookie", 0),
         JS_NewCFunction(ctx, js_document_setCookie, "set_cookie", 1),
         JS_PROP_CONFIGURABLE);
     JS_FreeAtom(ctx, cookie_atom);
 
        // =========================================
    // Register safe mock functions for common web APIs (excluding alert)
    const char *mock_functions[] = {
        "initializePage", "incrementCounter", "updateCounterDisplay",
        "confirm", NULL
    };
    
    for (int i = 0; mock_functions[i] != NULL; i++) {
        if (strcmp(mock_functions[i], "alert") == 0) continue;
        
        JS_SetPropertyStr(ctx, global_obj, mock_functions[i],
            JS_NewCFunction(ctx, js_safe_mock_function, mock_functions[i], 0));
    }
    
    // ========== ADD NATIVE BRIDGE ==========
    JSValue native_bridge = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, native_bridge, "setStyle", 
                     JS_NewCFunction(ctx, js_native_set_style, "setStyle", 3));
    JS_SetPropertyStr(ctx, native_bridge, "requestRender",
                     JS_NewCFunction(ctx, js_request_render, "requestRender", 0));
    JS_SetPropertyStr(ctx, native_bridge, "getComputedLayout",
                     JS_NewCFunction(ctx, js_get_computed_layout, "getComputedLayout", 0));
    JS_SetPropertyStr(ctx, native_bridge, "updateElement",
                     JS_NewCFunction(ctx, js_update_element, "updateElement", 2));
    
    JS_SetPropertyStr(ctx, global_obj, "__native", native_bridge);
    
    JS_SetPropertyStr(ctx, global_obj, "fetch",
        JS_NewCFunction(ctx, js_fetch, "fetch", 2));

    JS_SetPropertyStr(ctx, document_obj, "body", 
            JS_NewCFunction(ctx, js_document_get_body, "body", 0));

    // ===== ADD UNSUPPORTED GLOBAL FUNCTIONS (with warnings) =====
    const char *unsupported_globals[] = {
        "XMLHttpRequest",
        "localStorage",
        "sessionStorage",
        "WebSocket",
        "IndexedDB",
        "requestAnimationFrame",
        "cancelAnimationFrame",
        "matchMedia",
        "getComputedStyle",
        "open",
        "close",
        "scrollTo",
        "scrollBy",
        NULL
    };
    
    for (int i = 0; unsupported_globals[i] != NULL; i++) {
        JS_SetPropertyStr(ctx, global_obj, unsupported_globals[i],
            JS_NewCFunction(ctx, js_unsupported, unsupported_globals[i], 0));
    }
    
    // ===== ADD UNSUPPORTED PROPERTIES (with warnings) =====
    const char *unsupported_props[] = {
        "screenX", "screenY",
        NULL
    };
    
    for (int i = 0; unsupported_props[i] != NULL; i++) {
        JS_SetPropertyStr(ctx, global_obj, unsupported_props[i], JS_UNDEFINED);
        if(INFO_MESSAGES_JS)  printf("⚠️ Global property '%s' not supported\n", unsupported_props[i]);
    }

    // =========================================================================
    // 🚀 DYNAMIC SHIELD ENVIRONMENT: SAFE PROTOCOL DETECTION
    // =========================================================================
    const char *current_url = "about:blank";
    if (g_current_base_url && g_current_base_url[0] != '\0') {
        current_url = g_current_base_url;
    }
    
    char dynamic_hostname[512];
    memset(dynamic_hostname, 0, sizeof(dynamic_hostname));
    strcpy(dynamic_hostname, "localhost");
    
    int is_pure_local_file = (strncmp(current_url, "file:", 5) == 0) || 
                             (current_url[0] == '/') || 
                             (strstr(current_url, "://") == NULL);
                             
    if (is_pure_local_file) {
        strcpy(dynamic_hostname, "local-file-system");
        if(INFO_MESSAGES_JS) printf("📂 [JS Engine] Uspešno detektovan lokalni fajl sistem (%s). Postavljam bezbedan host.\n", current_url);
    } else {
        const char *host_start = strstr(current_url, "://");
        if (host_start) {
            host_start += 3;
            const char *host_end = strchr(host_start, '/');
            int len = host_end ? (host_end - host_start) : (int)strlen(host_start);
            
            const char *port_colon = strchr(host_start, ':');
            if (port_colon && port_colon < (host_end ? host_end : host_start + len)) {
                len = port_colon - host_start;
            }
            
            if (len > 0 && len < 250) {
                memcpy(dynamic_hostname, host_start, len);
                dynamic_hostname[len] = '\0';
            } else {
                strcpy(dynamic_hostname, "network-fallback");
            }
        }
    }

    // =========================================================================
    // 🚀 REVIZIJA DINAMIČKOG BOOTSTRAP STRINGA (SINHRONIZACIJA IDENTITETA)
    // Usaglašavamo UA i platformu sa mobilnim Opera Mini profilom pretraživača! [sk]
    // =========================================================================
    size_t js_buffer_capacity = strlen(current_url) + strlen(dynamic_hostname) + 65536;
    char *dynamic_js_bootstrap = malloc(js_buffer_capacity);
    
    if (dynamic_js_bootstrap) {
        // =========================================================================
        // 🍪 DISK TO JS TUNNEL: ČITANJE ŽIVE SESIJE I UBRIZGAVANJE U QUICKJS
        // Otvaramo cookie.txt koji je naša mrežna sonda upravo sačuvala na disk,
        // izvlačimo sirove NID i AEC parove i pravimo JS string "NID=xyz; AEC=123"
        // koji ubacujemo direktno u runtime document.cookie memoriju!
        // =========================================================================
        char zivi_js_cookies[1024];
        zivi_js_cookies[0] = '\0';
        
        char d_nid[512] = "";
        char d_aec[256] = "";
        
        FILE *f_js_cookie = fopen("cookie.txt", "r");
        if (f_js_cookie != NULL) {
            char l_buf[1024];
            char t_ime[128] = "";
            
            while (fgets(l_buf, sizeof(l_buf), f_js_cookie) != NULL) {
                char *i_ptr = strstr(l_buf, "Ime:     ");
                if (i_ptr) {
                    sscanf(i_ptr + 9, "%s", t_ime);
                }
                char *v_ptr = strstr(l_buf, "Vrednost:");
                if (v_ptr && t_ime[0] != '\0') {
                    char t_val[512] = "";
                    sscanf(v_ptr + 9, "%s", t_val);
                    if (strcmp(t_ime, "NID") == 0) strcpy(d_nid, t_val);
                    if (strcmp(t_ime, "AEC") == 0) strcpy(d_aec, t_val);
                }
            }
            fclose(f_js_cookie);
        }
        
        // Sklapamo čist JS format za document.cookie bez "Cookie:" prefiksa i bez \r\n
        if (d_nid[0] != '\0' && d_aec[0] != '\0') {
            snprintf(zivi_js_cookies, sizeof(zivi_js_cookies), "NID=%s; AEC=%s", d_nid, d_aec);
        } else if (d_nid[0] != '\0') {
            snprintf(zivi_js_cookies, sizeof(zivi_js_cookies), "NID=%s", d_nid);
        }
        // =========================================================================

        // 🚀 UBRIZGAVANJE: Dodajemo TREĆI %s specifikator na mesto _runtime_cookies!
        snprintf(dynamic_js_bootstrap, js_buffer_capacity,
            "var self = globalThis;\n"
            "var top = globalThis;\n"
            "var parent = globalThis;\n"
            "\n"
            "var innerWidth = 800, innerHeight = 600;\n"
            "var outerWidth = 800, outerHeight = 600;\n"
            "\n"
            "// JS PROTOTYPE POLYFILL FOR IMAGE\n"
            "if (typeof Image !== 'undefined') {\n"
            "    Image.prototype.toString = function() { return '[object HTMLImageElement]'; };\n"
            "}\n"
            "var screenX = 0, screenY = 0;\n"
            "var pageXOffset = 0, pageYOffset = 0;\n"
            "\n"
            "// PAUK DINAMIČKI LOKACIJSKI DETEKTOR\n"
            "var _raw_url = '%s';\n"
            "var _url_query = '';\n"
            "var _url_path = '/';\n"
            "var _q_idx = _raw_url.indexOf('?');\n"
            "if (_q_idx !== -1) {\n"
            "    _url_query = _raw_url.substring(_q_idx);\n"
            "    var _path_start = _raw_url.indexOf('//');\n"
            "    if (_path_start !== -1) {\n"
            "        var _path_end = _raw_url.indexOf('/', _path_start + 2);\n"
            "        if (_path_end !== -1) _url_path = _raw_url.substring(_path_end, _q_idx);\n"
            "    }\n"
            "}\n"
            "\n"
            "var location = {\n"
            "    href: _raw_url,\n"
            "    hostname: '%s',\n"
            "    protocol: 'https:',\n"
            "    pathname: _url_path,\n"
            "    search: _url_query,\n"
            "    hash: '',\n"
            "    assign: function(){}, replace: function(){}, reload: function(){}\n"
            "};\n"
            "var history = {\n"
            "    length: 1, back: function(){}, forward: function(){},\n"
            "    go: function(){}, pushState: function(){}, replaceState: function(){}\n"
            "};\n"
            "\n"
            "if (typeof document === 'undefined') {\n"
            "    globalThis.document = { location: location };\n"
            "} else {\n"
            "    if (!document.location) document.location = location;\n"
            "}\n"
            "\n"
            "var _runtime_cookies = '%s';\n"
            "Object.defineProperty(document, 'cookie', {\n"
            "    get: function() { return _runtime_cookies; },\n"
            "    set: function(val) { _runtime_cookies = val; }\n"
            "});\n"
            "\n"
            "// ===== NAVIGATOR SA ISPRAVNIM USER-AGENT-OM =====\n"
            "var navigator = {\n"
            "    userAgent: 'Pauk1.0.0rel.1 libwww-FM/2.14 SSL-MM/1.4.1',\n"
            "    language: 'en-US',\n"
            "    onLine: true,\n"
            "    platform: 'Win64',\n"
            "    appName: 'Netscape',\n"
            "    appVersion: '5.0 (Windows)',\n"
            "    cookieEnabled: true\n"
            "};\n"
            "\n"
            "document.getElementsByTagName = document.getElementsByTagName || function() { return []; };\n"
            "document.getElementsByClassName = document.getElementsByClassName || function() { return []; };\n"
            "document.querySelector = document.querySelector || function() { return null; };\n"
            "document.querySelectorAll = document.querySelectorAll || function() { return []; };\n"
            "\n"
            "window.matchMedia = window.matchMedia || function() {\n"
            "    return { matches: false, addListener: function(){}, removeListener: function(){} };\n"
            "};\n"
            "window.crypto = window.crypto || {\n"
            "    getRandomValues: function(arr) {\n"
            "        for (var i = 0; i < arr.length; i++) arr[i] = Math.floor(Math.random() * 256);\n"
            "        return arr;\n"
            "    }\n"
            "};\n"
            "\n"
            "self.webpackChunk_N_E = self.webpackChunk_N_E || [];\n"
            "\n"
            "// STORAGE & MODERN API SHIELD\n"
            "var _storage_mock = {};\n"
            "window.localStorage = window.localStorage || {\n"
            "    getItem: function(k) { return _storage_mock[k] || null; },\n"
            "    setItem: function(k, v) { _storage_mock[k] = String(v); },\n"
            "    removeItem: function(k) { delete _storage_mock[k]; },\n"
            "    clear: function() { _storage_mock = {}; },\n"
            "    length: 0\n"
            "};\n"
            "window.sessionStorage = window.sessionStorage || window.localStorage;\n"
            "\n"
            "window.requestAnimationFrame = window.requestAnimationFrame || function(cb) { return setTimeout(cb, 16); };\n"
            "window.cancelAnimationFrame = window.cancelAnimationFrame || function(id) { clearTimeout(id); };\n"
            "window.getComputedStyle = window.getComputedStyle || function() { return { getPropertyValue: function() { return ''; } }; };\n"
            "window.scrollTo = window.scrollTo || function() {};\n"
            "window.scrollBy = window.scrollBy || function() {};\n",
            current_url, dynamic_hostname, zivi_js_cookies);



        if (INFO_MESSAGES_JS) {
            printf("🚀 [JS Engine] Dynamic bootstrap initialized for: %s\n", dynamic_hostname);
        }

        JSValue bootstrap_res = JS_Eval(ctx, dynamic_js_bootstrap, strlen(dynamic_js_bootstrap), "bootstrap.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(bootstrap_res)) {
            JSValue exc = JS_GetException(ctx);
            const char *str = JS_ToCString(ctx, exc);
            if (INFO_MESSAGES_JS) {
                printf("❌ [JS Engine Bootstrap Error]: %s\n", str);
            }
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, bootstrap_res);
        free(dynamic_js_bootstrap);
    }

    JS_FreeValue(ctx, global_obj);
}



// Initialize QuickJS engine - SAME FUNCTION NAME as Duktape version
JSContext* js_engine_init(void) {
    if (!global_rt) {
        global_rt = JS_NewRuntime();
        if (!global_rt) {
            if(INFO_MESSAGES_JS)  printf("❌ [JS Engine] Critical: Unable to allocate JSRuntime core structure!\n");
            return NULL;
        }
        
        // ADJUSTED LOWER CAP: Lower the internal engine limit to 12 MB.
        // This ensures the engine doesn't request more heap blocks than the HelenOS task layout permits.
        JS_SetMemoryLimit(global_rt, 12 * 1024 * 1024);
        if(INFO_MESSAGES_JS)printf("🤖 [JS Engine] Initialized task-bounded global JSRuntime core\n");
    } else {
        if(INFO_MESSAGES_JS) printf("🤖 [JS Engine] Reusing stable existing JSRuntime core\n");
    }
    
    JSContext *ctx = JS_NewContext(global_rt);
    if (!ctx) {
        if(INFO_MESSAGES_JS) printf("❌ [JS Engine] Critical: JS_NewContext failed! No task memory left.\n");
        return NULL;
    }
    
    g_js_context = ctx; 
    js_register_timers(ctx);
    
    // 🛡️ PC DESKTOP JS ŠTIT (UGRADNJA NA IZLAZU):
    // Odmah nakon što kreiramo čist QuickJS kontekst i registrujemo tajmere,
    // prisilno ubrizgavamo lažne 'window', 'document' i 'navigator' objekte u RAM!
    // Ovo garantuje da će jQuery v3.3.1 i Bootstrap 5 na djurkovicdent.me odmah videti
    // veliku desktop rezoluciju i isključiti sve štetne responsive transformacije.
    inicijalizuj_moderni_js_stit(ctx);
 //   init_cookie_jar(&g_cookie_jar);
printf("🍪 Cookie Jar initialized at startup\n");
    return ctx;
}


   void js_register_dom(JSContext *ctx) {
    // Now register DOM API with global_document set
    js_register_dom_api(ctx);
}

// Cleanup - SAME FUNCTION NAME as Duktape version
void js_engine_cleanup(JSContext *ctx) {
    if(INFO_MESSAGES_JS) printf("🧹 js_engine_cleanup: START\n");
    
    // ===== 1. OČISTI MAPU JS OBJEKAT → cJSON =====
    if(INFO_MESSAGES_JS) {
        printf("   Cleaning JS-CJSON map (%d entries)\n", js_cjson_ref_count);
    }
    for (int i = 0; i < js_cjson_ref_count; i++) {
        // Oslobodi JS vrednosti u mapi
        JS_FreeValue(ctx, js_cjson_refs[i].js_obj);
    }
    free(js_cjson_refs);
    js_cjson_refs = NULL;
    js_cjson_ref_count = 0;
    if(INFO_MESSAGES_JS) printf("   ✅ JS-CJSON map cleaned\n");
    
    // ===== 2. OČISTI DETACHED ELEMENTE =====
    for (int i = 0; i < detached_count; i++) {
        if (detached_elements[i]) {
            cJSON_Delete(detached_elements[i]);
        }
    }
    free(detached_elements);
    detached_elements = NULL;
    detached_count = 0;
    if(INFO_MESSAGES_JS) printf("   ✅ Detached elements cleaned\n");
    
    // ===== 3. OČISTI PENDING APPEND ZAHTEVE =====
    for (int i = 0; i < pending_append_count; i++) {
        free(pending_append_requests[i].parent_id);
        free(pending_append_requests[i].child_id);
    }
    free(pending_append_requests);
    pending_append_requests = NULL;
    pending_append_count = 0;
    if(INFO_MESSAGES_JS) printf("   ✅ Pending append requests cleaned\n");
    
    // ===== 4. OČISTI JS CALLBACK-OVE =====
    for (int i = 0; i < js_callback_count; i++) {
        if (!JS_IsUndefined(js_callbacks[i].callback) && !JS_IsNull(js_callbacks[i].callback)) {
            JS_FreeValue(ctx, js_callbacks[i].callback);
        }
        free(js_callbacks[i].element_id);
    }
    free(js_callbacks);
    js_callbacks = NULL;
    js_callback_count = 0;
    if(INFO_MESSAGES_JS) printf("   ✅ JS callbacks cleaned\n");
    
    // ===== 5. OČISTI EVENT CALLBACK-OVE =====
    for (int i = 0; i < event_callback_count; i++) {
        free(event_callbacks[i].element_id);
        free(event_callbacks[i].event_type);
        if (!JS_IsUndefined(event_callbacks[i].callback) && !JS_IsNull(event_callbacks[i].callback)) {
            JS_FreeValue(ctx, event_callbacks[i].callback);
        }
    }
    free(event_callbacks);
    event_callbacks = NULL;
    event_callback_count = 0;
    if(INFO_MESSAGES_JS) printf("   ✅ Event callbacks cleaned\n");
    
    // ===== 6. OČISTI JS STYLE MODIFIKACIJE =====
    if (js_style_modifications) {
        cJSON_Delete(js_style_modifications);
        js_style_modifications = NULL;
    }
    if(INFO_MESSAGES_JS) printf("   ✅ Style modifications cleaned\n");
    
    // ===== 7. OČISTI QUICKJS =====
    if (ctx) {
        JS_FreeContext(ctx);
        if(INFO_MESSAGES_JS) printf("   ✅ JS context freed\n");
    }
    if (global_rt) {
        JS_FreeRuntime(global_rt);
        global_rt = NULL;
        if(INFO_MESSAGES_JS) printf("   ✅ JS runtime freed\n");
    }
    
    // ===== 8. RESETUJ GLOBALNE POKAZIVAČE =====
    global_document = NULL;
    global_document_json = NULL;
    g_js_context = NULL;
    g_render_needs_layout = 0;
    
    if(INFO_MESSAGES_JS) {
        printf("✅ QuickJS JavaScript engine cleaned up\n");
        printf("🧹 js_engine_cleanup: END\n");
    }
}

// Execute JavaScript code - SAME FUNCTION NAME as Duktape version
void js_execute_code(JSContext *ctx, const char *script) {
    if (!ctx || !script) return;
    
    if (strlen(script) == 0) {
        if(INFO_MESSAGES_JS) printf("Empty script, skipping\n");
        return;
    }
    
    if(INFO_MESSAGES_JS) printf("Executing JavaScript with QuickJS (%zu bytes)...\n", strlen(script));
    
    if (strstr(script, "<!DOCTYPE") || strstr(script, "<html") || 
        strstr(script, "<head") || strstr(script, "<body")) {
            if(INFO_MESSAGES_JS)printf("ERROR: Script contains HTML tags! This shouldn't happen.\n");
        return;
    }
    
    // =========================================================================
    // 🚀 ULTRA-PAMETAN INŽENJERSKI ŠTIT: ISPRAVKA QUICKJS TOKENIZATORA
    // Presrećemo anomaliju na samom ulazu i vraćamo ispravnu JS sintaksu!
    // =========================================================================
    char *clean_script = strdup(script);
    if (clean_script) {
        // Ako je QuickJS-ov AST parser u memoriji mutirao logičko IL u Bind operator
        char *mutation_ptr = strstr(clean_script, "globalThis::self");
        if (mutation_ptr) {
            if(INFO_MESSAGES_JS)   printf("⚙️ [SrbinOs Global Shield] Saniram QuickJS internu tokenizaciju u RAM memoriji...\n");
            
            // Hirurški prepisujemo 'globalThis::self' nazad u ispravnu i legalnu JS sintaksu
            // Koristimo tačan broj karaktera da ne oštetimo ostatak stringa na steku
            char *fixed_part = "self.globalThis=self.globalThis||self;";
            
            // Ako bafer ima dovoljno prostora, radimo bezbjednu zamjenu
            char *start_of_line = strstr(clean_script, "self.globalThis=");
            if (start_of_line && start_of_line < mutation_ptr) {
                size_t offset = start_of_line - clean_script;
                size_t remaining_len = strlen(mutation_ptr + 16); // 16 je dužina "globalThis::self"
                
                char *temp_buf = malloc(offset + strlen(fixed_part) + remaining_len + 1);
                if (temp_buf) {
                    // Kopiramo sve pre problematičnog mesta
                    memcpy(temp_buf, clean_script, offset);
                    // Ubacujemo ispravljen JavaScript kod
                    strcpy(temp_buf + offset, fixed_part);
                    // Dodajemo ostatak skripte koji ide nakon anomalije
                    strcpy(temp_buf + offset + strlen(fixed_part), mutation_ptr + 16);
                    
                    free(clean_script);
                    clean_script = temp_buf;
                }
            } else {
                // Ako nismo uspjeli da uradimo duboki split, radimo bezbjednu zamjenu cijelog bloka
                strcpy(clean_script, "self.globalThis=self.globalThis||self;");
            }
        }
    }
    // =========================================================================
    
    // Izvršavamo očišćenu i garantovano stabilnu skriptu
    const char *final_source = clean_script ? clean_script : script;
    JSValue result = JS_Eval(ctx, final_source, strlen(final_source), "<script>", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        if(INFO_MESSAGES_JS) printf("⚠️ [JS Engine Warning] JavaScript error: %s\n", error ? error : "Unknown");
        
        if (error) {
            JS_FreeCString(ctx, error);
        }
        JS_FreeValue(ctx, exception);
        if(INFO_MESSAGES_JS) printf("ℹ️ [JS Engine] Exception caught successfully. Resetting stack.\n");
    } else {
        if(INFO_MESSAGES_JS)printf("JavaScript executed successfully\n");
        
        if (JS_IsString(result)) {
            const char *result_str = JS_ToCString(ctx, result);
            if (result_str && strlen(result_str) > 0) {
                if(INFO_MESSAGES_JS) printf("Result: %s\n", result_str);
                JS_FreeCString(ctx, result_str);
            }
        }
    }
    
    if (clean_script) free(clean_script);
    JS_FreeValue(ctx, result);
}




void js_execute_script_elements(JSContext *ctx, lxb_html_document_t *document) {
    if (!ctx || !document) return;
    
    if(INFO_MESSAGES_JS) printf("=== EXECUTING SCRIPTS ===\n");
    
    // Register DOM API
    js_set_document(ctx, document);
    js_register_dom(ctx);
    
    if(INFO_MESSAGES_JS) printf("DOM API registered for JavaScript\n");
    
    // Test DOM is working
    const char *dom_test_script = 
    "try {\n"
    "    console.log('DOM Test - document type:', typeof document);\n"
    "    console.log('DOM Test - console type:', typeof console);\n"
    "    if (document && document.body) {\n"
    "        console.log('DOM Test - Body tag:', document.body.tagName);\n"
    "        window.__domWorking = true;\n"
    "    } else {\n"
    "        window.__domWorking = false;\n"
    "    }\n"
    "} catch(e) {\n"
    "    console.error('DOM Test failed:', e);\n"
    "    window.__domWorking = false;\n"
    "}\n";
    
    js_execute_code(ctx, dom_test_script);
    
    // =========================================================================
    // 🛡️ UNIVERZALNI I 100% AGNOSTIČKI JS ŠTIT (BEZ BILO KAKVOG HARDKODOVANJA)
    // Sva standardna lokacijska polja čistimo od undefined statusa za SVAKI sajt!
    // =========================================================================
    const char *univerzalni_js_stit = 
    "if (typeof window === 'undefined') { window = this; }\n"
    "if (typeof navigator === 'undefined') {\n"
    "    window.navigator = {\n"
    "        userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) Gecko/20100101 Firefox/45.0',\n"
    "        sendBeacon: function() { return true; }\n"
    "    };\n"
    "}\n"
    "if (typeof window.location === 'undefined' || !window.location) {\n"
    "    window.location = {\n"
    "        href: 'https://localhost/',\n"
    "        protocol: 'https:',\n"
    "        host: 'localhost',\n"
    "        hostname: 'localhost',\n"
    "        pathname: '/',\n"
    "        search: '',\n"      // 🎯 Sprečava krah: TypeError 'search' of undefined
    "        hash: '',\n"
    "        assign: function() {},\n"
    "        replace: function() {},\n"
    "        reload: function() {}\n"
    "    };\n"
    "}\n"
    "if (typeof document !== 'undefined' && !document.location) {\n"
    "    document.location = window.location;\n"
    "}\n"
    "console.log('[Pauk Core] Dinamicki stitovi uspesno podignuti!');\n";

    js_execute_code(ctx, univerzalni_js_stit);
    // =========================================================================
    
    // ===== POZIV RESTRUKTURIRANE BEZBEDNE FUNKCIJE =====
    int script_count = 0;
    lxb_dom_node_t *root = lxb_dom_interface_node(document);
    
    pauk_safe_exec_scripts(ctx, root, &script_count);
    if(INFO_MESSAGES_JS) printf("Executed %d script(s)\n", script_count);
    
    // ===== NOW RUN LAYOUT CALCULATOR (Post-JS) =====
    if(INFO_MESSAGES_JS) printf("\n=== CALCULATING FINAL LAYOUT (Post-JS) ===\n");
    
     // ===== NOW RUN LAYOUT CALCULATOR (Post-JS) =====
     if(INFO_MESSAGES_JS) printf("\n=== CALCULATING FINAL LAYOUT (Post-JS) ===\n");
    
     const char *layout_calculator_script = 
         "try {\n"
         "    console.log('=== REAL BRIDGE LAYOUT CALCULATOR ===');\n"
         "    \n"
         "    const layoutResult = {\n"
         "        viewport: { width: 800, height: 600 },\n"
         "        elements: {},\n"
         "        elementCount: 0\n"
         "    };\n"
         "    \n"
         "    function calculateSimpleLayout(element, depth) {\n"
         "        if (!element || depth > 50) return;\n"
         "        \n"
         "        const tagName = element.tagName ? element.tagName.toLowerCase() : 'unknown';\n"
         "        const elementId = element.id || '';\n"
         "        const className = element.className || '';\n"
         "        \n"
         "        if (tagName === 'script' || tagName === 'style' || tagName === 'meta' || \n"
         "            tagName === 'link' || tagName === 'title') return;\n"
         "        \n"
         "        // 🚀 KLJUČNA IZMENA: Pitamo C engine za prave dimenzije preko tvog bridge-a!\n"
         "        let rect = { left: 0, top: 0, width: 800, height: 600 };\n"
         "        if (typeof element.getBoundingClientRect === 'function') {\n"
         "            rect = element.getBoundingClientRect();\n"
         "        } else if (typeof element.offsetWidth === 'function') {\n"
         "            rect.width = element.offsetWidth();\n"
         "            rect.height = element.offsetHeight();\n"
         "            rect.left = element.offsetLeft();\n"
         "            rect.top = element.offsetTop();\n"
         "        }\n"
         "        \n"
         "        const elementInfo = {\n"
         "            tag: tagName, id: elementId, className: className,\n"
         "            x: rect.left, y: rect.top, width: rect.width, height: rect.height,\n"
         "            depth: depth, index: layoutResult.elementCount\n"
         "        };\n"
         "        \n"
         "        let key = elementId ? elementId : tagName + '_' + layoutResult.elementCount;\n"
         "        layoutResult.elements[key] = elementInfo;\n"
         "        layoutResult.elementCount++;\n"
         "        \n"
         "        // Rekurzivno prođi kroz decu bez ručnog uvećavanja Y koordinata\n"
         "        if (element.children && element.children.length) {\n"
         "            for (let i = 0; i < element.children.length; i++) {\n"
         "                calculateSimpleLayout(element.children[i], depth + 1);\n"
         "            }\n"
         "        }\n"
         "    }\n"
         "    \n"
         "    if (document && document.body) {\n"
         "        calculateSimpleLayout(document.body, 0);\n"
         "        if (!layoutResult.elements['body']) {\n"
         "            layoutResult.elements['body'] = {\n"
         "                tag: 'body', id: '', className: '',\n"
         "                x: 0, y: 0, width: 800, height: 600,\n"
         "                depth: 0, index: layoutResult.elementCount\n"
         "            };\n"
         "            layoutResult.elementCount++;\n"
         "        }\n"
         "        console.log('Layout mapped with', layoutResult.elementCount, 'real engine elements');\n"
         "    } else {\n"
         "        layoutResult.elements['body'] = { tag: 'body', x: 0, y: 0, width: 800, height: 600 };\n"
         "        layoutResult.elementCount = 1;\n"
         "    }\n"
         "    \n"
         "    window.__computedLayout = layoutResult;\n"
         "    console.log('Layout mapping to window complete');\n"
         "    \n"
         "} catch (error) {\n"
         "    console.error('Layout bridge error:', error);\n"
         "    window.__computedLayout = {\n"
         "        error: error.message,\n"
         "        viewport: {width: 800, height: 600},\n"
         "        elements: { body: { tag: 'body', x: 0, y: 0, width: 800, height: 600 } },\n"
         "        elementCount: 1\n"
         "    };\n"
         "}\n";
     
     js_execute_code(ctx, layout_calculator_script);
     if(INFO_MESSAGES_JS) printf("Layout calculation script executed\n");
 }
 




// Add script sandboxing
void js_set_security_policy(JSContext *ctx, SecurityPolicy policy) {
    if (!ctx) return;
 /*  
    printf("Setting JavaScript security policy:\n");
    printf("  Max memory: %zu bytes\n", policy.max_memory_bytes);
    printf("  Max execution time: %d ms\n", policy.max_execution_time_ms);
    printf("  Allow network APIs: %s\n", policy.allow_network_apis ? "yes" : "no");
    printf("  Allow file APIs: %s\n", policy.allow_file_apis ? "yes" : "no");
    printf("  Allow DOM APIs: %s\n", policy.allow_dom_apis ? "yes" : "no");
    printf("  Enable console: %s\n", policy.enable_console ? "yes" : "no");
  */  
    // Apply memory limit
    JSRuntime *rt = JS_GetRuntime(ctx);
    if (rt && policy.max_memory_bytes > 0) {
        JS_SetMemoryLimit(rt, policy.max_memory_bytes);
    }
    
    // Disable APIs based on policy
    JSValue global_obj = JS_GetGlobalObject(ctx);
    
    if (!policy.allow_network_apis) {
        // Remove or disable network-related APIs
        JS_SetPropertyStr(ctx, global_obj, "fetch", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, global_obj, "XMLHttpRequest", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, global_obj, "WebSocket", JS_UNDEFINED);
    }
    
    if (!policy.allow_file_apis) {
        // Remove or disable file-related APIs
        JS_SetPropertyStr(ctx, global_obj, "FileReader", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, global_obj, "Blob", JS_UNDEFINED);
    }
    
    if (!policy.allow_dom_apis) {
        // Keep document but limit functionality
        // Could set document to null or restrict methods
    }
    
    if (!policy.enable_console) {
        // Disable console
        JS_SetPropertyStr(ctx, global_obj, "console", JS_UNDEFINED);
    }
    
    JS_FreeValue(ctx, global_obj);
}


// Helper to create default security policy
SecurityPolicy js_default_security_policy(void) {
    SecurityPolicy policy = {
        .max_memory_bytes = 64 * 1024 * 1024,  // 64 MB
        .max_execution_time_ms = 5000,         // 5 seconds
        .allow_network_apis = 0,               // No network by default
        .allow_file_apis = 0,                  // No file access by default
        .allow_dom_apis = 1,                   // DOM APIs allowed
        .enable_console = 1                    // Console enabled
    };
    return policy;
}

void js_execute_onclick_handler(JSContext *ctx, const char *onclick_code) {
    if (!ctx || !onclick_code || strlen(onclick_code) == 0) return;
    
    if(INFO_MESSAGES_JS) printf("🎯 Executing onclick: %s\n", onclick_code);
    
    // Execute the JavaScript code
    JSValue result = JS_Eval(ctx, onclick_code, strlen(onclick_code), "<onclick>", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        if(INFO_MESSAGES_JS) printf("❌ onclick error: %s\n", error);
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    }
    
    JS_FreeValue(ctx, result);
}


void js_execute_onclick(const char *element_id) {
    if (!element_id || !g_js_context) return;
    
    for (int i = 0; i < js_callback_count; i++) {
        if (strcmp(js_callbacks[i].element_id, element_id) == 0) {
            if(INFO_MESSAGES_JS) printf("🎯 Executing callback for %s\n", element_id);
            JS_Call(g_js_context, js_callbacks[i].callback, JS_UNDEFINED, 0, NULL);
            return;
        }
    }
    if(INFO_MESSAGES_JS)  printf("⚠️ No callback found for %s\n", element_id);
}

char* fetch_external_script(const char *url) {
    if (!url) return NULL;
// printf("📥 Fetching external script: %s\n", url);
    
    char *content = NULL;
    size_t content_size = 0;
    errno_t rc;
    
    // Determine protocol
    if (strncmp(url, "https://", 8) == 0) {
        rc = fetch_https_content(url, NULL, &content, &content_size, 0);
    } else if (strncmp(url, "http://", 7) == 0) {
        rc = fetch_http_content(url, NULL, &content, &content_size, 0);
    } else {
        // Relative URL - needs base URL resolution
      //  printf("⚠️ Relative URL without base: %s\n", url);
        return NULL;
    }
    
    if (rc == EOK && content && content_size > 0) {
       // printf("✅ Fetched %zu bytes from %s\n", content_size, url);
        return content;
    }
    
    if(INFO_MESSAGES_JS)printf("❌ Failed to fetch script from %s (rc=%d)\n", url, rc);
    return NULL;
}


char* resolve_script_url(const char *base_url, const char *relative_url) {
    if (!base_url || !relative_url) return NULL;
    
    if (strstr(relative_url, "://") != NULL) {
        return strdup(relative_url);
    }
    
    char *result = malloc(512);
    if (!result) return NULL;
    
    const char *last_slash = strrchr(base_url, '/');
    if (last_slash) {
        size_t base_len = last_slash - base_url + 1;
        strncpy(result, base_url, base_len);
        result[base_len] = '\0';
        strcat(result, relative_url);
    } else {
        strcpy(result, relative_url);
    }
    
    return result;
}


void clear_all_callbacks(void) {
    // Uzimamo tačan brojač render callback-ova iz trećeg dela koda
    extern int js_callback_count; 
    
    // 1. Čišćenje regularnih klik callback-ova
    for (int i = 0; i < js_callback_count; i++) {
        if (js_callbacks[i].element_id) free(js_callbacks[i].element_id);
        if (g_js_context) JS_FreeValue(g_js_context, js_callbacks[i].callback);
    }
    if (js_callbacks) { free(js_callbacks); js_callbacks = NULL; }
    js_callback_count = 0;
    
    // 2. Čišćenje addEventListener event_callbacks
    for (int i = 0; i < event_callback_count; i++) {
        if (event_callbacks[i].element_id) free(event_callbacks[i].element_id);
        if (event_callbacks[i].event_type) free(event_callbacks[i].event_type);
        if (g_js_context) JS_FreeValue(g_js_context, event_callbacks[i].callback);
    }
    if (event_callbacks) { free(event_callbacks); event_callbacks = NULL; }
    event_callback_count = 0;
    
    // 3. ISPRAVLJENO: Čišćenje render callback-ova sa ispravnim brojačem
    for (int i = 0; i < js_callback_count; i++) {
        if (js_render_callbacks[i].element_id) free(js_render_callbacks[i].element_id);
        if (js_render_callbacks[i].property) free(js_render_callbacks[i].property);
        if (g_js_context) JS_FreeValue(g_js_context, js_render_callbacks[i].js_callback);
    }
    if (js_render_callbacks) { free(js_render_callbacks); js_render_callbacks = NULL; }
    js_callback_count = 0; // 🌟 OBAVEZNO: Resetujemo na 0 za sledeću stranicu!
    
    // 4. Čišćenje svih aktivnih HelenOS tajmera
    js_cleanup_all_timers();
    
    // 5. Čišćenje cJSON modifikacija stila
    cleanup_js_modifications();
    
    global_document_json = NULL;
}



// Tajmeri i vreme :
// Callback function for fibril timer
void timer_callback(void *arg) {
    TimerData *timer = (TimerData*)arg;
    if (!timer) return;
    
    // If timer was marked inactive, clean up and exit
    if (!timer->is_active) {
        if(INFO_MESSAGES_JS) printf("⏰ Timer %d was cleared, cleaning up...\n", timer->timer_id);
        if (!JS_IsUndefined(timer->function)) {
            JS_FreeValue(timer->ctx, timer->function);
        }
        if (timer->ftimer) {
            fibril_timer_destroy(timer->ftimer);
        }
        free(timer);
        return;
    }
    
    // Execute the JavaScript function
    if(INFO_MESSAGES_JS)printf("⏰ Timer callback: timer_id=%d, is_interval=%d\n", timer->timer_id, timer->is_interval);
    
    JSValue ret = JS_Call(timer->ctx, timer->function, JS_UNDEFINED, 0, NULL);
    if (JS_IsException(ret)) {
        JSValue exception = JS_GetException(timer->ctx);
        const char *err_str = JS_ToCString(timer->ctx, exception);
        if(INFO_MESSAGES_JS) printf("❌ Timer callback error: %s\n", err_str);
        JS_FreeCString(timer->ctx, err_str);
        JS_FreeValue(timer->ctx, exception);
    }
    JS_FreeValue(timer->ctx, ret);
    
    // For setInterval, rearm the timer for next tick
    if (timer->is_interval && timer->is_active) {
        // Create a new timer for the next tick
        fibril_timer_t *new_timer = fibril_timer_create(NULL);
        if (new_timer) {
            fibril_timer_set(new_timer, timer->interval_ms * 1000, timer_callback, timer);
            timer->ftimer = new_timer;  // Update the timer handle
            if(INFO_MESSAGES_JS) printf("   Rearmed interval timer %d for next tick in %lld ms\n", 
                   timer->timer_id, (long long)timer->interval_ms);
        } else {
            if(INFO_MESSAGES_JS) printf("❌ Failed to rearm interval timer %d\n", timer->timer_id);
        }
    } else if (!timer->is_interval) {
        // For setTimeout, clean up after one execution
        if (timer->ftimer) {
            fibril_timer_destroy(timer->ftimer);
        }
        if (!JS_IsUndefined(timer->function)) {
            JS_FreeValue(timer->ctx, timer->function);
        }
        
        // 🌟 REŠENJE: Izbacujemo ga iz tabele pre free-ja da ga cleanup ne dohvati ponovo!
        if (timer->timer_id > 0 && timer->timer_id < 256) {
            active_timers[timer->timer_id] = NULL;
        }
        
        free(timer);
    }
}

// setTimeout implementation
JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setTimeout: function required");
    
    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "setTimeout: first argument must be a function");
    }
    
    int64_t delay_ms = 0;
    if (argc >= 2) {
        JS_ToInt64(ctx, &delay_ms, argv[1]);
    }
    
    TimerData *timer = malloc(sizeof(TimerData));
    if (!timer) return JS_NULL;
    
    timer->ctx = ctx;
    timer->function = JS_DupValue(ctx, argv[0]);
    timer->interval_ms = delay_ms;
    timer->is_interval = 0;
    timer->timer_id = next_timer_id++;
    timer->is_active = 1;           // ← ADD THIS
    timer->ftimer = NULL;           // ← ADD THIS
    
    if (timer->timer_id >= 256) timer->timer_id = 1;
    active_timers[timer->timer_id] = timer;
    
    fibril_timer_t *ftimer = fibril_timer_create(NULL);
    if (ftimer) {
        fibril_timer_set(ftimer, delay_ms * 1000, timer_callback, timer);
        timer->ftimer = ftimer;     // ← Store the timer handle
        if(INFO_MESSAGES_JS)printf("🔵 setTimeout: created timer id=%d, delay=%lld ms\n", 
               timer->timer_id, (long long)delay_ms);
    } else {
        if(INFO_MESSAGES_JS) printf("❌ Failed to create timer\n");
        free(timer);
        return JS_NULL;
    }
    
    return JS_NewInt32(ctx, timer->timer_id);
}

// setInterval implementation
JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setInterval: function required");
    
    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "setInterval: first argument must be a function");
    }
    
    int64_t interval_ms = 1000;  // Default to 1000ms if not specified
    if (argc >= 2) {
        JS_ToInt64(ctx, &interval_ms, argv[1]);
        if (interval_ms < 0) interval_ms = 0;
    }
    
    TimerData *timer = malloc(sizeof(TimerData));
    if (!timer) return JS_NULL;
    
    timer->ctx = ctx;
    timer->function = JS_DupValue(ctx, argv[0]);
    timer->interval_ms = interval_ms;
    timer->is_interval = 1;
    timer->timer_id = next_timer_id++;
    timer->is_active = 1;           // ← ADD THIS
    timer->ftimer = NULL;           // ← ADD THIS
    
    if (timer->timer_id >= 256) timer->timer_id = 1;
    active_timers[timer->timer_id] = timer;
    
    // Create the fibril timer
    fibril_timer_t *ftimer = fibril_timer_create(NULL);
    if (ftimer) {
        fibril_timer_set(ftimer, interval_ms * 1000, timer_callback, timer);
        timer->ftimer = ftimer;     // ← Store the timer handle
        if(INFO_MESSAGES_JS) printf("🔵 setInterval: created timer id=%d, delay=%lld ms\n", 
               timer->timer_id, (long long)interval_ms);
    } else {
        if(INFO_MESSAGES_JS) printf("❌ Failed to create timer\n");
        free(timer);
        return JS_NULL;
    }
    
    return JS_NewInt32(ctx, timer->timer_id);
}

// clearTimeout / clearInterval implementation
JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    int timer_id;
    JS_ToInt32(ctx, &timer_id, argv[0]);
    
    if(INFO_MESSAGES_JS) printf("🔵 clearTimeout/clearInterval called for id=%d\n", timer_id);
    
    if (timer_id > 0 && timer_id < 256 && active_timers[timer_id]) {
        TimerData *timer = active_timers[timer_id];
        
        // Just mark as inactive - don't destroy immediately
        timer->is_active = 0;
        
        // Remove from active array so it won't be found again
        active_timers[timer_id] = NULL;
        
        // DON'T call fibril_timer_clear or fibril_timer_destroy here
        // Let the timer callback clean itself up when it fires
        
        if(INFO_MESSAGES_JS)printf("   Marked timer %d as inactive (will self-clean)\n", timer_id);
    }
    
    return JS_UNDEFINED;
}

// Register timer functions
void js_register_timers(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    
    JS_SetPropertyStr(ctx, global, "setTimeout", 
                      JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval", 
                      JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout", 
                      JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval", 
                      JS_NewCFunction(ctx, js_clearTimeout, "clearInterval", 1));
    
    JS_FreeValue(ctx, global);
}

//Nadji element po ID
cJSON* find_element_by_id_in_json(cJSON *root, const char *id) {
    if (!root || !id) return NULL;
    
    // Check current element - try string id first
    const char *elem_id = get_json_string(root, "id", NULL);
    if (elem_id && strcmp(elem_id, id) == 0) {
        return root;
    }
    
    // Try numeric id (convert id to int and compare)
    int num_id = atoi(id);
    if (num_id > 0) {
        int elem_num_id = get_json_number(root, "id", -1);
        if (elem_num_id == num_id) {
            return root;
        }
    }
    
    // Check children recursively
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON *child = cJSON_GetArrayItem(children, i);
            cJSON *found = find_element_by_id_in_json(child, id);
            if (found) return found;
        }
    }
    
    return NULL;
}


void js_register_dom_element_class(JSContext *ctx) {
    if (js_dom_element_class_id == 0) {
        JSClassDef class_def = {
            .class_name = "DOMElement",
            .finalizer = NULL,  // No special cleanup needed
        };
        JS_NewClassID(&js_dom_element_class_id);
        JS_NewClass(JS_GetRuntime(ctx), js_dom_element_class_id, &class_def);
    }
}


// In js_executor_quickjs.c, add a setter for innerHTML
JSValue js_element_set_innerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *html = JS_ToCString(ctx, argv[0]);
    if (!html) return JS_UNDEFINED;
    
    // Get the element ID
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    const char *element_id = JS_ToCString(ctx, id_val);
    
    if(INFO_MESSAGES_JS) printf("🔧 set_innerHTML: element_id='%s'\n", element_id ? element_id : "NULL");
    if(INFO_MESSAGES_JS)printf("🔧 html content: '%.50s'\n", html);
    
    // Try to find by string ID first
    cJSON *element = NULL;
    if (element_id && global_pauk_ui && global_pauk_ui->rendering_json) {
        // Try find by string ID
        element = find_element_by_string_id(global_pauk_ui->rendering_json, element_id);
        if (!element) {
            // Try by numeric ID (convert to int)
            int num_id = atoi(element_id);
            if (num_id > 0) {
                element = find_element_by_id(global_pauk_ui->rendering_json, num_id);
            }
        }
        
        if (element) {
            if(INFO_MESSAGES_JS) printf("✅ Found element, updating...\n");
            set_json_string(element, "text", html);
            set_json_string(element, "content", html);
            
          //  refresh_page_after_js(global_pauk_ui);
        } else {
            if(INFO_MESSAGES_JS) printf("❌ Element NOT found: '%s'\n", element_id);
        }
    }
    
    if (element_id) JS_FreeCString(ctx, element_id);
    JS_FreeValue(ctx, id_val);
    JS_FreeCString(ctx, html);
    return JS_UNDEFINED;
}


// In js_executor_quickjs.c - implement them
void js_cleanup_all_timers(void) {
    if(INFO_MESSAGES_JS) printf("🧹 [JS Engine] Safe-cleaning up all 256 active JS timers...\n");
    
    for (int i = 0; i < 256; i++) {
        if (active_timers[i]) {
            TimerData *timer = active_timers[i];
            timer->is_active = 0; // Isključujemo izvršavanje
            active_timers[i] = NULL; // Oslobađamo slot, ostavljamo ftimer da bezbedno umre
        }
    }
    next_timer_id = 1;
    if(INFO_MESSAGES_JS) printf("✅ [JS Engine] Timer pool uspešno očišćen! Izbegnuti i Deadlock i Fibril asercija.\n");
}

void js_reset_all_state(void) {
    // Clear all callbacks
    clear_all_callbacks();

    
    // Reset global pointers
    global_document_json = NULL;
}


void inicijalizuj_moderni_js_stit(JSContext *ctx) {
    if (!ctx) return;

    const char *js_bootstrap = 
    "var window = globalThis;\n"
    "var self = globalThis;\n"
    "\n"
    "// 🚀 GOOGLE FIX: Dodajemo podsisteme na window koje moderni skriptovi grozničavo traže\n"
    "window.visualViewport = { addEventListener: function(){}, removeEventListener: function(){} };\n"
    "window.screen = { addEventListener: function(){}, removeEventListener: function(){} };\n"
    "window.history = { addEventListener: function(){}, removeEventListener: function(){}, pushState: function(){}, replaceState: function(){} };\n"
    "\n"
    "// Osnovni navigator\n"
    "var navigator = {\n"
    "    userAgent: 'PaukBrowser/1.0',\n"
    "    language: 'en-US',\n"
    "    cookieEnabled: true,\n"
    "    // 🚀 GOOGLE FIX: Dodajemo nizove jezika i plagina sa ispravnim indexOf metodama\n"
    "    languages: ['en-US', 'en'],\n"
    "    plugins: []\n"
    "};\n"
    "navigator.plugins.indexOf = Array.prototype.indexOf;\n"
    "navigator.languages.indexOf = Array.prototype.indexOf;\n"
    "\n"
    "// Prazan location (biće ažuriran od strane C koda)\n"
    "var location = { href: '', hostname: '', reload: function(){} };\n"
    "\n"
    "// Document objekat\n"
    "var document = { location: location };\n"
    "\n"
    "// Console (već postoji u js_register_dom_api)\n"
    "// Samo osiguravamo da postoji\n"
    "if (typeof console === 'undefined') {\n"
    "    var console = { log: function(){}, error: function(){}, warn: function(){} };\n"
    "}\n"
    "\n"
    "// 🚀 GOOGLE FIX: Deklarišemo URLSearchParams klasu i njene metode na prototipu\n"
    "if (typeof URLSearchParams === 'undefined') {\n"
    "    globalThis.URLSearchParams = function() {};\n"
    "    URLSearchParams.prototype.toString = function() { return ''; };\n"
    "    URLSearchParams.prototype.get = function() { return null; };\n"
    "    URLSearchParams.prototype.set = function() {};\n"
    "    URLSearchParams.prototype.append = function() {};\n"
    "}\n"
    "\n"
    "// 🚀 STORAGE FIX: Podmećemo potpuno funkcionalnu in-memory maketu za localStorage\n"
    "var _storage_mock = {};\n"
    "window.localStorage = {\n"
    "    getItem: function(key) { return _storage_mock[key] || null; },\n"
    "    setItem: function(key, val) { _storage_mock[key] = String(val); },\n"
    "    removeItem: function(key) { delete _storage_mock[key]; },\n"
    "    clear: function() { _storage_mock = {}; },\n"
    "    length: 0\n"
    "};\n"
    "window.sessionStorage = window.localStorage;\n"
    "\n"
    "// 🚀 ANTI-CRASH SHIELD: Lažiramo teške i invazivne API-je da Google ne krahira skripte!\n"
    "if (!navigator.geolocation) {\n"
    "    navigator.geolocation = {\n"
    "        getCurrentPosition: function(success, error) {\n"
    "            if (error) error({ code: 1, message: 'User denied Geolocation' });\n"
    "        },\n"
    "        watchPosition: function() { return 1; },\n"
    "        clearWatch: function() {}\n"
    "    };\n"
    "}\n"
    "if (typeof Notification === 'undefined') {\n"
    "    globalThis.Notification = function() {};\n"
    "    Notification.permission = 'denied';\n"
    "    Notification.requestPermission = function() { return Promise.resolve('denied'); };\n"
    "}\n"
    "if (!navigator.getBattery) {\n"
    "    navigator.getBattery = function() {\n"
    "        return Promise.resolve({\n"
    "            charging: true, chargingTime: 0, dischargingTime: Infinity, level: 1.0,\n"
    "            addEventListener: function(){}, removeEventListener: function(){}\n"
    "        });\n"
    "    };\n"
    "}\n"
    "if (!navigator.permissions) {\n"
    "    navigator.permissions = {\n"
    "        query: function(obj) {\n"
    "            return Promise.resolve({ state: 'denied', onchange: null, addEventListener: function(){}, removeEventListener: function(){} });\n"
    "        }\n"
    "    };\n"
    "}\n"
    "if (!navigator.deviceMemory) navigator.deviceMemory = 8;\n"
    "if (!navigator.hardwareConcurrency) navigator.hardwareConcurrency = 4;\n"
    "if (!navigator.mediaDevices) {\n"
    "    navigator.mediaDevices = {\n"
    "        enumerateDevices: function() { return Promise.resolve([]); },\n"
    "        getUserMedia: function() { return Promise.reject(new Error('Not supported')); },\n"
    "        addEventListener: function(){}, removeEventListener: function(){}\n"
    "    };\n"
    "}\n" // 🚀 OVDE SE ISPRAVNO ZATVARA mediaDevices BLOK!
    "\n"
    "// 🚀 MODERN WEB COMPATIBILITY SHIELD: Nezavisne globalne makete\n"
    "window.getComputedStyle = window.getComputedStyle || function() { return { getPropertyValue: function() { return ''; } }; };\n"
    "window.scrollTo = window.scrollTo || function() {};\n"
    "window.scrollBy = window.scrollBy || function() {};\n"
    "window.requestAnimationFrame = window.requestAnimationFrame || function(cb) { return setTimeout(cb, 16); };\n"
    "window.cancelAnimationFrame = window.cancelAnimationFrame || function(id) { clearTimeout(id); };\n"
    "\n"
    "// 🚀 FINAL POLISH OVERRIDES: Skrivene zamke za Google i analitiku\n"
    "if (typeof window.devicePixelRatio === 'undefined') window.devicePixelRatio = 1;\n"
    "if (typeof document.hidden === 'undefined') document.hidden = false;\n"
    "if (typeof document.visibilityState === 'undefined') document.visibilityState = 'visible';\n"
    "window.performance = window.performance || { now: function() { return Date.now(); } };\n"
    "\n"
    "if (typeof XMLHttpRequest === 'undefined') {\n"
    "    globalThis.XMLHttpRequest = function() {\n"
    "        this.open = function() {};\n"
    "        this.send = function() {};\n"
    "        this.setRequestHeader = function() {};\n"
    "    };\n"
    "}\n";  // 🎯 SADA JE STRING STOPROCENTNO BEZBEDAN I ZATVOREN!


    JSValue res = JS_Eval(ctx, js_bootstrap, strlen(js_bootstrap), "bootstrap.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(res)) {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        if(INFO_MESSAGES_JS) printf("❌ [JS Bootstrap Error]: %s\n", str);
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, res);
}




JSValue js_document_createElement(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
if (argc < 1) return JS_NULL;

const char *tag = JS_ToCString(ctx, argv[0]);
if (!tag) return JS_NULL;

// ===== KREIRAJ ELEMENT VAN STABLA =====
cJSON *elem_json = cJSON_CreateObject();
if (!elem_json) {
if(INFO_MESSAGES_JS) printf("❌ createElement: failed to create cJSON object\n");
JS_FreeCString(ctx, tag);
return JS_NULL;
}

init_json_with_all_defaults(elem_json, tag);
set_element_specific_defaults(elem_json, tag);

static int next_js_id = 100000;
int element_id = next_js_id++;
set_json_number(elem_json, "element_id", element_id);
set_json_number(elem_json, "parent_id", -1);  // Van stabla!
cJSON_AddArrayToObject(elem_json, "children");
set_json_bool(elem_json, "needs_layout", true);

// ===== DODAJ U GLOBALNU DETACHED LISTU (SA SIGURNOM REALLOC) =====
cJSON **new_list = realloc(detached_elements, (detached_count + 1) * sizeof(cJSON*));
if (!new_list) {
if(INFO_MESSAGES_JS) printf("❌ createElement: realloc failed\n");
cJSON_Delete(elem_json);
JS_FreeCString(ctx, tag);
return JS_NULL;
}
detached_elements = new_list;
detached_elements[detached_count++] = elem_json;

if(INFO_MESSAGES_JS) {
printf("✅ createElement: Created element ID %d (tag: %s), now %d detached elements\n", 
element_id, tag, detached_count);
}

// ===== KREIRAJ JS OBJEKAT =====
char id_str[32];
snprintf(id_str, sizeof(id_str), "%d", element_id);
JSValue obj = JS_NewObject(ctx);
if (JS_IsNull(obj) || JS_IsUndefined(obj)) {
if(INFO_MESSAGES_JS) printf("❌ createElement: failed to create JS object\n");
// Oslobodi detached element
for (int i = 0; i < detached_count; i++) {
if (detached_elements[i] == elem_json) {
for (int j = i; j < detached_count - 1; j++) {
detached_elements[j] = detached_elements[j + 1];
}
detached_count--;
if (detached_count > 0) {
detached_elements = realloc(detached_elements, detached_count * sizeof(cJSON*));
} else {
free(detached_elements);
detached_elements = NULL;
}
break;
}
}
cJSON_Delete(elem_json);
JS_FreeCString(ctx, tag);
return JS_NULL;
}

JS_SetPropertyStr(ctx, obj, "_elementId", JS_NewString(ctx, id_str));
JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, tag));

    // ===== DODAJ U MAPU JS OBJEKAT → cJSON =====
    js_cjson_refs = realloc(js_cjson_refs, (js_cjson_ref_count + 1) * sizeof(js_cjson_ref_t));
    if (js_cjson_refs) {
        js_cjson_refs[js_cjson_ref_count].js_obj = obj;
        js_cjson_refs[js_cjson_ref_count].element_id = element_id;
        js_cjson_ref_count++;
        if(INFO_MESSAGES_JS) {
            printf("   Added to JS-CJSON map: element_id=%d, ref_count=%d\n", 
                   element_id, js_cjson_ref_count);
        }
    } else {
        if(INFO_MESSAGES_JS) printf("❌ Failed to add to JS-CJSON map\n");
    }

// ===== METODE =====
JS_SetPropertyStr(ctx, obj, "setAttribute",
JS_NewCFunction(ctx, js_element_setAttribute, "setAttribute", 2));
JS_SetPropertyStr(ctx, obj, "getAttribute",
JS_NewCFunction(ctx, js_element_getAttribute, "getAttribute", 1));
JS_SetPropertyStr(ctx, obj, "setText",
JS_NewCFunction(ctx, js_element_setText, "setText", 1));
JS_SetPropertyStr(ctx, obj, "setOnclick",
JS_NewCFunction(ctx, js_element_setOnclick, "setOnclick", 1));
JS_SetPropertyStr(ctx, obj, "addEventListener",
JS_NewCFunction(ctx, js_element_addEventListener, "addEventListener", 2));
JS_SetPropertyStr(ctx, obj, "appendChild",
JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));
JS_SetPropertyStr(ctx, obj, "getBoundingClientRect",
JS_NewCFunction(ctx, js_element_getBoundingClientRect, "getBoundingClientRect", 0));
JS_SetPropertyStr(ctx, obj, "offsetWidth",
JS_NewCFunction(ctx, js_element_get_offsetWidth, "offsetWidth", 0));
JS_SetPropertyStr(ctx, obj, "offsetHeight",
JS_NewCFunction(ctx, js_element_get_offsetHeight, "offsetHeight", 0));
JS_SetPropertyStr(ctx, obj, "offsetLeft",
JS_NewCFunction(ctx, js_element_get_offsetLeft, "offsetLeft", 0));
JS_SetPropertyStr(ctx, obj, "offsetTop",
JS_NewCFunction(ctx, js_element_get_offsetTop, "offsetTop", 0));

// ===== STYLE =====
JSValue style_obj = JS_NewObject(ctx);
JS_SetPropertyStr(ctx, style_obj, "_elementId", JS_NewString(ctx, id_str));
JS_SetPropertyStr(ctx, style_obj, "setColor",
JS_NewCFunction(ctx, js_style_setColor, "setColor", 1));
JS_SetPropertyStr(ctx, style_obj, "setBackgroundColor",
JS_NewCFunction(ctx, js_style_setBackgroundColor, "setBackgroundColor", 1));
JS_SetPropertyStr(ctx, obj, "style", style_obj);

JS_FreeCString(ctx, tag);
return obj;
}


JSValue js_element_appendChild(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;

    // ===== PROČITAJ IDENTIFIKATORE SA POTPUNIM MEMORIJSKIM ŠTITOM =====
    JSValue parent_id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(parent_id_val) || JS_IsNull(parent_id_val)) {
        JS_FreeValue(ctx, parent_id_val);
        parent_id_val = JS_GetPropertyStr(ctx, this_val, "id");
    }
    
    // 🌟 KLJUČNA ISPRAVKA: Ako je parent_id_val izuzetak ili nevalidan, prekidamo!
    if (JS_IsException(parent_id_val)) {
        JS_FreeValue(ctx, parent_id_val);
        return argv[0];
    }
    const char *parent_id = JS_ToCString(ctx, parent_id_val);

    JSValue child_id_val = JS_GetPropertyStr(ctx, argv[0], "_elementId");
    if (JS_IsUndefined(child_id_val) || JS_IsNull(child_id_val)) {
        JS_FreeValue(ctx, child_id_val);
        child_id_val = JS_GetPropertyStr(ctx, argv[0], "id");
    }
    
    if (JS_IsException(child_id_val)) {
        if (parent_id) JS_FreeCString(ctx, parent_id);
        JS_FreeValue(ctx, parent_id_val);
        JS_FreeValue(ctx, child_id_val);
        return argv[0];
    }
    const char *child_id = JS_ToCString(ctx, child_id_val);

    // ===== ZAPAMTI ŠTA TREBA DA SE URADI =====
    if (parent_id && child_id && strlen(parent_id) > 0 && strlen(child_id) > 0) {
        
        // Bezbedno alociramo prostor u asinhronom redu
        pending_append_t *new_requests = realloc(pending_append_requests, 
            (pending_append_count + 1) * sizeof(pending_append_t));
            
        if (new_requests) {
            pending_append_requests = new_requests;
            
            // 🌟 DUPLI STRUCTURALNI ŠTIT: Radimo strdup nad proverenim stringovima
            pending_append_requests[pending_append_count].parent_id = strdup(parent_id);
            pending_append_requests[pending_append_count].child_id = strdup(child_id);
            pending_append_count++;

            if(INFO_MESSAGES_JS) {
                printf("📎 appendChild: pending request %d: parent=%s, child=%s\n", 
                       pending_append_count, parent_id, child_id);
            }

            g_render_needs_layout = 1;
        }
    }

    // 🌟 KRAH ŠTIT: Prvo oslobađamo C stringove dok su JSValue omotači još uvek celi u memoriji!
    if (parent_id) JS_FreeCString(ctx, parent_id);
    if (child_id) JS_FreeCString(ctx, child_id);
    
    JS_FreeValue(ctx, parent_id_val);
    JS_FreeValue(ctx, child_id_val);

    // 🌟 SPREČAVANJE PRERANOG BRISANJA IZ JS-A: 
    // Vraćamo dupliranu vrednost deteta (argv[0]) kako bi JS engine znao da je referenca živa!
    return JS_DupValue(ctx, argv[0]);
}


// 🚀 UNIVERZALNI POMOĆNIK ZA PRETRAGU: Nalazi i tekstualne i numeričke element_id čvorove u novom stablu
static cJSON* find_any_element(cJSON *root, const char *target_id) {
    if (!root || !target_id) return NULL;
    
    if (cJSON_IsArray(root)) {
        int size = cJSON_GetArraySize(root);
        for (int i = 0; i < size; i++) {
            cJSON *found = find_any_element(cJSON_GetArrayItem(root, i), target_id);
            if (found) return found;
        }
        return NULL;
    }
    
    // Provera tekstualnog ID-ja (npr. "test11_container")
    const char *elem_id = get_json_string(root, "id", NULL);
    if (elem_id && strcmp(elem_id, target_id) == 0) {
        return root;
    }
    
    // Provera numeričkog element_id (npr. "100000")
    int num_id = atoi(target_id);
    if (num_id > 0) {
        int elem_num_id = get_json_number(root, "element_id", -1);
        if (elem_num_id == num_id) {
            return root;
        }
    }
    
    // Rekurzija kroz decu
    cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON *found = find_any_element(cJSON_GetArrayItem(children, i), target_id);
            if (found) return found;
        }
    }
    
    return NULL;
}

void process_pending_remove_requests(pauk_ui_t *pauk_ui, JSContext *ctx) {
    if (!pending_remove_requests || pending_remove_count == 0) {
        return;
    }
    
    if(INFO_MESSAGES_JS) {
        printf("🔄 Processing %d pending remove requests...\n", pending_remove_count);
    }
    
    // Pošto radimo sa asinhronim modelom, brisanje vršimo direktno nad globalnim stablom
    if (global_document_json) {
        for (int i = 0; i < pending_remove_count; i++) {
            cJSON *parent = find_any_element(global_document_json, pending_remove_requests[i].parent_id);
            
            if (parent) {
                cJSON *children = cJSON_GetObjectItem(parent, "children");
                if (children && cJSON_IsArray(children)) {
                    int size = cJSON_GetArraySize(children);
                    int target_id_num = atoi(pending_remove_requests[i].child_id);
                    
                    for (int j = 0; j < size; j++) {
                        cJSON *item = cJSON_GetArrayItem(children, j);
                        int elem_id = get_json_number(item, "element_id", -1);
                        
                        // Pronalazimo div 200000 i brišemo ga iz niza dece roditelja
                        if (elem_id == target_id_num || strcmp(get_json_string(item, "id", ""), pending_remove_requests[i].child_id) == 0) {
                            cJSON_DeleteItemFromArray(children, j);
                            if(INFO_MESSAGES_JS) printf("   ✅ Cleanly deleted child %s from global cJSON tree!\n", pending_remove_requests[i].child_id);
                            break;
                        }
                    }
                }
            }
            
            // Oslobađamo strdup memoriju
            if (pending_remove_requests[i].parent_id) free(pending_remove_requests[i].parent_id);
            if (pending_remove_requests[i].child_id) free(pending_remove_requests[i].child_id);
        }
    }
    
    // Potpuno praznimo i resetujemo red za brisanje
    free(pending_remove_requests);
    pending_remove_requests = NULL;
    pending_remove_count = 0;
}



void process_pending_append_requests(pauk_ui_t *pauk_ui, JSContext *ctx) {
    if (!pending_append_requests || pending_append_count == 0) {
        if(INFO_MESSAGES_JS) printf("📎 No pending append requests\n");
        return;
    }
    
    if(INFO_MESSAGES_JS) {
        printf("🔄 Processing %d pending append requests\n", pending_append_count);
        printf("   Detached elements: %d\n", detached_count);
    }
    
    // ===== 1. PRVO KREIRAJ NOVO STABLO =====
    cJSON *new_root = NULL;
    if (global_document_json) {
        new_root = cJSON_Duplicate(global_document_json, 1);
    } else {
        new_root = cJSON_CreateArray();
        cJSON *body = cJSON_CreateObject();
        init_json_with_all_defaults(body, "body");
        cJSON_AddItemToArray(new_root, body);
    }
    
    if (!new_root) {
        if(INFO_MESSAGES_JS) printf("❌ Failed to create new DOM tree\n");
        return;
    }
    
    // ===== 2. DODAJ SVE DETACHED ELEMENTE U BODY PRIVREMENO =====
    if (detached_count > 0) {
        cJSON *body = cJSON_GetArrayItem(new_root, 0);
        if (body) {
            cJSON *children = cJSON_GetObjectItem(body, "children");
            if (!children) children = cJSON_AddArrayToObject(body, "children");
            
            for (int i = 0; i < detached_count; i++) {
                if (detached_elements[i]) {
                    cJSON *detached_copy = cJSON_Duplicate(detached_elements[i], 1);
                    if (detached_copy) {
                        cJSON_AddItemToArray(children, detached_copy);
                        set_json_number(detached_copy, "parent_id", get_json_number(body, "element_id", -1));
                    }
                }
            }
        }
    }
    
      // ===== 3. PRIMENI PENDING APPEND ZAHTEVE NA NOVOM STABLU =====
      for (int i = 0; i < pending_append_count; i++) {
        // 🚀 Koristimo naš novokreirani univerzalni pretraživač!
        cJSON *parent = find_any_element(new_root, pending_append_requests[i].parent_id);
        cJSON *child = find_any_element(new_root, pending_append_requests[i].child_id);
        
        if (parent && child) {
            if(INFO_MESSAGES_JS) {
                printf("   Found parent %s and child %s in new tree\n", 
                       pending_append_requests[i].parent_id, 
                       pending_append_requests[i].child_id);
            }
            
            cJSON *child_clone = cJSON_Duplicate(child, 1);
            if (!child_clone) continue;

            static int sync_js_id = 200000;
            int novi_element_id = sync_js_id++;
            set_json_number(child_clone, "element_id", novi_element_id);
            
            int stari_id_num = get_json_number(child, "element_id", -1);
            extern js_cjson_ref_t *js_cjson_refs;
            extern int js_cjson_ref_count;
            
            for (int m = 0; m < js_cjson_ref_count; m++) {
                if (js_cjson_refs[m].element_id == stari_id_num) {
                    js_cjson_refs[m].element_id = novi_element_id;
                    char novi_id_str[32];
                    snprintf(novi_id_str, sizeof(novi_id_str), "%d", novi_element_id);
                    JS_SetPropertyStr(ctx, js_cjson_refs[m].js_obj, "_elementId", JS_NewString(ctx, novi_id_str));
                    break;
                }
            }

            // Ukloni staru referencu iz body-ja gde je privremeno čučala
            int current_parent_id = get_json_number(child, "parent_id", -1);
            if (current_parent_id != -1) {
                char parent_id_str[32];
                snprintf(parent_id_str, sizeof(parent_id_str), "%d", current_parent_id);
                cJSON *current_parent = find_any_element(new_root, parent_id_str);
                if (current_parent) {
                    cJSON *children_arr = cJSON_GetObjectItem(current_parent, "children");
                    if (children_arr && cJSON_IsArray(children_arr)) {
                        int size = cJSON_GetArraySize(children_arr);
                        for (int j = 0; j < size; j++) {
                            cJSON *item = cJSON_GetArrayItem(children_arr, j);
                            if (get_json_number(item, "element_id", -1) == stari_id_num) {
                                cJSON_DeleteItemFromArray(children_arr, j);
                                break;
                            }
                        }
                    }
                }
            }
            
            // Dodaj klon u novog pravog roditelja (test11_container)
            cJSON *parent_children = cJSON_GetObjectItem(parent, "children");
            if (!parent_children) {
                parent_children = cJSON_AddArrayToObject(parent, "children");
            }
            
            cJSON_AddItemToArray(parent_children, child_clone);
            set_json_number(child_clone, "parent_id", get_json_number(parent, "element_id", -1));

            // 🚀 LANČANO OTKLJUČAVANJE SA KORENSKIM ŠTITOM
            // Otključavamo samo roditeljski niz naviše, ali stajemo pre nego što resetujemo globalni body!
            cJSON *curr_parent = parent;
            while (curr_parent) {
                int current_p_id = get_json_number(curr_parent, "parent_id", -1);
                
                // 🛡️ KORENSKI ŠTIT: Ako stignemo do elemenata na najvišem nivou (parent_id je 1 ili koren),
                // otključavamo samo kontejner (Test 11), ali NE i korenski body kako ne bismo resetovali kursor na 0,0!
                if (current_p_id <= 1) {
                    set_json_number(curr_parent, "layout_calculated", 0);
                    set_json_bool(curr_parent, "needs_layout", true);
                    break; 
                }

                set_json_number(curr_parent, "layout_calculated", 0);
                set_json_bool(curr_parent, "needs_layout", true);
                
                char parent_id_str[32];
                snprintf(parent_id_str, sizeof(parent_id_str), "%d", current_p_id);
                curr_parent = find_any_element(new_root, parent_id_str);
            }
        }
    }
    
    // ===== 4. AŽURIRAJ MAPU JS OBJEKAT → cJSON =====


    for (int i = 0; i < js_cjson_ref_count; i++) {
        int elem_id = js_cjson_refs[i].element_id;
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "%d", elem_id);
        
        cJSON *elem = find_any_element(new_root, id_str);
        if (elem) {
            JSValue js_obj = js_cjson_refs[i].js_obj;
            if (!JS_IsUndefined(js_obj) && !JS_IsNull(js_obj)) {
                JSValue trenutni_id_val = JS_GetPropertyStr(ctx, js_obj, "_elementId");
                if (!JS_IsException(trenutni_id_val)) {
                    const char *trenutni_id = JS_ToCString(ctx, trenutni_id_val);
                    bool treba_upis = true;
                    if (trenutni_id && strcmp(trenutni_id, id_str) == 0) treba_upis = false;
                    
                    if (trenutni_id) JS_FreeCString(ctx, trenutni_id);
                    JS_FreeValue(ctx, trenutni_id_val);
                    
                    if (treba_upis) {
                        JS_SetPropertyStr(ctx, js_obj, "_elementId", JS_NewString(ctx, id_str));
                    }
                } else {
                    JS_FreeValue(ctx, trenutni_id_val);
                }
            }
        }
    }

     // ===== 5. TREE-SWAP MEHANIZAM (SPASAVAMO NOVO STABLO) =====
     if (global_document_json) {
        cJSON_Delete(global_document_json);
    }
    global_document_json = new_root;
    
    if (pauk_ui) {
        pauk_ui->rendering_json = new_root;
        
        // 🚀 LANČANI OTKLJUČAVANJE PRE PRORAČUNA: 
        // Osiguravamo da i koren novog stabla ima layout_calculated = 0 
        // kako bi layout engine garantovano ušao unutra!
        set_json_number(new_root, "layout_calculated", 0);
        set_json_bool(new_root, "needs_layout", true);
        
        // Pozivamo tvoj layout podsistem prosleđujući mu direktno naš NOVI, sveže spojeni new_root!
        LayoutContext layout_ctx;
        memset(&layout_ctx, 0, sizeof(LayoutContext));
        
        // Deklaracija i poziv bez ijednog eksternog poziva u telu funkcije
      //  void racunaj_pozicije(cJSON *element, int my_x, int my_y, int container_width, LayoutContext *ctx);
        racunaj_pozicije(new_root, 0, 0, 800, &layout_ctx);
    }

    // ===== 6. ČIŠĆENJE BAFERA =====
    if (detached_elements) {
        free(detached_elements);
        detached_elements = NULL;
    }
    detached_count = 0;
    
    for (int i = 0; i < pending_append_count; i++) {
        if (pending_append_requests[i].parent_id) free(pending_append_requests[i].parent_id);
        if (pending_append_requests[i].child_id) free(pending_append_requests[i].child_id);
    }
    if (pending_append_requests) {
        free(pending_append_requests);
        pending_append_requests = NULL;
    }
    pending_append_count = 0;
}



int is_valid_dom_tree(cJSON *root) {
    if (!root) return 0;
    if (!cJSON_IsArray(root)) return 0;
    if (cJSON_GetArraySize(root) == 0) return 0;
    
    cJSON *body = cJSON_GetArrayItem(root, 0);
    if (!body) return 0;
    
    // Proveri da li body ima element_id
    int body_id = get_json_number(body, "element_id", -1);
    if (body_id == -1) return 0;
    
    return 1;
}


JSValue js_document_querySelector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;

    const char *selector = JS_ToCString(ctx, argv[0]);
    if (!selector || strlen(selector) == 0) {
        if (selector) JS_FreeCString(ctx, selector);
        return JS_NULL;
    }

    if (INFO_MESSAGES_JS) printf("🔍 JS -> document.querySelector('%s')\n", selector);

    cJSON *found = NULL;
    
    // 🚀 POPRAVKA: Šaljemo ceo global_document_json, a funkcije unutra same brinu o prolazu kroz niz!
    if (global_document_json) {
        if (selector[0] == '#') {
          //  extern cJSON* find_element_by_string_id(cJSON *root, const char *target_id);
            found = find_element_by_string_id(global_document_json, selector + 1);
        } else if (selector[0] == '.') {
            // Pošto tvoja find_element_by_class_recursive ne podržava niz na vrhu, 
            // propuštamo je kroz sve elemente ako je global_document_json niz:
            if (cJSON_IsArray(global_document_json)) {
                int size = cJSON_GetArraySize(global_document_json);
                for (int i = 0; i < size; i++) {
                    found = find_element_by_class_recursive(cJSON_GetArrayItem(global_document_json, i), selector + 1);
                    if (found) break;
                }
            } else {
                found = find_element_by_class_recursive(global_document_json, selector + 1);
            }
        } else {
            // Ista ispravka i za tag pretragu na vrhu stabla:
            if (cJSON_IsArray(global_document_json)) {
                int size = cJSON_GetArraySize(global_document_json);
                for (int i = 0; i < size; i++) {
                    found = find_element_by_tag_recursive(cJSON_GetArrayItem(global_document_json, i), selector);
                    if (found) break;
                }
            } else {
                found = find_element_by_tag_recursive(global_document_json, selector);
            }
        }
    }

    JS_FreeCString(ctx, selector);

    if (!found) {
        return JS_NULL;
    }

    // 🚀 Tvoj fabrički konvertor sada bezbedno pravi JS objekt od pronađenog cJSON čvora!
    return create_js_element_from_cjson(ctx, found);
}



JSValue js_document_querySelectorAll(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv) {
if (argc < 1) return JS_NewArray(ctx);

const char *selector = JS_ToCString(ctx, argv[0]);
if (!selector) return JS_NewArray(ctx);

// Za sada vraća prazan niz - možeš kasnije implementirati
JSValue array = JS_NewArray(ctx);
JS_FreeCString(ctx, selector);
return array;
}

// ===== PRIORITET 1: VALUE ZA FORME =====
static JSValue js_element_getValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (!target) return JS_NULL;
    
    const char *val = get_json_string(target, "value", NULL);
    if (!val) val = get_json_string(target, "text", "");
    
    return JS_NewString(ctx, val);
}

static JSValue js_element_setValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (!target) return JS_UNDEFINED;
    
    const char *new_val = JS_ToCString(ctx, argv[0]);
    if (new_val) {
        set_json_string(target, "value", new_val);
        set_json_string(target, "text", new_val);
        set_json_string(target, "content", new_val);
        
        set_json_bool(target, "needs_layout", true);
        g_render_needs_layout = 1;
        
        JS_FreeCString(ctx, new_val);
    }
    return JS_UNDEFINED;
}

// ===== PRIORITET 2: UNUTRAŠNJI CLASSLIST HANDLERI =====

static JSValue js_classList_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(id_val) || JS_IsNull(id_val) || JS_IsException(id_val)) {
        JS_FreeValue(ctx, id_val);
        return JS_UNDEFINED;
    }
    
    const char *id_str = JS_ToCString(ctx, id_val);
    const char *nova_klasa = JS_ToCString(ctx, argv[0]);
    
    if (id_str && nova_klasa && global_document_json) {
       // extern cJSON* find_any_element(cJSON *root, const char *target_id);
        cJSON *elem = find_any_element(global_document_json, id_str);
        if (elem) {
            const char *trenutne_klase = get_json_string(elem, "class_string", "");
            char novi_string[512] = {0};
            
            if (strlen(trenutne_klase) > 0) {
                if (!strstr(trenutne_klase, nova_klasa)) {
                    snprintf(novi_string, sizeof(novi_string), "%s %s", trenutne_klase, nova_klasa);
                    set_json_string(elem, "class_string", novi_string);
                }
            } else {
                set_json_string(elem, "class_string", nova_klasa);
            }
            set_json_bool(elem, "needs_layout", true);
            g_render_needs_layout = 1;
        }
    }
    
    if (id_str) JS_FreeCString(ctx, id_str);
    if (nova_klasa) JS_FreeCString(ctx, nova_klasa);
    JS_FreeValue(ctx, id_val);
    return JS_UNDEFINED;
}

static JSValue js_classList_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(id_val) || JS_IsNull(id_val) || JS_IsException(id_val)) {
        JS_FreeValue(ctx, id_val);
        return JS_UNDEFINED;
    }
    
    const char *id_str = JS_ToCString(ctx, id_val);
    const char *klasa_za_brisanje = JS_ToCString(ctx, argv[0]);
    
    if (id_str && klasa_za_brisanje && global_document_json) {
    //    extern cJSON* find_any_element(cJSON *root, const char *target_id);
        cJSON *elem = find_any_element(global_document_json, id_str);
        if (elem) {
            const char *trenutne_klase = get_json_string(elem, "class_string", "");
            if (strlen(trenutne_klase) > 0) {
                char bafer[512];
                strncpy(bafer, trenutne_klase, sizeof(bafer) - 1);
                
                char novi_string[512] = {0};
                char *token = strtok(bafer, " ");
                while (token != NULL) {
                    if (strcmp(token, klasa_za_brisanje) != 0) {
                        if (strlen(novi_string) > 0) strcat(novi_string, " ");
                        strcat(novi_string, token);
                    }
                    token = strtok(NULL, " ");
                }
                set_json_string(elem, "class_string", novi_string);
                set_json_bool(elem, "needs_layout", true);
                g_render_needs_layout = 1;
            }
        }
    }
    
    if (id_str) JS_FreeCString(ctx, id_str);
    if (klasa_za_brisanje) JS_FreeCString(ctx, klasa_za_brisanje);
    JS_FreeValue(ctx, id_val);
    return JS_UNDEFINED;
}

static JSValue js_element_focus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (target && global_pauk_ui) {
        global_pauk_ui->focused_element = target;
        if (INFO_MESSAGES_JS) printf("🎯 Element focused: %d\n", get_json_number(target, "element_id", -1));
    }
    return JS_UNDEFINED;
}

static JSValue js_element_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (target) {
        // 🚀 THE FIX: Use *argv (or argv) to pass the JSValue, not the pointer
        const char *attr = JS_ToCString(ctx, *argv); 
        if (attr) {
            cJSON_DetachItemFromObject(target, attr);
            JS_FreeCString(ctx, attr);
            g_render_needs_layout = 1;
        }
    }
    return JS_UNDEFINED;
}


static JSValue js_classList_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_FALSE;
    
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(id_val) || JS_IsNull(id_val) || JS_IsException(id_val)) {
        JS_FreeValue(ctx, id_val);
        return JS_FALSE;
    }
    
    const char *id_str = JS_ToCString(ctx, id_val);
    const char *klasa_za_proveru = JS_ToCString(ctx, argv[0]);
    bool ima_klasu = false;
    
    if (id_str && klasa_za_proveru && global_document_json) {
     //   extern cJSON* find_any_element(cJSON *root, const char *target_id);
        cJSON *elem = find_any_element(global_document_json, id_str);
        if (elem) {
            const char *trenutne_klase = get_json_string(elem, "class_string", "");
            if (strlen(trenutne_klase) > 0) {
                // Koristimo bezbednu tokenizaciju reči da "active-box" ne bi vratio true za proveru "active"
                char bafer[512];
                strncpy(bafer, trenutne_klase, sizeof(bafer) - 1);
                bafer[sizeof(bafer) - 1] = '\0';
                
                char *token = strtok(bafer, " ");
                while (token != NULL) {
                    if (strcmp(token, klasa_za_proveru) == 0) {
                        ima_klasu = true;
                        break;
                    }
                    token = strtok(NULL, " ");
                }
            }
        }
    }
    
    if (id_str) JS_FreeCString(ctx, id_str);
    if (klasa_za_proveru) JS_FreeCString(ctx, klasa_za_proveru);
    JS_FreeValue(ctx, id_val);
    
    return ima_klasu ? JS_TRUE : JS_FALSE;
}

static JSValue js_classList_toggle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_FALSE;
    
    // 🚀 PAMETNI REUZIMATELJ: Prvo zovemo contains da vidimo da li klasa već postoji
    JSValue contains_res = js_classList_contains(ctx, this_val, argc, argv);
    bool ima_klasu = JS_ToBool(ctx, contains_res);
    JS_FreeValue(ctx, contains_res);
    
    // Deklarišemo add i remove funkcije koje smo ranije napisali
    extern JSValue js_classList_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    extern JSValue js_classList_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
    
    if (ima_klasu) {
        // Ako je ima, brišemo je i vraćamo false (klasa je ugašena)
        JS_FreeValue(ctx, js_classList_remove(ctx, this_val, argc, argv));
        return JS_FALSE;
    } else {
        // Ako je nema, dodajemo je i vraćamo true (klasa je upaljena)
        JS_FreeValue(ctx, js_classList_add(ctx, this_val, argc, argv));
        return JS_TRUE;
    }
}


static JSValue js_document_get_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (global_document_json) {
        // Na nultom indeksu se u tvom cJSON-u uvek nalazi korenski body element stranice
        cJSON *body_node = cJSON_GetArrayItem(global_document_json, 0);
        if (body_node) {
            extern JSValue create_js_element_from_cjson(JSContext *ctx, cJSON* element);
            return create_js_element_from_cjson(ctx, body_node);
        }
    }
    return JS_NULL;
}


static JSValue js_form_submit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (!target) return JS_UNDEFINED;
    
    if (INFO_MESSAGES_JS) printf("🚀 Form submit() triggered from JavaScript!\n");
    
    // Proveravamo da li forma ima definisan "onsubmit" hendler u HTML-u
    const char *onsubmit_code = get_json_string(target, "onsubmit", NULL);
    if (onsubmit_code && strlen(onsubmit_code) > 0) {
        if (INFO_MESSAGES_JS) printf("   Executing form onsubmit code: %s\n", onsubmit_code);
        
        // Izvršavamo onsubmit JavaScript kod unutar istog konteksta
        JSValue res = JS_Eval(ctx, onsubmit_code, strlen(onsubmit_code), "<onsubmit>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(ctx, res);
    } else {
        // Ako nema onsubmit koda, pretraživač bi u realnosti promenio URL (action)
        if (INFO_MESSAGES_JS) printf("   Form has no onsubmit handler. Action target: %s\n", get_json_string(target, "action", "#"));
    }
    
    return JS_UNDEFINED;
}

// 🚀 INNERHTML GETTER: JavaScript traži sirovi tekst/sadržaj iz elementa
static JSValue js_element_getInnerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (!target) return JS_NewString(ctx, "");
    
    // Prvo gledamo da li element ima direktno upisan text/content
    const char *tekst = get_json_string(target, "text", NULL);
    if (!tekst) tekst = get_json_string(target, "content", NULL);
    
    // Ako nema direktno, pročešljaćemo prvo dete (tekstualni čvor)
    if (!tekst) {
        cJSON *children = cJSON_GetObjectItem(target, "children");
        if (children && cJSON_IsArray(children) && cJSON_GetArraySize(children) > 0) {
            cJSON *prvo_dete = cJSON_GetArrayItem(children, 0);
            tekst = get_json_string(prvo_dete, "text", NULL);
            if (!tekst) tekst = get_json_string(prvo_dete, "content", "");
        }
    }
    
    return JS_NewString(ctx, tekst ? tekst : "");
}

// 🚀 INNERHTML SETTER: JavaScript nasilno ubacuje novi sadržaj/tekst u element
static JSValue js_element_setInnerHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    cJSON *target = js_get_cjson_element(ctx, this_val);
    if (!target) return JS_UNDEFINED;
    
    const char *novi_sadrzaj = JS_ToCString(ctx, argv[0]);
    if (novi_sadrzaj) {
        if (INFO_MESSAGES_JS) {
            printf("📝 [DOM Engine] innerHTML setovan na: '%s'\n", novi_sadrzaj);
        }
        
        // 1. Ažuriramo glavne tekstualne vrednosti samog elementa
        set_json_string(target, "text", novi_sadrzaj);
        set_json_string(target, "content", novi_sadrzaj);
        
        // 2. FIZIČKO ČIŠĆENJE DECE: Brišemo sve stare pod-elemente jer ih innerHTML prepisuje!
        cJSON_DeleteItemFromObject(target, "children");
        cJSON *novi_children_niz = cJSON_AddArrayToObject(target, "children");
        
        if (novi_children_niz) {
            // 3. KREIRAMO NOVI TEKSTUALNI ČVOR: Usoravamo tekst unutar elementa
            cJSON *novi_tekst_cvor = cJSON_CreateObject();
            set_json_string(novi_tekst_cvor, "tag", "text");
            set_json_string(novi_tekst_cvor, "type", "text");
            set_json_string(novi_tekst_cvor, "display", "inline");
            set_json_string(novi_tekst_cvor, "content", novi_sadrzaj);
            set_json_string(novi_tekst_cvor, "text", novi_sadrzaj);
            
            // Nasleđujemo osnovne CSS parametre od roditelja
            set_json_number(novi_tekst_cvor, "font_size", get_json_number(target, "font_size", 14));
            set_json_string(novi_tekst_cvor, "font_weight", get_json_string(target, "font_weight", "normal"));
            set_json_string(novi_tekst_cvor, "color", get_json_string(target, "color", "#000000"));
            
            // Postavljamo layout koordinate na fabrički reset da ih engine premeri
            set_json_number(novi_tekst_cvor, "x", -99999);
            set_json_number(novi_tekst_cvor, "y", -99999);
            set_json_bool(novi_tekst_cvor, "is_text", 1);
            set_json_bool(novi_tekst_cvor, "is_inline", 1);
            set_json_bool(novi_tekst_cvor, "needs_layout", 1);
            set_json_number(novi_tekst_cvor, "width", 0);
            set_json_number(novi_tekst_cvor, "height", 0);
            set_json_number(novi_tekst_cvor, "layout_calculated", 0);
            
            // Generišemo bezbedan asinhroni ID
            static int inner_txt_id = 600000;
            int novi_id = inner_txt_id++;
            set_json_number(novi_tekst_cvor, "id", novi_id);
            set_json_number(novi_tekst_cvor, "element_id", novi_id);
            set_json_number(novi_tekst_cvor, "parent_id", get_json_number(target, "element_id", -1));
            
            cJSON_AddItemToArray(novi_children_niz, novi_tekst_cvor);
        }
        
        // 4. Pokrećemo globalni re-layout i osvežavanje ekrana!
        set_json_bool(target, "needs_layout", true);
        g_render_needs_layout = 1;
        
        // Automatski okidamo osvežavanje kroz naš centralni mrežni renderer
        extern JSValue js_request_render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
        js_request_render(ctx, JS_UNDEFINED, 0, NULL);
        
        JS_FreeCString(ctx, novi_sadrzaj);
    }
    return JS_UNDEFINED;
}

JSValue create_js_element_from_cjson(JSContext *ctx, cJSON* element) {
    if (!element || !ctx) return JS_NULL;
    
    int element_id = get_json_number(element, "element_id", -1);
    if (element_id < 0) return JS_NULL;
    
    JSValue obj = JS_NewObject(ctx);
    
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%d", element_id);
    JS_SetPropertyStr(ctx, obj, "_elementId", JS_NewString(ctx, id_buf));
    
    const char *tag = get_json_string(element, "tag", "div");
    JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, tag));
    
    const char *id = get_json_string(element, "id", "");
    if (id && strlen(id) > 0) {
        JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, id));
    }
        // Proveravamo da li je element u pitanju HTML forma
        const char *tag_name = get_json_string(element, "tag", "");
        if (strcmp(tag_name, "form") == 0) {
            // 🚀 Lepimo submit metodu samo na forme!
            JS_SetPropertyStr(ctx, obj, "submit", JS_NewCFunction(ctx, js_form_submit, "submit", 0));
        }
    // DIMENZIJE
    JS_SetPropertyStr(ctx, obj, "offsetWidth",
        JS_NewCFunction(ctx, js_element_get_offsetWidth, "offsetWidth", 0));
    JS_SetPropertyStr(ctx, obj, "offsetHeight",
        JS_NewCFunction(ctx, js_element_get_offsetHeight, "offsetHeight", 0));
    JS_SetPropertyStr(ctx, obj, "offsetLeft",
        JS_NewCFunction(ctx, js_element_get_offsetLeft, "offsetLeft", 0));
    JS_SetPropertyStr(ctx, obj, "offsetTop",
        JS_NewCFunction(ctx, js_element_get_offsetTop, "offsetTop", 0));
    
    // DOM METODE
    JS_SetPropertyStr(ctx, obj, "innerHTML",
        JS_NewCFunction(ctx, js_element_set_innerHTML, "innerHTML", 1));
    JS_SetPropertyStr(ctx, obj, "textContent",
        JS_NewCFunction(ctx, js_element_set_innerHTML, "textContent", 1));
    JS_SetPropertyStr(ctx, obj, "setText",
        JS_NewCFunction(ctx, js_element_setText, "setText", 1));
    JS_SetPropertyStr(ctx, obj, "setOnclick",
        JS_NewCFunction(ctx, js_element_setOnclick, "setOnclick", 1));
    JS_SetPropertyStr(ctx, obj, "addEventListener",
        JS_NewCFunction(ctx, js_element_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(ctx, obj, "getAttribute",
        JS_NewCFunction(ctx, js_element_getAttribute, "getAttribute", 1));
    JS_SetPropertyStr(ctx, obj, "setAttribute",
        JS_NewCFunction(ctx, js_element_setAttribute, "setAttribute", 2));
    
    // STYLE
    JSValue style_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, style_obj, "_elementId", JS_NewString(ctx, id_buf));
    JS_SetPropertyStr(ctx, style_obj, "setColor",
        JS_NewCFunction(ctx, js_style_setColor, "setColor", 1));
    JS_SetPropertyStr(ctx, style_obj, "setBackgroundColor",
        JS_NewCFunction(ctx, js_style_setBackgroundColor, "setBackgroundColor", 1));
    JS_SetPropertyStr(ctx, obj, "style", style_obj);
    
    //====InerHTML ======
        // 🚀 DEFINISANJE INNERHTML SVOJSTVA (Pravi JavaScript Getter i Setter)
        JSAtom inner_html_atom = JS_NewAtom(ctx, "innerHTML");
        JS_DefinePropertyGetSet(ctx, obj, inner_html_atom,
            JS_NewCFunction(ctx, js_element_getInnerHTML, "get_innerHTML", 0),
            JS_NewCFunction(ctx, js_element_setInnerHTML, "set_innerHTML", 1),
            JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
        JS_FreeAtom(ctx, inner_html_atom); // Odmah bezbedno čistimo atom iz QuickJS steka
    

        // 🚀 AKTIVIRANJE VALUE I CLASSLIST SVOJSTAVA
        JS_SetPropertyStr(ctx, obj, "getValue", JS_NewCFunction(ctx, js_element_getValue, "getValue", 0));
        JS_SetPropertyStr(ctx, obj, "setValue", JS_NewCFunction(ctx, js_element_setValue, "setValue", 1));
        JS_SetPropertyStr(ctx, obj, "classList", JS_NewCFunction(ctx, js_element_classList, "classList", 0));
    
    // UNSUPPORTED (sa warning-om)
    JS_SetPropertyStr(ctx, obj, "removeEventListener",
        JS_NewCFunction(ctx, js_unsupported, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, obj, "appendChild",
        JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, obj, "removeChild",
        JS_NewCFunction(ctx, js_element_removeChild, "removeChild", 1));

    
    // DEFAULT VREDNOSTI
    JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "children", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, obj, "firstChild", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "lastChild", JS_NULL);
    
    return obj;
}


cJSON* find_element_by_class_recursive(cJSON* element, const char* class_name) {
    if (!element) return NULL;
    
    // Proveri class_string
    const char* classes = get_json_string(element, "class_string", "");
    if (classes && strlen(classes) > 0) {
        char* class_copy = strdup(classes);
        if (class_copy) {
            char* token = strtok(class_copy, " ");
            while (token) {
                if (strcmp(token, class_name) == 0) {
                    free(class_copy);
                    return element;
                }
                token = strtok(NULL, " ");
            }
            free(class_copy);
        }
    }
    
    // Rekurzivno prođi kroz decu
    cJSON* children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON* child = cJSON_GetArrayItem(children, i);
            cJSON* found = find_element_by_class_recursive(child, class_name);
            if (found) return found;
        }
    }
    
    return NULL;
}

cJSON* find_element_by_tag_recursive(cJSON* element, const char* tag_name) {
    if (!element) return NULL;
    
    const char* tag = get_json_string(element, "tag", "");
    if (tag && strcmp(tag, tag_name) == 0) {
        return element;
    }
    
    cJSON* children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        int size = cJSON_GetArraySize(children);
        for (int i = 0; i < size; i++) {
            cJSON* child = cJSON_GetArrayItem(children, i);
            cJSON* found = find_element_by_tag_recursive(child, tag_name);
            if (found) return found;
        }
    }
    
    return NULL;
}


JSValue js_element_toString(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv) {
// Uzmi tagName
JSValue tag_val = JS_GetPropertyStr(ctx, this_val, "tagName");
const char *tag = JS_ToCString(ctx, tag_val);

if (!tag) {
JS_FreeValue(ctx, tag_val);
return JS_NewString(ctx, "[object HTMLElement]");
}

char result[256];
snprintf(result, sizeof(result), "[object HTML%sElement]", tag);

JS_FreeCString(ctx, tag);
JS_FreeValue(ctx, tag_val);

return JS_NewString(ctx, result);
}


JSValue js_url_search_params_toString(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv) {
// Vrati prazan string ili mock vrednost
return JS_NewString(ctx, "");
}


JSValue js_url_search_params(JSContext *ctx, JSValueConst this_val, 
    int argc, JSValueConst *argv) {
JSValue obj = JS_NewObject(ctx);

// Metode
JS_SetPropertyStr(ctx, obj, "get", 
JS_NewCFunction(ctx, js_unsupported, "get", 1));
JS_SetPropertyStr(ctx, obj, "set", 
JS_NewCFunction(ctx, js_unsupported, "set", 2));
JS_SetPropertyStr(ctx, obj, "append", 
JS_NewCFunction(ctx, js_unsupported, "append", 2));
JS_SetPropertyStr(ctx, obj, "toString", 
JS_NewCFunction(ctx, js_url_search_params_toString, "toString", 0));

return obj;
}


JSValue js_element_classList(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) return JS_UNDEFINED;

    // ID PASS SHIELD: Preslikavamo tajni _elementId na classList objekat
    JSValue id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (!JS_IsException(id_val) && !JS_IsUndefined(id_val) && !JS_IsNull(id_val)) {
        JS_SetPropertyStr(ctx, obj, "_elementId", id_val);
    } else {
        JS_FreeValue(ctx, id_val);
    }

    // 🚀 OŽIVLJAVANJE SVIH METODA ZA CLASSLIST!
    JS_SetPropertyStr(ctx, obj, "add", JS_NewCFunction(ctx, js_classList_add, "add", 1));
    JS_SetPropertyStr(ctx, obj, "remove", JS_NewCFunction(ctx, js_classList_remove, "remove", 1));
    JS_SetPropertyStr(ctx, obj, "contains", JS_NewCFunction(ctx, js_classList_contains, "contains", 1));
    JS_SetPropertyStr(ctx, obj, "toggle", JS_NewCFunction(ctx, js_classList_toggle, "toggle", 1));
    
    JS_SetPropertyStr(ctx, obj, "toString", JS_NewCFunction(ctx, js_classList_toString, "toString", 0));

    return obj;
}


/*
cJSON* find_element_by_element_id(cJSON *root, const char *element_id_str) {
    if (!root || !element_id_str) return NULL;
    
    int target_id = atoi(element_id_str);
    return find_element_by_id(root, target_id);
}
*/

static JSValue js_element_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;

    // 1. Pročitaj ID roditelja sa memorijskim štitom
    JSValue parent_id_val = JS_GetPropertyStr(ctx, this_val, "_elementId");
    if (JS_IsUndefined(parent_id_val) || JS_IsNull(parent_id_val)) {
        JS_FreeValue(ctx, parent_id_val);
        parent_id_val = JS_GetPropertyStr(ctx, this_val, "id");
    }
    if (JS_IsException(parent_id_val)) {
        JS_FreeValue(ctx, parent_id_val);
        return JS_DupValue(ctx, argv[0]);
    }
    const char *parent_id = JS_ToCString(ctx, parent_id_val);

    // 2. Pročitaj ID deteta koje treba obrisati
    JSValue child_id_val = JS_GetPropertyStr(ctx, argv[0], "_elementId");
    if (JS_IsUndefined(child_id_val) || JS_IsNull(child_id_val)) {
        JS_FreeValue(ctx, child_id_val);
        child_id_val = JS_GetPropertyStr(ctx, argv[0], "id");
    }
    if (JS_IsException(child_id_val)) {
        if (parent_id) JS_FreeCString(ctx, parent_id);
        JS_FreeValue(ctx, parent_id_val);
        JS_FreeValue(ctx, child_id_val);
        return JS_DupValue(ctx, argv[0]);
    }
    const char *child_id = JS_ToCString(ctx, child_id_val);

    // 3. Smeštamo zahtev u asinhroni red za brisanje
    if (parent_id && child_id && strlen(parent_id) > 0 && strlen(child_id) > 0) {
        pending_remove_t *new_requests = realloc(pending_remove_requests, 
            (pending_remove_count + 1) * sizeof(pending_remove_t));
            
        if (new_requests) {
            pending_remove_requests = new_requests;
            pending_remove_requests[pending_remove_count].parent_id = strdup(parent_id);
            pending_remove_requests[pending_remove_count].child_id = strdup(child_id);
            pending_remove_count++;

            if(INFO_MESSAGES_JS) {
                printf("🗑️ removeChild: pending request %d: parent=%s, child=%s\n", 
                       pending_remove_count, parent_id, child_id);
            }
            js_request_render(ctx, JS_UNDEFINED, 0, NULL);
            g_render_needs_layout = 1;
        }
    }

    // Oslobađamo C stringove i privremene vrednosti
    if (parent_id) JS_FreeCString(ctx, parent_id);
    if (child_id) JS_FreeCString(ctx, child_id);
    JS_FreeValue(ctx, parent_id_val);
    JS_FreeValue(ctx, child_id_val);

    // Vraćamo dupliranu vrednost deteta kako traži JS standard
    return JS_DupValue(ctx, argv[0]);
}

// 🚀 BEZBEDNI ASINHRONI MOSTRUKO OKIDAČ: Izvršava se unutar svog matičnog fajla gde je struktura stoprocentno sigurna
int js_execute_callback_if_exists(const char *element_id, const char *event_type) {
    if (!element_id || !event_type || !g_js_context) return 0;
    
    if (event_callbacks && event_callback_count > 0) {
        for (int i = 0; i < event_callback_count; i++) {
            if (event_callbacks[i].element_id && event_callbacks[i].event_type) {
                if (strcmp(event_callbacks[i].element_id, element_id) == 0 &&
                    strcmp(event_callbacks[i].event_type, event_type) == 0) {
                    
                    if (JS_IsFunction(g_js_context, event_callbacks[i].callback)) {
                        if(INFO_MESSAGES_JS) {
                            printf("⚙️ [JS Engine] Pokrećem registrovani callback za #%s (%s)\n", element_id, event_type);
                        }
                        
                        JSValue global_obj = JS_GetGlobalObject(g_js_context);
                        JSValue ret = JS_Call(g_js_context, event_callbacks[i].callback, global_obj, 0, NULL);
                        
                        JS_FreeValue(g_js_context, global_obj);
                        JS_FreeValue(g_js_context, ret);
                        return 1; // Uspešno pronađen i izvršen callback!
                    }
                }
            }
        }
    }
    return 0; // Nije pronađen nijedan JS listener
}


// 🚀 ASINHRONI JS CALLBACK MOSTAR: Izvršava se direktno u kontekstu QuickJS-a!
int pozovi_asinhroni_js_callback(const char *el_id, const char *ev_type) {
    if (!el_id || !ev_type || !g_js_context) return 0;

    if (event_callbacks && event_callback_count > 0) {
        for (int i = 0; i < event_callback_count; i++) {
            if (event_callbacks[i].element_id && event_callbacks[i].event_type) {
                // Proveravamo poklapanje ID-ja i događaja (npr. "test13_input" i "click")
                if (strcmp(event_callbacks[i].element_id, el_id) == 0 &&
                    strcmp(event_callbacks[i].event_type, ev_type) == 0) {
                    
                    if (JS_IsFunction(g_js_context, event_callbacks[i].callback)) {
                        if (INFO_MESSAGES_JS) {
                            printf("⚙️ [Event Engine] Pokrećem asinhroni JS callback za #%s (%s)\n", el_id, ev_type);
                        }
                        
                        // Izvlačimo globalni window/globalThis kao 'this' kontekst za funkciju
                        JSValue global_obj = JS_GetGlobalObject(g_js_context);
                        JSValue ret = JS_Call(g_js_context, event_callbacks[i].callback, global_obj, 0, NULL);
                        
                        // Odmah oslobađamo privremene JS reference iz steka da ne curi RAM
                        JS_FreeValue(g_js_context, global_obj);
                        JS_FreeValue(g_js_context, ret);
                        return 1; // Uspešno pronađeno i pokrenuto!
                    }
                }
            }
        }
    }
    return 0; // Nije pronađen nijedan JS listener za ovaj element
}
