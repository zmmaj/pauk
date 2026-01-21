
#ifndef MEDIA_PARSER_H
#define MEDIA_PARSER_H

#include "cjson.h"
#include <lexbor/dom/dom.h>

void calculate_media_dimensions(cJSON *media_json);

#endif // MEDIA_PARSER_H