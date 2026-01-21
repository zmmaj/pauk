#include "js_executor_quickjs.h"
#include "cjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    char *element_id;
    char *property;
    JSValue js_callback;
    void (*native_callback)(lxb_dom_element_t*, const char*);
} JSRenderCallback;

static cJSON *js_style_modifications = NULL;
// Global runtime for better memory management
static JSRuntime *global_rt = NULL;
static lxb_html_document_t *global_document = NULL;
static JSRenderCallback *js_render_callbacks = NULL;
static int js_callback_count = 0;


void init_js_modifications(void) {
    if (!js_style_modifications) {
        js_style_modifications = cJSON_CreateObject();
    }
}


static JSValue js_request_render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS -> Renderer: requestRender()\n");
    
    // Set a flag that renderer should update
    // In a real implementation, this would trigger a re-render
    // For now, just log it
    
    return JS_UNDEFINED;
}


static JSValue js_get_computed_layout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS -> Renderer: getComputedLayout()\n");
    
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
    
    const char *element_id = JS_ToCString(ctx, argv[0]);
    const char *property = JS_ToCString(ctx, argv[1]);
    const char *value = JS_ToCString(ctx, argv[2]);
    
    if (!element_id || !property || !value) {
        if (element_id) JS_FreeCString(ctx, element_id);
        if (property) JS_FreeCString(ctx, property);
        if (value) JS_FreeCString(ctx, value);
        return JS_UNDEFINED;
    }
    
    printf("JS -> Renderer: setStyle('%s', '%s', '%s')\n", element_id, property, value);
    
    // Initialize if needed
    init_js_modifications();
    
    // Find or create element entry
    cJSON *element_mods = cJSON_GetObjectItemCaseSensitive(js_style_modifications, element_id);
    if (!element_mods) {
        element_mods = cJSON_CreateObject();
        cJSON_AddItemToObject(js_style_modifications, element_id, element_mods);
    }
    
    // Find or create styles object
    cJSON *styles = cJSON_GetObjectItem(element_mods, "style");
    if (!styles) {
        styles = cJSON_CreateObject();
        cJSON_AddItemToObject(element_mods, "style", styles);
    }
    
    // Add/update the style property
    cJSON_ReplaceItemInObject(styles, property, cJSON_CreateString(value));
    
    // Also try to update the actual DOM element if it exists
    if (global_document && strlen(element_id) > 0) {
        lxb_dom_node_t *root = lxb_dom_interface_node(global_document);
        lxb_dom_element_t *elem = find_element_by_id_recursive(root, element_id, 0);
        
        if (elem) {
            // Create or update style attribute
            lxb_dom_attr_t *style_attr = lxb_dom_element_attr_by_name(elem, (lxb_char_t*)"style", 5);
            
            if (style_attr) {
                // Get existing style
                size_t style_len;
                const lxb_char_t *existing_style = lxb_dom_attr_value(style_attr, &style_len);
                char new_style[1024];
                
                if (existing_style && style_len > 0) {
                    // Parse and update existing style
                    char *style_str = malloc(style_len + 1);
                    memcpy(style_str, existing_style, style_len);
                    style_str[style_len] = '\0';
                    
                    // Simple update - just append new property
                    snprintf(new_style, sizeof(new_style), "%s; %s: %s", style_str, property, value);
                    free(style_str);
                } else {
                    // New style
                    snprintf(new_style, sizeof(new_style), "%s: %s", property, value);
                }
                
                // Update the attribute
                lxb_dom_attr_set_value(style_attr, (lxb_char_t*)new_style, strlen(new_style));
            }
        }
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeCString(ctx, property);
    JS_FreeCString(ctx, value);
    
    return JS_UNDEFINED;
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
    
    printf("JS -> Renderer: updateElement('%s', '%s')\n", element_id, properties_json);
    
    // Parse the properties JSON
    cJSON *properties = cJSON_Parse(properties_json);
    if (properties) {
        init_js_modifications();
        
        // Store the properties
        cJSON *element_mods = cJSON_GetObjectItemCaseSensitive(js_style_modifications, element_id);
        if (!element_mods) {
            element_mods = cJSON_CreateObject();
            cJSON_AddItemToObject(js_style_modifications, element_id, element_mods);
        }
        
        // Merge properties
        cJSON *item = properties->child;
        while (item) {
            cJSON_ReplaceItemInObject(element_mods, item->string, cJSON_Duplicate(item, 1));
            item = item->next;
        }
        
        cJSON_Delete(properties);
    }
    
    JS_FreeCString(ctx, element_id);
    JS_FreeCString(ctx, properties_json);
    
    return JS_UNDEFINED;
}

