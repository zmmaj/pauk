// lua_position.c - C interface to Lua position calculator
#include "lua_position.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

// Helper function to print Lua stack for debugging
static void print_lua_stack(lua_State* L, const char* context) {
    printf("[LUA DEBUG %s] Stack top: %d\n", context, lua_gettop(L));
    for (int i = 1; i <= lua_gettop(L); i++) {
        int type = lua_type(L, i);
        const char* type_name = lua_typename(L, type);
        printf("  [%d] %s", i, type_name);
        
        if (type == LUA_TSTRING) {
            printf(": \"%s\"", lua_tostring(L, i));
        } else if (type == LUA_TNUMBER) {
            printf(": %g", lua_tonumber(L, i));
        } else if (type == LUA_TBOOLEAN) {
            printf(": %s", lua_toboolean(L, i) ? "true" : "false");
        } else if (type == LUA_TNIL) {
            printf(": nil");
        }
        printf("\n");
    }
}

// Initialize Lua and load position calculation script
lua_State* init_lua_position_calculator(const char* lua_script_path) {
    printf("Initializing Lua position calculator...\n");
    printf("Looking for script: %s\n", lua_script_path);
    
    // Check if file exists first
    FILE* test = fopen(lua_script_path, "r");
    if (!test) {
        printf("ERROR: Lua script file not found: %s (errno: %d)\n", 
               lua_script_path, errno);
        return NULL;
    }
    fclose(test);
    
    // Create Lua state
    lua_State* L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "ERROR: Cannot create Lua state (out of memory?)\n");
        return NULL;
    }
    
    printf("Lua state created successfully\n");
    
    // Open standard libraries
    luaL_openlibs(L);
    printf("Lua standard libraries loaded\n");
    
    // Try to load the script file
    printf("Loading Lua script from: %s\n", lua_script_path);
    int load_result = luaL_loadfile(L, lua_script_path);
    
    if (load_result != LUA_OK) {
        const char* error_msg = lua_tostring(L, -1);
        fprintf(stderr, "ERROR loading Lua script (code %d): %s\n", 
                load_result, error_msg);
        
        if (load_result == LUA_ERRSYNTAX) {
            fprintf(stderr, "Syntax error in Lua script\n");
        } else if (load_result == LUA_ERRMEM) {
            fprintf(stderr, "Memory allocation error\n");
        } else if (load_result == LUA_ERRFILE) {
            fprintf(stderr, "File error\n");
        }
        
        print_lua_stack(L, "LOAD ERROR");
        lua_close(L);
        return NULL;
    }
    
    printf("Lua script loaded (not executed yet)\n");
    print_lua_stack(L, "AFTER LOAD");
    
    // Execute the script
    printf("Executing Lua script...\n");
    int exec_result = lua_pcall(L, 0, 1, 0);  // 0 args, 1 result
    
    if (exec_result != LUA_OK) {
        const char* error_msg = lua_tostring(L, -1);
        fprintf(stderr, "ERROR executing Lua script (code %d): %s\n", 
                exec_result, error_msg);
        
        if (exec_result == LUA_ERRRUN) {
            fprintf(stderr, "Runtime error\n");
        } else if (exec_result == LUA_ERRMEM) {
            fprintf(stderr, "Memory allocation error during execution\n");
        } else if (exec_result == LUA_ERRERR) {
            fprintf(stderr, "Error handler error\n");
        }
        
        print_lua_stack(L, "EXEC ERROR");
        lua_close(L);
        return NULL;
    }
    
    printf("Lua script executed successfully\n");
    print_lua_stack(L, "AFTER EXEC");
    
    // Check what we got back (should be a table)
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "ERROR: Lua script didn't return a table (got %s)\n",
                lua_typename(L, lua_type(L, -1)));
        print_lua_stack(L, "WRONG RETURN TYPE");
        lua_close(L);
        return NULL;
    }
    
    // Store the module in global namespace
    lua_setglobal(L, "calculate_positions");
    printf("Lua module stored in global namespace\n");
    
    // Verify it's accessible
    lua_getglobal(L, "calculate_positions");
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "ERROR: Global 'calculate_positions' is not a table\n");
        lua_close(L);
        return NULL;
    }
    
    // Check for required functions
    lua_getfield(L, -1, "calculate_positions");
    if (!lua_isfunction(L, -1)) {
        fprintf(stderr, "ERROR: calculate_positions function not found in module\n");
        lua_pop(L, 2);  // Pop function check and module table
        lua_close(L);
        return NULL;
    }
    lua_pop(L, 2);  // Pop function and module table
    
    printf("Lua position calculator initialized successfully\n");
    return L;
}

