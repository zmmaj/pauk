#ifndef IMG_PARSER_H
#define IMG_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>


cJSON* parse_srcset(const char *srcset_str);
void calculate_image_dimensions(cJSON *image_json);

int is_binary_image_data(const unsigned char *buffer, size_t size);
int is_binary_data(const unsigned char *buffer, size_t size);

#endif // IMG_PARSER_H
