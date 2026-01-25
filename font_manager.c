// font_manager.c
#include "gui.h"
#include "font_manager.h"
#include <str.h>
#include <mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <vfs/vfs.h>


#include "stb_truetype.h"

static bool is_font_file(const char *filename) {
    const char *ext = str_rchr(filename, '.');
    return ext && (str_cmp(ext, ".ttf") == 0 || str_cmp(ext, ".otf") == 0);
}

static const font_substitution_t default_substitutions[] = {
    {"Arial", "arial.ttf", 10},
    {"Helvetica", "arial.ttf", 10},
    {"Times New Roman", "times.ttf", 10},
    {"Times", "times.ttf", 10},
    {"Courier New", "cour.ttf", 10},
    {"Courier", "cour.ttf", 10},
    {"Verdana", "arial.ttf", 8},           // Use FreeSans as Verdana substitute
    {"Tahoma", "arial.ttf", 7},
    {"Georgia", "times.ttf", 9},           // Use FreeSerif as Georgia
    {"Palatino", "times.ttf", 8},
    {"Garamond", "times.ttf", 8},
    {"Comic Sans MS", "BananasItalic.ttf", 3},
    {"Impact", "Oswald.ttf", 3},
    {"Lucida Console", "cour.ttf", 5},
    {"Trebuchet MS", "arial.ttf", 6},
    {"monospace", "cour.ttf", 10},
    {"serif", "times.ttf", 10},
    {"sans-serif", "arial.ttf", 10},
    {NULL, NULL, 0}
};

// Initialize font substitutions
void font_manager_init_substitutions(font_manager_t *manager) {
    manager->substitution_count = 0;
    
    printf("[FONT-SUBSTITUTIONS] Initializing substitution table...\n");
    
    for (int i = 0; default_substitutions[i].requested_font != NULL; i++) {
        if (manager->substitution_count < 50) {
            manager->font_substitutions[manager->substitution_count] = default_substitutions[i];
            printf("[FONT-SUBSTITUTIONS] %s -> %s (priority: %d)\n",
                   default_substitutions[i].requested_font,
                   default_substitutions[i].substitute_font,
                   default_substitutions[i].priority);
            manager->substitution_count++;
        }
    }
    
    printf("[FONT-SUBSTITUTIONS] Total substitutions: %d\n", manager->substitution_count);
}

// Find best font match
int font_manager_find_best_match(font_manager_t *manager, const char *requested_font) {
    if (!requested_font) return manager->default_font_index;
    
    printf("[FONT-MATCH] '%s'\n", requested_font);
    
    // 1. Try exact match
    for (int i = 0; i < manager->font_count; i++) {
        if (str_casecmp(manager->fonts[i].name, requested_font) == 0) {
            printf("[FONT-MATCH] ✅ Exact\n");
            return i;
        }
    }
    
    // 2. Try without extension  
    for (int i = 0; i < manager->font_count; i++) {
        char name_no_ext[MAX_FONT_NAME_LEN];
        str_cpy(name_no_ext, sizeof(name_no_ext), manager->fonts[i].name);
        char *dot = str_rchr(name_no_ext, '.');
        if (dot) *dot = '\0';
        
        if (str_casecmp(name_no_ext, requested_font) == 0) {
            printf("[FONT-MATCH] ✅ No-ext\n");
            return i;
        }
    }
    
    // 3. Use substitution table
    for (int i = 0; i < manager->substitution_count; i++) {
        if (str_casecmp(manager->font_substitutions[i].requested_font, requested_font) == 0) {
            printf("[FONT-MATCH] ✅ Sub: %s\n", manager->font_substitutions[i].substitute_font);
            
            // Find the substitute font
            for (int j = 0; j < manager->font_count; j++) {
                if (str_casecmp(manager->fonts[j].name, manager->font_substitutions[i].substitute_font) == 0) {
                    return j;
                }
            }
            break;
        }
    }
    
    printf("[FONT-MATCH] ❌ Default\n");
    return manager->default_font_index;
}

static void font_manager_add_family_mapping(font_manager_t *manager, 
    const char *family, const char *font_name) {
    if (manager->family_count >= 20) return;

    // Find the font index for this font name
    for (int i = 0; i < manager->font_count; i++) {
        if (str_casecmp(manager->fonts[i].name, font_name) == 0) {
            manager->family_map[manager->family_count].family = family;
            manager->family_map[manager->family_count].font_index = i;
            manager->family_count++;
            return;
        }
    }
}