// Push layout configuration to Lua
static void push_layout_config(lua_State* L, LuaLayoutConfig* config) {
    if (!config) return;
    
    printf("[LUA] Pushing layout configuration...\n");
    
    lua_getglobal(L, "calculate_positions");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "config");
        
        if (lua_istable(L, -1)) {
            // Update configuration values
            lua_pushinteger(L, config->viewport_width);
            lua_setfield(L, -2, "viewport_width");
            
            lua_pushinteger(L, config->viewport_height);
            lua_setfield(L, -2, "viewport_height");
            
            lua_pushinteger(L, config->default_font_size);
            lua_setfield(L, -2, "default_font_size");
            
            lua_pushnumber(L, config->line_height_multiplier);
            lua_setfield(L, -2, "line_height_multiplier");
            
            lua_pushinteger(L, config->margin_between_elements);
            lua_setfield(L, -2, "margin_between_elements");
            
            lua_pushinteger(L, config->padding_inside_elements);
            lua_setfield(L, -2, "padding_inside_elements");
            
            printf("[LUA] Config set: %dx%d, font: %d, line_mult: %.2f\n", 
                   config->viewport_width, config->viewport_height,
                   config->default_font_size, config->line_height_multiplier);
        } else {
            printf("[LUA WARNING] config table not found in module\n");
        }
        
        lua_pop(L, 1);  // Pop config table
    } else {
        printf("[LUA ERROR] Global 'calculate_positions' not found\n");
    }
    
    lua_pop(L, 1);  // Pop module table
}

// Calculate positions using Lua
cJSON* calculate_positions_lua(lua_State* L, cJSON* document_json, LuaLayoutConfig* config) {
    if (!L || !document_json) {
        printf("[LUA ERROR] Invalid parameters to calculate_positions_lua\n");
        return NULL;
    }
    
    printf("Calculating positions with Lua...\n");
    
    // Convert cJSON to string
    char* json_str = cJSON_PrintUnformatted(document_json);
    if (!json_str) {
        fprintf(stderr, "ERROR: Cannot convert JSON to string\n");
        return NULL;
    }
    
    printf("[LUA] JSON string length: %zu chars\n", strlen(json_str));
    if (strlen(json_str) < 100) {
        printf("[LUA] JSON preview: %.100s...\n", json_str);
    }
    
    // Get the calculate_positions function from Lua
    lua_getglobal(L, "calculate_positions");
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "ERROR: Global 'calculate_positions' is not a table\n");
        free(json_str);
        return NULL;
    }
    
    lua_getfield(L, -1, "calculate_positions");
    if (!lua_isfunction(L, -1)) {
        fprintf(stderr, "ERROR: calculate_positions function not found in Lua module\n");
        lua_pop(L, 2);  // Pop function check and module table
        free(json_str);
        return NULL;
    }
    
    printf("[LUA] Found calculate_positions function\n");
    
    // Push configuration if provided
    if (config) {
        push_layout_config(L, config);
    }
    
    // Push JSON string as argument
    lua_pushstring(L, json_str);
    free(json_str);
    
    printf("[LUA] Calling Lua function...\n");
    
    // Call the function (1 argument, 1 result)
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        const char* error_msg = lua_tostring(L, -1);
        fprintf(stderr, "ERROR calling Lua function: %s\n", error_msg);
        print_lua_stack(L, "PCALL ERROR");
        lua_pop(L, 1);  // Pop error message
        return NULL;
    }
    
    printf("[LUA] Function call successful\n");
    
    // Get the result string
    if (!lua_isstring(L, -1)) {
        fprintf(stderr, "ERROR: Lua function didn't return a string (got %s)\n",
                lua_typename(L, lua_type(L, -1)));
        print_lua_stack(L, "WRONG RETURN TYPE");
        lua_pop(L, 1);
        return NULL;
    }
    
    const char* result_str = lua_tostring(L, -1);
    printf("[LUA] Result string length: %zu chars\n", strlen(result_str));
    
    if (strlen(result_str) < 200) {
        printf("[LUA] Result preview: %.200s...\n", result_str);
    }
    
    // Parse result back to cJSON
    cJSON* result_json = cJSON_Parse(result_str);
    
    // Pop result from stack
    lua_pop(L, 1);
    
    if (!result_json) {
        fprintf(stderr, "ERROR: Cannot parse Lua result as JSON\n");
        fprintf(stderr, "Result was: %.500s...\n", result_str);
    } else {
        printf("Position calculation completed successfully\n");
        
        // Print summary
        cJSON* doc_width = cJSON_GetObjectItem(result_json, "document_width");
        cJSON* doc_height = cJSON_GetObjectItem(result_json, "document_height");
        if (doc_width && doc_height) {
            printf("Document dimensions: %dx%d\n", 
                   (int)doc_width->valuedouble, 
                   (int)doc_height->valuedouble);
        }
    }
    
    return result_json;
}