cJSON* get_js_modifications(void) {
    if (js_style_modifications) {
        return cJSON_Duplicate(js_style_modifications, 1);
    }
    return NULL;
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
            printf("DEBUG: Got layout JSON: %s\n", json_str);
            layout_json = cJSON_Parse(json_str);
            if (!layout_json) {
                printf("WARNING: Failed to parse layout JSON from JS: %s\n", json_str);
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
    printf("DOM Bridge: Connected QuickJS to Lexbor document\n");
}

// Real document.getElementById implementation
static JSValue js_document_get_element_by_id_real(JSContext *ctx, 
    JSValueConst this_val, int argc, JSValueConst *argv) {
    
    if (argc < 1 || !global_document) return JS_UNDEFINED;
    
    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_UNDEFINED;
    
    printf("JS: Searching for element with id='%s'\n", id);
    
    // Start search from document root
    lxb_dom_node_t *root = lxb_dom_interface_node(global_document);
    
    // Use recursive search with depth 0
    lxb_dom_element_t *found_element = find_element_by_id_recursive(root, id, 0);
    JSValue obj = JS_NewObject(ctx);
    
    if (found_element) {
        // Get element tag name
        size_t tag_len;
        const lxb_char_t *tag_name = lxb_dom_element_local_name(found_element, &tag_len);
        
        char tag_str[32];
        if (tag_name && tag_len > 0) {
            size_t copy_len = tag_len < 31 ? tag_len : 31;
            memcpy(tag_str, tag_name, copy_len);
            tag_str[copy_len] = '\0';
        } else {
            strcpy(tag_str, "element");
        }
        
        JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, id));
        JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, tag_str));
        JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 1)); // ELEMENT_NODE
        
        // Get text content
        lxb_dom_node_t *element_node = lxb_dom_interface_node(found_element);
        size_t text_len;
        lxb_char_t *text_content = lxb_dom_node_text_content(element_node, &text_len);
        
        if (text_content && text_len > 0) {
            char *text_str = malloc(text_len + 1);
            memcpy(text_str, text_content, text_len);
            text_str[text_len] = '\0';
            JS_SetPropertyStr(ctx, obj, "textContent", JS_NewString(ctx, text_str));
            free(text_str);
        } else {
            JS_SetPropertyStr(ctx, obj, "textContent", JS_NewString(ctx, ""));
        }
        
        if (text_content) {
            lxb_dom_document_destroy_text(element_node->owner_document, text_content);
        }
        
        printf("✓ Found element: %s#%s\n", tag_str, id);
    } else {
        JS_SetPropertyStr(ctx, obj, "id", JS_NULL);
        JS_SetPropertyStr(ctx, obj, "tagName", JS_NULL);
        JS_SetPropertyStr(ctx, obj, "textContent", JS_NewString(ctx, ""));
        printf("✗ Element not found: %s\n", id);
    }
    
    JS_FreeCString(ctx, id);
    return obj;
}


// Console.log implementation
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS console: ");
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            printf("%s ", str);
            JS_FreeCString(ctx, str);
        }
    }
    printf("\n");
    return JS_UNDEFINED;
}

// document.getElementById implementation
static JSValue js_document_get_element_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_UNDEFINED;
    
    printf("JS: document.getElementById('%s')\n", id);
    
    // Create mock element object
    JSValue obj = JS_NewObject(ctx);
    
    // Add properties
    JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, id));
    JS_SetPropertyStr(ctx, obj, "innerHTML", JS_NewString(ctx, "Mock content"));
    JS_SetPropertyStr(ctx, obj, "textContent", JS_NewString(ctx, "Mock content"));
    
    // Add style object
    JSValue style_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "style", style_obj);
    
    JS_FreeCString(ctx, id);
    return obj;
}

