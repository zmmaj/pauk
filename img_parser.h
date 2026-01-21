#ifndef IMG_PARSER_H
#define IMG_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>


cJSON* parse_srcset(const char *srcset_str);
void calculate_image_dimensions(cJSON *image_json);

#endif // IMG_PARSER_H