errno_t font_manager_init(font_manager_t *manager) {
    memset(manager, 0, sizeof(font_manager_t));
    manager->default_font_index = -1;
    
    // Setup default font family mappings
    // Setup default font family mappings with FreeFont files
    font_manager_add_family_mapping(manager, "sans-serif", "arial.ttf");  // FreeSans
    font_manager_add_family_mapping(manager, "serif", "times.ttf");       // FreeSerif  
    font_manager_add_family_mapping(manager, "monospace", "cour.ttf");    // FreeMono
    font_manager_add_family_mapping(manager, "Arial", "arial.ttf");
    font_manager_add_family_mapping(manager, "Helvetica", "arial.ttf");
    font_manager_add_family_mapping(manager, "Times", "times.ttf");
    font_manager_add_family_mapping(manager, "Courier", "cour.ttf");
    
    return EOK;
}

errno_t font_manager_load_fonts(font_manager_t *manager, const char *font_directory) {
    const char *font_dir = "/data/font/";
    DIR *dir = opendir(font_dir);
    if (!dir) {
        printf("Cannot open font directory: %s\n", font_dir);
        return errno;
    }

    manager->font_count = 0;
    struct dirent *ent;
    
    while ((ent = readdir(dir)) && manager->font_count < MAX_FONTS) {
        if (!is_font_file(ent->d_name)) continue;

        // Check filename length to prevent buffer overflow
        size_t name_len = str_length(ent->d_name);
        if (name_len > 200) {  // Reasonable limit for font filenames
            printf("Font filename too long, skipping: %s\n", ent->d_name);
            continue;
        }

        // Increased buffer size for safety
        char fullpath[512];  // Increased from 256 to 512
        int written = snprintf(fullpath, sizeof(fullpath), "/data/font/%s", ent->d_name);
        
        // Check if the path was truncated
        if (written < 0 || (size_t)written >= sizeof(fullpath)) {
            printf("Font path too long, skipping: %s\n", ent->d_name);
            continue;
        }

        // Load font file
        FILE *font_file = fopen(fullpath, "rb");
        if (!font_file) {
            printf("Cannot open font file: %s\n", fullpath);
            continue;
        }

        // Get file size and read data
        fseek(font_file, 0, SEEK_END);
        size_t file_size = ftell(font_file);
        fseek(font_file, 0, SEEK_SET);

        // Check if font file is reasonable size (under 10MB)
        if (file_size > 10 * 1024 * 1024) {
            printf("Font file too large (%zu bytes), skipping: %s\n", file_size, ent->d_name);
            fclose(font_file);
            continue;
        }

        unsigned char *font_data = malloc(file_size);
        if (!font_data) {
            fclose(font_file);
            printf("Memory allocation failed for font: %s\n", ent->d_name);
            continue;
        }

        size_t bytes_read = fread(font_data, 1, file_size, font_file);
        fclose(font_file);
        
        if (bytes_read != file_size) {
            free(font_data);
            printf("Failed to read font file: %s (read %zu of %zu bytes)\n", 
                   ent->d_name, bytes_read, file_size);
            continue;
        }

        // Initialize font
        html_font_t *font = &manager->fonts[manager->font_count];
        stbtt_fontinfo *info = &font->info;
        
        if (!stbtt_InitFont(info, font_data, 0)) {
            free(font_data);
            printf("Failed to initialize TTF font: %s\n", ent->d_name);
            continue;
        }

        // Validate font metrics
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &linegap);
        
        if (ascent <= 0 || descent >= 0) {
            free(font_data);
            printf("Invalid font metrics for: %s (ascent=%d, descent=%d)\n", ent->d_name, ascent, descent);
            continue;
        }

        // Store font information
        str_cpy(font->name, MAX_FONT_NAME_LEN, ent->d_name);
        str_cpy(font->path, sizeof(font->path), fullpath);
        font->font_data = font_data;
        font->font_size = file_size;
        font->is_loaded = true;

        // Set as default if it's Arial
        if (manager->default_font_index == -1 && 
            (str_casecmp(ent->d_name, "arial.ttf") == 0 || str_str(ent->d_name, "arial"))) {
            manager->default_font_index = manager->font_count;
        }

        manager->font_count++;
    }

    closedir(dir);

    // Set default font if none was set
    if (manager->default_font_index == -1 && manager->font_count > 0) {
        manager->default_font_index = 0;
    }

    printf("Loaded %d fonts from %s\n", manager->font_count, font_dir);
    printf("[FONT] FINAL: Loaded %d fonts, default index: %d\n", 
        manager->font_count, manager->default_font_index);
 if (manager->default_font_index >= 0 && manager->default_font_index < manager->font_count) {
     printf("[FONT] Default font: %s\n", manager->fonts[manager->default_font_index].name);
 } else {
     printf("[FONT] ❌ INVALID default index!\n");
 }
    return EOK;
}

void font_manager_destroy(font_manager_t *manager) {
    for (int i = 0; i < manager->font_count; i++) {
        if (manager->fonts[i].font_data) {
            free(manager->fonts[i].font_data);
        }
    }
    memset(manager, 0, sizeof(font_manager_t));
}