// Safe mock function
static JSValue js_safe_mock_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("JS: Safe mock function called\n");
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
    
    // Create document object - ALWAYS use real functions when global_document exists
    JSValue document_obj = JS_NewObject(ctx);
    
    if (global_document) {
        JS_SetPropertyStr(ctx, document_obj, "getElementById",
            JS_NewCFunction(ctx, js_document_get_element_by_id_real, "getElementById", 1));
        
        // Set body property directly
        lxb_html_body_element_t *body = lxb_html_document_body_element(global_document);
        if (body) {
            JSValue body_obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, body_obj, "tagName", JS_NewString(ctx, "BODY"));
            JS_SetPropertyStr(ctx, document_obj, "body", body_obj);
        } else {
            JS_SetPropertyStr(ctx, document_obj, "body", JS_NULL);
        }
    } else {
        // Fallback to mock
        JS_SetPropertyStr(ctx, document_obj, "getElementById",
            JS_NewCFunction(ctx, js_document_get_element_by_id, "getElementById", 1));
        JS_SetPropertyStr(ctx, document_obj, "body", JS_NULL);
    }
    
    JS_SetPropertyStr(ctx, document_obj, "title", JS_NewString(ctx, "Real Document"));
    JS_SetPropertyStr(ctx, global_obj, "document", document_obj);
    
    // Create window object (same as global in browsers)
    JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));
    
    // Register safe mock functions for common web APIs - INCLUDED
    const char *mock_functions[] = {
        "initializePage", "incrementCounter", "updateCounterDisplay",
        "addEventListener", "setTimeout", "setInterval", "alert", "confirm",
        "querySelector", "querySelectorAll", "createElement", "appendChild",
        "setAttribute", "getAttribute", "removeAttribute", "classList",
        "focus", "blur", "click", NULL
    };
    
    for (int i = 0; mock_functions[i] != NULL; i++) {
        JS_SetPropertyStr(ctx, global_obj, mock_functions[i],
            JS_NewCFunction(ctx, js_safe_mock_function, mock_functions[i], 0));
    }
    
    // ========== ADD NATIVE BRIDGE HERE ==========
    // Add after the mock functions loop, before JS_FreeValue
    
    // Create native bridge for renderer communication
    JSValue native_bridge = JS_NewObject(ctx);
    
    // Add setStyle function
    JS_SetPropertyStr(ctx, native_bridge, "setStyle", 
                     JS_NewCFunction(ctx, js_native_set_style, "setStyle", 3));
    
    // Add requestRender function  
    JS_SetPropertyStr(ctx, native_bridge, "requestRender",
                     JS_NewCFunction(ctx, js_request_render, "requestRender", 0));
    
    // Add getComputedLayout function
    JS_SetPropertyStr(ctx, native_bridge, "getComputedLayout",
                     JS_NewCFunction(ctx, js_get_computed_layout, "getComputedLayout", 0));
    
    // Add updateElement function
    JS_SetPropertyStr(ctx, native_bridge, "updateElement",
                     JS_NewCFunction(ctx, js_update_element, "updateElement", 2));
    
    // Add the bridge to global object
    JS_SetPropertyStr(ctx, global_obj, "__native", native_bridge);
    
    // ========== END OF NATIVE BRIDGE ==========
    
    JS_FreeValue(ctx, global_obj);
}

// Initialize QuickJS engine - SAME FUNCTION NAME as Duktape version
JSContext* js_engine_init(void) {
    // Create runtime
       // Create runtime and context only - NO DOM registration
       global_rt = JS_NewRuntime();
       if (!global_rt) return NULL;
       
       JS_SetMemoryLimit(global_rt, 64 * 1024 * 1024);
       
       JSContext *ctx = JS_NewContext(global_rt);
       if (!ctx) {
           JS_FreeRuntime(global_rt);
           global_rt = NULL;
           return NULL;
       }
       
       //js_std_add_helpers(ctx, 0, NULL);
       return ctx;
   }

   void js_register_dom(JSContext *ctx) {
    // Now register DOM API with global_document set
    js_register_dom_api(ctx);
}

// Cleanup - SAME FUNCTION NAME as Duktape version
void js_engine_cleanup(JSContext *ctx) {
    if (ctx) {
        JS_FreeContext(ctx);
    }
    if (global_rt) {
        JS_FreeRuntime(global_rt);
        global_rt = NULL;
    }
    printf("QuickJS JavaScript engine cleaned up\n");
}

