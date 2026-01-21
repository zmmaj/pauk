// lua_position.h - C interface to Lua position calculator
#ifndef LUA_POSITION_H
#define LUA_POSITION_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "cjson.h"

// Structure to hold layout configuration for Lua
typedef struct {
    int viewport_width;
    int viewport_height;
    int default_font_size;
    float line_height_multiplier;
    int margin_between_elements;
    int padding_inside_elements;
} LuaLayoutConfig;

// Initialize Lua and load position calculation script
lua_State* init_lua_position_calculator(const char* lua_script_path);

// Calculate positions using Lua
cJSON* calculate_positions_lua(lua_State* L, cJSON* document_json, LuaLayoutConfig* config);

// Calculate positions from JSON string
cJSON* calculate_positions_from_json_string(lua_State* L, const char* json_str, LuaLayoutConfig* config);

// Cleanup
void cleanup_lua_position_calculator(lua_State* L);

void merge_lua_positions_fixed(cJSON* main_output, cJSON* lua_output);

void save_and_merge_lua_output(cJSON* rendering_output, cJSON* lua_output, const char* output_file);

cJSON* read_json_file(const char* filename);

#endif // LUA_POSITION_H