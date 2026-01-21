
#ifndef RENDER_OUTPUT_H
#define RENDER_OUTPUT_H

#include "layout_calculator.h"
#include "cjson.h"
#include <lexbor/dom/dom.h>

cJSON* clean_element_for_render(cJSON *element);

cJSON* create_final_render_file(const char *main_output_file, 
    const char **extracted_files, 
    int file_count,
    LayoutConfig config);

#endif // RENDER_OUTPUT_H