// Calculate positions from JSON string
cJSON* calculate_positions_from_json_string(lua_State* L, const char* json_str, LuaLayoutConfig* config) {
    if (!L || !json_str) {
        return NULL;
    }
    
    // Parse JSON string
    cJSON* document_json = cJSON_Parse(json_str);
    if (!document_json) {
        fprintf(stderr, "ERROR: Invalid JSON string\n");
        return NULL;
    }
    
    // Calculate positions
    cJSON* result = calculate_positions_lua(L, document_json, config);
    
    // Cleanup
    cJSON_Delete(document_json);
    
    return result;
}



void merge_lua_positions_fixed(cJSON* main_output, cJSON* lua_output) {
    printf("\n=== MERGING LUA POSITIONS (FIXED) ===\n");
    
    if (!main_output || !lua_output) {
        printf("ERROR: Invalid parameters\n");
        return;
    }
    
    // Get Lua elements array
    cJSON* lua_elements = cJSON_GetObjectItem(lua_output, "elements");
    if (!lua_elements || !cJSON_IsArray(lua_elements)) {
        printf("ERROR: No elements array in Lua output\n");
        return;
    }
    
    int lua_count = cJSON_GetArraySize(lua_elements);
    printf("Lua has %d positioned elements\n", lua_count);
    
    // Build lookup table for faster matching
    typedef struct {
        char tag[50];
        char id[100];
        char type[50];
        cJSON* element;
    } LuaElement;
    
    LuaElement* lua_lookup = malloc(sizeof(LuaElement) * lua_count);
    int lookup_count = 0;
    
    for (int i = 0; i < lua_count; i++) {
        cJSON* lua_elem = cJSON_GetArrayItem(lua_elements, i);
        if (!lua_elem) continue;
        
        cJSON* tag = cJSON_GetObjectItem(lua_elem, "tag");
        cJSON* id = cJSON_GetObjectItem(lua_elem, "id");
        cJSON* type = cJSON_GetObjectItem(lua_elem, "type");
        
        if (tag && cJSON_IsString(tag)) {
            // Store tag
            strncpy(lua_lookup[lookup_count].tag, tag->valuestring, 
                   sizeof(lua_lookup[0].tag) - 1);
            lua_lookup[lookup_count].tag[sizeof(lua_lookup[0].tag) - 1] = '\0';
            
            // Store id (if exists)
            if (id && cJSON_IsString(id)) {
                strncpy(lua_lookup[lookup_count].id, id->valuestring,
                       sizeof(lua_lookup[0].id) - 1);
                lua_lookup[lookup_count].id[sizeof(lua_lookup[0].id) - 1] = '\0';
            } else {
                lua_lookup[lookup_count].id[0] = '\0';
            }
            
            // Store type (if exists)
            if (type && cJSON_IsString(type)) {
                strncpy(lua_lookup[lookup_count].type, type->valuestring,
                       sizeof(lua_lookup[0].type) - 1);
                lua_lookup[lookup_count].type[sizeof(lua_lookup[0].type) - 1] = '\0';
            } else {
                lua_lookup[lookup_count].type[0] = '\0';
            }
            
            lua_lookup[lookup_count].element = lua_elem;
            lookup_count++;
        }
    }
    
    printf("Built lookup table with %d entries\n", lookup_count);
    
    // Recursive function to merge positions
    void merge_element_recursive(cJSON* element) {
        if (!element) return;
        
        cJSON* tag = cJSON_GetObjectItem(element, "tag");
        cJSON* id = cJSON_GetObjectItem(element, "id");
        cJSON* type = cJSON_GetObjectItem(element, "type");
        
        if (!tag || !cJSON_IsString(tag)) return;
        
        const char* tag_str = tag->valuestring;
        const char* id_str = id && cJSON_IsString(id) ? id->valuestring : "";
        const char* type_str = type && cJSON_IsString(type) ? type->valuestring : "";
        
        // Find best matching Lua element
        cJSON* matched_lua = NULL;
        int match_score = 0;  // Higher score = better match
        
        for (int i = 0; i < lookup_count; i++) {
            int score = 0;
            
            // Check tag match (required)
            if (strcmp(lua_lookup[i].tag, tag_str) != 0) continue;
            score += 10;  // Base score for tag match
            
            // Check ID match (very strong)
            if (strlen(id_str) > 0 && strlen(lua_lookup[i].id) > 0 &&
                strcmp(lua_lookup[i].id, id_str) == 0) {
                score += 50;
            }
            
            // Check type match (strong)
            if (strlen(type_str) > 0 && strlen(lua_lookup[i].type) > 0 &&
                strcmp(lua_lookup[i].type, type_str) == 0) {
                score += 30;
            }
            
            // Keep the best match
            if (score > match_score) {
                match_score = score;
                matched_lua = lua_lookup[i].element;
            }
        }
        
        if (matched_lua && match_score >= 10) {  // At least tag match
            // Get position data from Lua
            cJSON* x = cJSON_GetObjectItem(matched_lua, "x");
            cJSON* y = cJSON_GetObjectItem(matched_lua, "y");
            cJSON* width = cJSON_GetObjectItem(matched_lua, "calculated_width");
            cJSON* height = cJSON_GetObjectItem(matched_lua, "calculated_height");
            
            // Update position data
            if (x) {
                cJSON_DeleteItemFromObject(element, "x");
                cJSON_AddNumberToObject(element, "x", x->valuedouble);
            }
            if (y) {
                cJSON_DeleteItemFromObject(element, "y");
                cJSON_AddNumberToObject(element, "y", y->valuedouble);
            }
            if (width) {
                cJSON_DeleteItemFromObject(element, "calculated_width");
                cJSON_AddNumberToObject(element, "calculated_width", width->valuedouble);
            }
            if (height) {
                cJSON_DeleteItemFromObject(element, "calculated_height");
                cJSON_AddNumberToObject(element, "calculated_height", height->valuedouble);
            }
            
            printf("  Merged: %s", tag_str);
            if (strlen(id_str) > 0) printf(" (id=%s)", id_str);
            if (strlen(type_str) > 0) printf(" [type=%s]", type_str);
            printf(" score=%d\n", match_score);
        }
        
        // Recursively process children
        cJSON* children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            for (int i = 0; i < cJSON_GetArraySize(children); i++) {
                merge_element_recursive(cJSON_GetArrayItem(children, i));
            }
        }
    }
    
    // Start merging from root
    if (cJSON_IsArray(main_output)) {
        printf("Processing array structure...\n");
        for (int i = 0; i < cJSON_GetArraySize(main_output); i++) {
            merge_element_recursive(cJSON_GetArrayItem(main_output, i));
        }
    } else {
        printf("Processing single element...\n");
        merge_element_recursive(main_output);
    }
    
    free(lua_lookup);
    printf("=== MERGE COMPLETE ===\n");
}

