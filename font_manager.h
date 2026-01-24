#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <stdbool.h>
#include <vfs/vfs.h>
#include <errno.h>
#include <str_error.h>
#include <dirent.h>
#include "stb_truetype.h"

#define MAX_FONTS 50
#define MAX_FONT_NAME_LEN 64
#define FONT_CACHE_SIZE 256

// ADD THIS STRUCTURE DEFINITION:
typedef struct {
    const char *requested_font;
    const char *substitute_font;
    int priority;
} font_substitution_t;

typedef struct {
    char name[MAX_FONT_NAME_LEN];
    char path[256];
    unsigned char *font_data;
    size_t font_size;
    stbtt_fontinfo info;
    bool is_loaded;
    
    // Metrics cache for common sizes
    struct {
        int size;
        float scale;
        int ascent, descent, linegap;
        int baseline, line_height;
        int space_width;
    } metrics[10]; // Cache for 10 different sizes
} html_font_t;

typedef struct {
    html_font_t fonts[MAX_FONTS];
    int font_count;
    int default_font_index;
    
    // Font family mappings
    struct {
        const char *family;
        int font_index;
    } family_map[20];
    int family_count;
    
    // ADD THIS LINE (the one that was causing the error):
    font_substitution_t font_substitutions[50];
    int substitution_count;
} font_manager_t;

// Public API
errno_t font_manager_init(font_manager_t *manager);
errno_t font_manager_load_fonts(font_manager_t *manager, const char *font_list_path);
void font_manager_destroy(font_manager_t *manager);

// Font selection
int font_manager_get_font_for_family(font_manager_t *manager, const char *family, 
                                   bool bold, bool italic);
html_font_t* font_manager_get_font(font_manager_t *manager, int index);

// Text measurement
int font_manager_text_width(font_manager_t *manager, int font_index, int size, 
                          const char *text, int length);
int font_manager_line_height(font_manager_t *manager, int font_index, int size);
int font_manager_find_best_match(font_manager_t *manager, const char *requested_font);

void font_manager_init_substitutions(font_manager_t *manager);
int font_manager_select_from_list(font_manager_t *manager, const char *font_list);

#endif