// Execute JavaScript code - SAME FUNCTION NAME as Duktape version
void js_execute_code(JSContext *ctx, const char *script) {
    if (!ctx || !script) return;
    
    // Skip empty scripts (same logic as your Duktape version)
    if (strlen(script) == 0) {
        printf("Empty script, skipping\n");
        return;
    }
    
    printf("Executing JavaScript with QuickJS (%zu bytes)...\n", strlen(script));
    
    // Check for common HTML tags in script (same safety check)
    if (strstr(script, "<!DOCTYPE") || strstr(script, "<html") || 
        strstr(script, "<head") || strstr(script, "<body")) {
        printf("ERROR: Script contains HTML tags! This shouldn't happen.\n");
        printf("First 100 chars: %.100s\n", script);
        return;
    }
    
    JSValue result = JS_Eval(ctx, script, strlen(script), "<script>", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        printf("JavaScript error: %s\n", error);
        
        // Get line number if available
        JSValue line_number = JS_GetPropertyStr(ctx, exception, "lineNumber");
        if (JS_IsNumber(line_number)) {
            int line_num;
            JS_ToInt32(ctx, &line_num, line_number);
            printf("Error at line: %d\n", line_num);
        }
        JS_FreeValue(ctx, line_number);
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    } else {
        printf("JavaScript executed successfully\n");
        
        // Clean up result (same as Duktape version)
        if (JS_IsString(result)) {
            const char *result_str = JS_ToCString(ctx, result);
            if (result_str && strlen(result_str) > 0) {
                printf("Result: %s\n", result_str);
                JS_FreeCString(ctx, result_str);
            }
        }
    }
    
    JS_FreeValue(ctx, result);
}

// Extract and execute script with crash protection - SAME LOGIC as Duktape version
static void execute_single_script(JSContext *ctx, lxb_dom_node_t *node, int script_num) {
    printf("Script #%d: Safe extraction...\n", script_num);
    
    // DIRECT EXTRACTION - same as your Duktape version
    size_t script_len;
    lxb_char_t *script_content = lxb_dom_node_text_content(node, &script_len);
    
    if (!script_content || script_len == 0) {
        printf("Script #%d: No content\n", script_num);
        if (script_content) lxb_dom_document_destroy_text(node->owner_document, script_content);
        return;
    }
    
    printf("Script #%d: Got %zu bytes\n", script_num, script_len);
    
    // Simple allocation and copy (same as Duktape)
    char *script_str = malloc(script_len + 1);
    if (!script_str) {
        printf("Script #%d: Allocation failed\n", script_num);
        lxb_dom_document_destroy_text(node->owner_document, script_content);
        return;
    }
    
    memcpy(script_str, script_content, script_len);
    script_str[script_len] = '\0';
    
    // Clean up IMMEDIATELY (same as Duktape)
    lxb_dom_document_destroy_text(node->owner_document, script_content);
    
    // Quick debug (same as Duktape)
    printf("Script #%d preview: ", script_num);
    if (script_len > 50) {
        printf("%.50s...\n", script_str);
    } else {
        printf("%s\n", script_str);
    }
    
    // Execute if reasonable size (same logic as Duktape)
    if (script_len > 0 && script_len <= 5000) {
        js_execute_code(ctx, script_str);
    } else {
        printf("Script #%d: Size %zu, skipping\n", script_num, script_len);
    }
    
    free(script_str);
}

// Safe recursive script execution - SAME FUNCTION NAME as Duktape version
void js_execute_script_elements_recursive(JSContext *ctx, lxb_dom_node_t *node, int *script_count) {
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *element = lxb_dom_interface_element(node);
            size_t name_len;
            const lxb_char_t *name = lxb_dom_element_local_name(element, &name_len);
            
            if (name && name_len == 6 && memcmp(name, "script", 6) == 0) {
                (*script_count)++;
                
                // Only process inline scripts (skip external)
                lxb_dom_attr_t *src_attr = lxb_dom_element_attr_by_name(element, 
                    (lxb_char_t*)"src", 3);
                
                if (!src_attr) {
                    execute_single_script(ctx, node, *script_count);
                } else {
                    printf("Script #%d: EXTERNAL (skipped)\n", *script_count);
                }
            }
        }
        
        // Process children recursively - CORRECT
        lxb_dom_node_t *child = lxb_dom_node_first_child(node);
        if (child) {
            js_execute_script_elements_recursive(ctx, child, script_count);
        }
        
        node = lxb_dom_node_next(node);
    }
}