// Cleanup
void cleanup_lua_position_calculator(lua_State* L) {
    if (L) {
        printf("Cleaning up Lua position calculator...\n");
        lua_close(L);
        printf("Lua position calculator cleaned up\n");
    }
}




// Helper function to read a JSON file
cJSON* read_json_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("ERROR: Cannot open JSON file: %s\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    cJSON* json = cJSON_Parse(content);
    free(content);
    
    if (!json) {
        printf("ERROR: Invalid JSON in file: %s\n", filename);
    }
    
    return json;
}

// Helper function to save and merge Lua output
void save_and_merge_lua_output(cJSON* rendering_output, cJSON* lua_output, const char* output_file) {
    if (!rendering_output || !lua_output) return;
    
    printf("=== MERGING LUA POSITIONS ===\n");
    
    // 1. Save raw Lua output
    char lua_raw_file[512];
    snprintf(lua_raw_file, sizeof(lua_raw_file), "%s_lua_raw.json", output_file);
    
    FILE* raw_file = fopen(lua_raw_file, "wb");
    if (raw_file) {
        char* raw_json = cJSON_Print(lua_output);
        fwrite(raw_json, 1, strlen(raw_json), raw_file);
        free(raw_json);
        fclose(raw_file);
        printf("Raw Lua output saved to: %s\n", lua_raw_file);
        kopiraj_fajl(lua_raw_file);
    }
    
    // 2. Extract Lua elements
    cJSON* lua_elements = cJSON_GetObjectItem(lua_output, "elements");
    if (!lua_elements || !cJSON_IsArray(lua_elements)) {
        printf("ERROR: No elements array in Lua output\n");
        return;
    }
    
    int lua_count = cJSON_GetArraySize(lua_elements);
    printf("Lua output has %d elements\n", lua_count);
    
    // 3. Build lookup table for Lua elements
    typedef struct {
        char tag[50];
        char id[100];
        cJSON* element;
    } LuaElementLookup;
    
    LuaElementLookup* lookup = malloc(sizeof(LuaElementLookup) * lua_count);
    int lookup_count = 0;
    
    for (int i = 0; i < lua_count; i++) {
        cJSON* lua_elem = cJSON_GetArrayItem(lua_elements, i);
        if (!lua_elem) continue;
        
        cJSON* tag = cJSON_GetObjectItem(lua_elem, "tag");
        cJSON* id = cJSON_GetObjectItem(lua_elem, "id");
        
        if (tag && cJSON_IsString(tag)) {
            strncpy(lookup[lookup_count].tag, tag->valuestring, sizeof(lookup[0].tag)-1);
            lookup[lookup_count].tag[sizeof(lookup[0].tag)-1] = '\0';
            
            if (id && cJSON_IsString(id)) {
                strncpy(lookup[lookup_count].id, id->valuestring, sizeof(lookup[0].id)-1);
                lookup[lookup_count].id[sizeof(lookup[0].id)-1] = '\0';
            } else {
                lookup[lookup_count].id[0] = '\0';
            }
            
            lookup[lookup_count].element = lua_elem;
            lookup_count++;
        }
    }
    
    printf("Built lookup table with %d entries\n", lookup_count);
    
    // 4. Recursive function to merge positions
    void merge_element(cJSON* element) {
        if (!element) return;
        
        cJSON* tag = cJSON_GetObjectItem(element, "tag");
        cJSON* id = cJSON_GetObjectItem(element, "id");
        
        if (!tag || !cJSON_IsString(tag)) return;
        
        const char* tag_str = tag->valuestring;
        const char* id_str = id ? id->valuestring : "";
        
        // Find matching Lua element
        cJSON* matched_lua = NULL;
        for (int i = 0; i < lookup_count; i++) {
            if (strcmp(lookup[i].tag, tag_str) == 0) {
                if (strlen(id_str) > 0 && strlen(lookup[i].id) > 0) {
                    if (strcmp(lookup[i].id, id_str) == 0) {
                        matched_lua = lookup[i].element;
                        break;
                    }
                } else {
                    // Match by tag only (first match)
                    if (!matched_lua) {
                        matched_lua = lookup[i].element;
                    }
                }
            }
        }
        
        if (matched_lua) {
            // Get position data from Lua
            cJSON* x = cJSON_GetObjectItem(matched_lua, "x");
            cJSON* y = cJSON_GetObjectItem(matched_lua, "y");
            cJSON* width = cJSON_GetObjectItem(matched_lua, "calculated_width");
            cJSON* height = cJSON_GetObjectItem(matched_lua, "calculated_height");
            
            // Update or add position data
            if (x) {
                cJSON_DeleteItemFromObject(element, "x");
                cJSON_AddNumberToObject(element, "x", x->valuedouble);
            }
            if (y) {
                cJSON_DeleteItemFromObject(element, "y");
                cJSON_AddNumberToObject(element, "y", y->valuedouble);
            }
            if (width) {
                cJSON_DeleteItemFromObject(element, "calculated_width");
                cJSON_AddNumberToObject(element, "calculated_width", width->valuedouble);
            }
            if (height) {
                cJSON_DeleteItemFromObject(element, "calculated_height");
                cJSON_AddNumberToObject(element, "calculated_height", height->valuedouble);
            }
            
            printf("  Merged positions for: %s", tag_str);
            if (strlen(id_str) > 0) printf(" (id=%s)", id_str);
            printf("\n");
        }
        
        // Recursively process children
        cJSON* children = cJSON_GetObjectItem(element, "children");
        if (children && cJSON_IsArray(children)) {
            for (int i = 0; i < cJSON_GetArraySize(children); i++) {
                merge_element(cJSON_GetArrayItem(children, i));
            }
        }
    }
    
    // 5. Start merging from root
    if (cJSON_IsArray(rendering_output)) {
        for (int i = 0; i < cJSON_GetArraySize(rendering_output); i++) {
            merge_element(cJSON_GetArrayItem(rendering_output, i));
        }
    } else {
        merge_element(rendering_output);
    }
    
    free(lookup);
    printf("=== MERGE COMPLETE ===\n");
}