int font_manager_get_font_for_family(font_manager_t *manager, const char *family, 
                                   bool bold, bool italic) {
    if (!family || !*family) {
        return manager->default_font_index;
    }

    // Simple font family resolution
    for (int i = 0; i < manager->family_count; i++) {
        if (str_casecmp(manager->family_map[i].family, family) == 0) {
            return manager->family_map[i].font_index;
        }
    }

    // Fallback: search in font names
    for (int i = 0; i < manager->font_count; i++) {
        if (str_casecmp(manager->fonts[i].name, family) == 0) {
            return i;
        }
        // Check without extension
        char name_no_ext[MAX_FONT_NAME_LEN];
        str_cpy(name_no_ext, sizeof(name_no_ext), manager->fonts[i].name);
        char *dot = str_rchr(name_no_ext, '.');
        if (dot) *dot = '\0';
        
        if (str_casecmp(name_no_ext, family) == 0) {
            return i;
        }
    }

    return manager->default_font_index;
}

html_font_t* font_manager_get_font(font_manager_t *manager, int index) {
    if (index < 0 || index >= manager->font_count) {
        return &manager->fonts[manager->default_font_index];
    }
    return &manager->fonts[index];
}

int font_manager_text_width(font_manager_t *manager, int font_index, int size, 
                          const char *text, int length) {
    if (length <= 0) return 0;
    
    html_font_t *font = font_manager_get_font(manager, font_index);
    if (!font || !font->is_loaded) {
        return str_length(text) * 8; // Fallback
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font->info, size);
    
    int width = 0;
    const char *p = text;
    int chars_processed = 0;
    
    while (*p && chars_processed < length) {
        int codepoint = (unsigned char)*p++;
        chars_processed++;
        
        // Basic ASCII handling - extend for UTF-8 later
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        width += (int)(advance * scale);
        
        // Add kerning for next character
        if (*p && chars_processed < length) {
            int kern = stbtt_GetCodepointKernAdvance(&font->info, codepoint, (unsigned char)*p);
            width += (int)(kern * scale);
        }
    }
    
    return width;
}

int font_manager_line_height(font_manager_t *manager, int font_index, int size) {
    html_font_t *font = font_manager_get_font(manager, font_index);
    if (!font || !font->is_loaded) {
        return size + 4; // Fallback
    }
    
    float scale = stbtt_ScaleForPixelHeight(&font->info, size);
    
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &linegap);
    
    return (int)((ascent - descent + linegap) * scale);
}


int font_manager_select_from_list(font_manager_t *manager, const char *font_list) {
    if (!font_list || !*font_list) {
        return manager->default_font_index;
    }
    
    printf("[FONT] Selecting font from list: %s\n", font_list);
    
    // Make a copy for tokenization
    char *list_copy = str_dup(font_list);
    if (!list_copy) return manager->default_font_index;
    
    char *current_font = list_copy;
    char *comma_pos;
    int selected_font = -1;
    
    // Iterate through comma-separated font list
    do {
        comma_pos = str_chr(current_font, ',');
        if (comma_pos) {
            *comma_pos = '\0'; // Temporarily terminate at comma
        }
        
        // Trim whitespace from current font name
        char *font_name = str_trim(current_font);
        
        // Remove quotes if present
        if (font_name[0] == '"' || font_name[0] == '\'') {
            size_t len = str_length(font_name);
            if (len > 2 && font_name[len-1] == font_name[0]) {
                font_name[len-1] = '\0';
                font_name++;
            }
        }
        
        printf("[FONT] Trying font: '%s'\n", font_name);
        
        // USE YOUR EXISTING FONT MATCHING FUNCTION
        selected_font = font_manager_find_best_match(manager, font_name);
        
        // Check if we found a valid font (not just the default fallback)
        if (selected_font != manager->default_font_index) {
            printf("[FONT] ✓ Found font: '%s' at index %d\n", font_name, selected_font);
            break;
        } else {
            printf("[FONT] ✗ Font not available: '%s' (using default index %d)\n", 
                   font_name, selected_font);
        }
        
        // Move to next font in list
        if (comma_pos) {
            current_font = comma_pos + 1;
        }
    } while (comma_pos);
    
    free(list_copy);
    
    // If we found a specific font in the list, use it
    if (selected_font != -1 && selected_font != manager->default_font_index) {
        return selected_font;
    }
    
    // Otherwise use default
    printf("[FONT] No suitable font found in list, using default\n");
    return manager->default_font_index;
}