// Main script execution with crash protection - SAME FUNCTION NAME as Duktape
void js_execute_script_elements(JSContext *ctx, lxb_html_document_t *document) {
    if (!ctx || !document) return;
    
    printf("=== EXECUTING SCRIPTS ===\n");
    
    // CRITICAL: Register DOM API FIRST
    js_set_document(ctx, document);
    js_register_dom(ctx);
    
    printf("DOM API registered for JavaScript\n");
    
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
    
    // 1. Execute all regular page scripts
    int script_count = 0;
    lxb_dom_node_t *root = lxb_dom_interface_node(document);
    js_execute_script_elements_recursive(ctx, root, &script_count);
    
    printf("Executed %d regular script(s)\n", script_count);
    
    // 2. NOW run the layout calculator (AFTER all page scripts)
    printf("\n=== CALCULATING FINAL LAYOUT (Post-JS) ===\n");
    
    const char *layout_calculator_script = 
    "try {\n"
    "    console.log('=== SIMPLE LAYOUT CALCULATOR ===');\n"
    "    \n"
    "    // Simple layout result\n"
    "    const layoutResult = {\n"
    "        viewport: {\n"
    "            width: 800,\n"
    "            height: 600\n"
    "        },\n"
    "        elements: {},\n"
    "        elementCount: 0\n"
    "    };\n"
    "    \n"
    "    // Simple recursive function to calculate positions\n"
    "    function calculateSimpleLayout(element, parentX, parentY, depth) {\n"
    "        if (!element || depth > 10) return;\n"
    "        \n"
    "        // Get element info\n"
    "        const tagName = element.tagName ? element.tagName.toLowerCase() : 'unknown';\n"
    "        const elementId = element.id || '';\n"
    "        const className = element.className || '';\n"
    "        \n"
    "        // Skip script and style elements\n"
    "        if (tagName === 'script' || tagName === 'style' || tagName === 'meta' || \n"
    "            tagName === 'link' || tagName === 'title') {\n"
    "            return;\n"
    "        }\n"
    "        \n"
    "        // Calculate position based on element type and depth\n"
    "        let x = parentX + 10;\n"
    "        let y = parentY + 10;\n"
    "        let width = 780;  // Default width\n"
    "        let height = 30;  // Default height\n"
    "        \n"
    "        // Adjust for different element types\n"
    "        if (tagName === 'body') {\n"
    "            x = 0;\n"
    "            y = 0;\n"
    "            width = 800;\n"
    "            height = 600;\n"
    "        } else if (tagName === 'h1') {\n"
    "            height = 40;\n"
    "            x = 20;\n"
    "        } else if (tagName === 'h2') {\n"
    "            height = 35;\n"
    "            x = 25;\n"
    "        } else if (tagName === 'h3') {\n"
    "            height = 30;\n"
    "            x = 30;\n"
    "        } else if (tagName === 'p') {\n"
    "            height = 25;\n"
    "            x = 20;\n"
    "        } else if (tagName === 'div') {\n"
    "            x = 15;\n"
    "        }\n"
    "        \n"
    "        // Create element info\n"
    "        const elementInfo = {\n"
    "            tag: tagName,\n"
    "            id: elementId,\n"
    "            className: className,\n"
    "            x: x,\n"
    "            y: y,\n"
    "            width: width,\n"
    "            height: height,\n"
    "            depth: depth,\n"
    "            index: layoutResult.elementCount\n"
    "        };\n"
    "        \n"
    "        // Create unique key\n"
    "        let key = '';\n"
    "        if (elementId) {\n"
    "            key = elementId;\n"
    "        } else {\n"
    "            key = tagName + '_' + layoutResult.elementCount;\n"
    "        }\n"
    "        \n"
    "        // Store element\n"
    "        layoutResult.elements[key] = elementInfo;\n"
    "        layoutResult.elementCount++;\n"
    "        \n"
    "        // Process children\n"
    "        if (element.children) {\n"
    "            let childY = y + height + 5;\n"
    "            \n"
    "            for (let i = 0; i < element.children.length; i++) {\n"
    "                const child = element.children[i];\n"
    "                calculateSimpleLayout(child, x + 10, childY, depth + 1);\n"
    "                \n"
    "                // Update Y for next child\n"
    "                if (i < element.children.length - 1) {\n"
    "                    childY += 35;  // Space between children\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    \n"
    "    // Start calculation from body\n"
    "    if (document && document.body) {\n"
    "        console.log('Starting layout calculation from body');\n"
    "        calculateSimpleLayout(document.body, 0, 0, 0);\n"
    "        \n"
    "        // Always add body if not already added\n"
    "        if (!layoutResult.elements['body']) {\n"
    "            layoutResult.elements['body'] = {\n"
    "                tag: 'body',\n"
    "                id: '',\n"
    "                className: '',\n"
    "                x: 0,\n"
    "                y: 0,\n"
    "                width: 800,\n"
    "                height: 600,\n"
    "                depth: 0,\n"
    "                index: layoutResult.elementCount\n"
    "            };\n"
    "            layoutResult.elementCount++;\n"
    "        }\n"
    "        \n"
    "        console.log('Layout calculated with', layoutResult.elementCount, 'elements');\n"
    "    } else {\n"
    "        console.warn('No document.body for layout calculation');\n"
    "        \n"
    "        // Fallback: Add at least body\n"
    "        layoutResult.elements['body'] = {\n"
    "            tag: 'body',\n"
    "            x: 0,\n"
    "            y: 0,\n"
    "            width: 800,\n"
    "            height: 600\n"
    "        };\n"
    "        layoutResult.elementCount = 1;\n"
    "    }\n"
    "    \n"
    "    // Store the result\n"
    "    window.__computedLayout = layoutResult;\n"
    "    console.log('Layout calculation complete');\n"
    "    \n"
    "} catch (error) {\n"
    "    console.error('Layout calculation error:', error);\n"
    "    console.error('Error message:', error.message);\n"
    "    \n"
    "    // Create error layout\n"
    "    window.__computedLayout = {\n"
    "        error: error.message,\n"
    "        viewport: {width: 800, height: 600},\n"
    "        elements: {\n"
    "            body: {\n"
    "                tag: 'body',\n"
    "                x: 0,\n"
    "                y: 0,\n"
    "                width: 800,\n"
    "                height: 600\n"
    "            }\n"
    "        },\n"
    "        elementCount: 1\n"
    "    };\n"
    "}\n";
    
    // Execute the layout calculator
    js_execute_code(ctx, layout_calculator_script);
    
    printf("Layout calculation script executed\n");
    
    // Test if layout was created
    const char *layout_test_script = 
    "try {\n"
    "    if (window.__computedLayout) {\n"
    "        console.log('Layout created successfully');\n"
    "        console.log('Elements count:', Object.keys(window.__computedLayout.elements || {}).length);\n"
    "        window.__layoutCreated = true;\n"
    "    } else {\n"
    "        console.error('No layout created');\n"
    "        window.__layoutCreated = false;\n"
    "    }\n"
    "} catch(e) {\n"
    "    console.error('Layout test failed:', e);\n"
    "    window.__layoutCreated = false;\n"
    "}\n";
    
    js_execute_code(ctx, layout_test_script);
}


// Add script sandboxing
void js_set_security_policy(JSContext *ctx, SecurityPolicy policy) {
    if (!ctx) return;
    
    printf("Setting JavaScript security policy:\n");
    printf("  Max memory: %zu bytes\n", policy.max_memory_bytes);
    printf("  Max execution time: %d ms\n", policy.max_execution_time_ms);
    printf("  Allow network APIs: %s\n", policy.allow_network_apis ? "yes" : "no");
    printf("  Allow file APIs: %s\n", policy.allow_file_apis ? "yes" : "no");
    printf("  Allow DOM APIs: %s\n", policy.allow_dom_apis ? "yes" : "no");
    printf("  Enable console: %s\n", policy.enable_console ? "yes" : "no");
    
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