html_font_t* font_manager_get_by_name(font_manager_t *manager, const char *css_font_family) {
    if (!manager || !css_font_family || manager->font_count == 0) {
        return NULL;
    }
    
    // Debug output
    if (DEB_FONT) printf("[FONT] Looking for: '%s'\n", css_font_family);
    
    // 1. FIRST: Check your pre-defined family_map (fastest path)
    for (int i = 0; i < manager->family_count; i++) {
        if (strcasecmp(manager->family_map[i].family, css_font_family) == 0) {
            int font_index = manager->family_map[i].font_index;
            
            if (font_index >= 0 && font_index < manager->font_count) {
                html_font_t *font = &manager->fonts[font_index];
                
                if (font->is_loaded) {
                    if (DEB_FONT) printf("[FONT] Found in family_map: '%s' -> index %d (%s)\n",
                                        css_font_family, font_index, font->name);
                    return font;
                }
            }
            break; // Found in map but font not loaded/valid
        }
    }
    
    // 2. Check loaded fonts by name (from font->name field)
    for (int i = 0; i < manager->font_count; i++) {
        html_font_t *font = &manager->fonts[i];
        
        if (font->is_loaded && font->name[0] != '\0') {
            // Exact name match (case-insensitive)
            if (strcasecmp(font->name, css_font_family) == 0) {
                if (DEB_FONT) printf("[FONT] Exact name match: %s\n", font->name);
                return font;
            }
            
            // Partial match (CSS "Arial" matches font name "Arial Regular")
            if (str_casestr(font->name, css_font_family)) {
                if (DEB_FONT) printf("[FONT] Partial match: '%s' in %s\n", 
                                    css_font_family, font->name);
                return font;
            }
        }
    }
    
    // 3. Common aliases for your 3 FreeFont files
    // Map common CSS names to your loaded font indices
    struct {
        const char *css_name;
        int font_index;
    } common_aliases[] = {
        {"helvetica", 0},       // arial.ttf
        {"verdana", 0},         // arial.ttf  
        {"tahoma", 0},          // arial.ttf
        {"geneva", 0},          // arial.ttf
        {"times new roman", 1}, // times.ttf
        {"georgia", 1},         // times.ttf
        {"courier new", 2},     // cour.ttf
        {"consolas", 2},        // cour.ttf
        {"monaco", 2},          // cour.ttf
        {NULL, -1}
    };
    
    for (int i = 0; common_aliases[i].css_name; i++) {
        if (str_casestr(css_font_family, common_aliases[i].css_name)) {
            int font_index = common_aliases[i].font_index;
            
            if (font_index >= 0 && font_index < manager->font_count) {
                html_font_t *font = &manager->fonts[font_index];
                
                if (font->is_loaded) {
                    if (DEB_FONT) printf("[FONT] Common alias: '%s' -> %s\n",
                                        css_font_family, font->name);
                    return font;
                }
            }
            break;
        }
    }
    
    // 4. Generic font families (sans-serif, serif, monospace)
    // These should be in your family_map, but just in case...
    if (strcasecmp(css_font_family, "sans-serif") == 0) {
        // Find arial (index 0)
        if (manager->font_count > 0 && manager->fonts[0].is_loaded) {
            if (DEB_FONT) printf("[FONT] Generic sans-serif -> %s\n", 
                                manager->fonts[0].name);
            return &manager->fonts[0];
        }
    }
    
    if (strcasecmp(css_font_family, "serif") == 0) {
        // Find times (index 1)
        if (manager->font_count > 1 && manager->fonts[1].is_loaded) {
            if (DEB_FONT) printf("[FONT] Generic serif -> %s\n", 
                                manager->fonts[1].name);
            return &manager->fonts[1];
        }
    }
    
    if (strcasecmp(css_font_family, "monospace") == 0) {
        // Find courier (index 2)
        if (manager->font_count > 2 && manager->fonts[2].is_loaded) {
            if (DEB_FONT) printf("[FONT] Generic monospace -> %s\n", 
                                manager->fonts[2].name);
            return &manager->fonts[2];
        }
    }
    
    // 5. DEFAULT FONT FALLBACK (from your default_font_index)
    if (manager->default_font_index >= 0 && 
        manager->default_font_index < manager->font_count) {
        html_font_t *font = &manager->fonts[manager->default_font_index];
        
        if (font->is_loaded) {
            if (DEB_FONT) printf("[FONT] Using default font: %s\n", font->name);
            return font;
        }
    }
    
    // 6. LAST RESORT: First loaded font
    for (int i = 0; i < manager->font_count; i++) {
        if (manager->fonts[i].is_loaded) {
            if (DEB_FONT) printf("[FONT] Using first loaded font: %s\n", 
                                manager->fonts[i].name);
            return &manager->fonts[i];
        }
    }
    
    // No fonts available at all
    if (DEB_FONT) printf("[FONT] ERROR: No fonts available for '%s'\n", css_font_family);
    return NULL;
}
