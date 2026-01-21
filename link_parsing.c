// link_parsing.c - COMPLETE IMPLEMENTATION
#include "link_parsing.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// ========== HELPER FUNCTIONS ==========

static char* lexbor_to_cstr(const lxb_char_t *lb_str, size_t len) {
    if (!lb_str || len == 0) return strdup("");
    
    char *cstr = malloc(len + 1);
    if (!cstr) return NULL;
    
    memcpy(cstr, lb_str, len);
    cstr[len] = '\0';
    return cstr;
}
/*
// Helper function to check if an attribute exists
static int has_attribute(lxb_dom_element_t *elem, const lxb_char_t *name, size_t name_len) {
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(elem, name, name_len);
    return attr != NULL;
}
*/
// ========== ATTRIBUTE PARSING FUNCTIONS ==========

void parse_link_target_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *target = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"target", 6, &attr_len);
    
    if (target && attr_len > 0) {
        char *target_str = lexbor_to_cstr(target, attr_len);
        if (target_str) {
            cJSON_AddStringToObject(link_json, "target", target_str);
            
            // Set specific flags
            if (strcasecmp(target_str, "_blank") == 0) {
                cJSON_AddBoolToObject(link_json, "opens_new_tab", true);
            } else if (strcasecmp(target_str, "_self") == 0) {
                cJSON_AddBoolToObject(link_json, "opens_self", true);
            } else if (strcasecmp(target_str, "_parent") == 0) {
                cJSON_AddBoolToObject(link_json, "opens_parent", true);
            } else if (strcasecmp(target_str, "_top") == 0) {
                cJSON_AddBoolToObject(link_json, "opens_top", true);
            } else {
                // Custom frame/window name
                cJSON_AddBoolToObject(link_json, "opens_named_frame", true);
                cJSON_AddStringToObject(link_json, "frame_name", target_str);
            }
            free(target_str);
        }
    } else {
        cJSON_AddStringToObject(link_json, "target", "_self");
    }
}

void parse_link_rel_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *rel = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"rel", 3, &attr_len);
    
    if (!rel || attr_len == 0) return;
    
    char *rel_str = lexbor_to_cstr(rel, attr_len);
    if (!rel_str) return;
    
    cJSON_AddStringToObject(link_json, "rel", rel_str);
    
    // Parse into array and set individual flags
    cJSON *rel_array = cJSON_CreateArray();
    char *saveptr;
    char *token = strtok_r(rel_str, " \t\n\r", &saveptr);
    int rel_count = 0;
    
    while (token) {
        if (strlen(token) > 0) {
            cJSON_AddItemToArray(rel_array, cJSON_CreateString(token));
            rel_count++;
            
            // Set individual rel flags
            if (strcasecmp(token, "nofollow") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_nofollow", true);
            } else if (strcasecmp(token, "noopener") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_noopener", true);
            } else if (strcasecmp(token, "noreferrer") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_noreferrer", true);
            } else if (strcasecmp(token, "external") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_external", true);
            } else if (strcasecmp(token, "help") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_help", true);
            } else if (strcasecmp(token, "author") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_author", true);
            } else if (strcasecmp(token, "bookmark") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_bookmark", true);
            } else if (strcasecmp(token, "license") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_license", true);
            } else if (strcasecmp(token, "next") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_next", true);
            } else if (strcasecmp(token, "prev") == 0 || 
                       strcasecmp(token, "previous") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_prev", true);
            } else if (strcasecmp(token, "search") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_search", true);
            } else if (strcasecmp(token, "tag") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_tag", true);
            } else if (strcasecmp(token, "canonical") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_canonical", true);
            } else if (strcasecmp(token, "alternate") == 0) {
                cJSON_AddBoolToObject(link_json, "rel_alternate", true);
            }
        }
        token = strtok_r(NULL, " \t\n\r", &saveptr);
    }
    
    if (rel_count > 0) {
        cJSON_AddItemToObject(link_json, "rel_array", rel_array);
        cJSON_AddNumberToObject(link_json, "rel_count", rel_count);
    } else {
        cJSON_Delete(rel_array);
    }
    
    free(rel_str);
}

void parse_link_download_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    lxb_dom_attr_t *download_attr = lxb_dom_element_attr_by_name(
        elem, (lxb_char_t*)"download", 8);
    
    if (!download_attr) return;
    
    cJSON_AddBoolToObject(link_json, "download", true);
    
    // Check if download has a value (filename)
    size_t attr_len;
    const lxb_char_t *download_value = lxb_dom_attr_value(download_attr, &attr_len);
    if (download_value && attr_len > 0) {
        char *filename = lexbor_to_cstr(download_value, attr_len);
        if (filename) {
            cJSON_AddStringToObject(link_json, "download_filename", filename);
            free(filename);
        }
    }
}

void parse_link_title_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *title = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"title", 5, &attr_len);
    
    if (!title || attr_len == 0) return;
    
    char *title_str = lexbor_to_cstr(title, attr_len);
    if (title_str) {
        cJSON_AddStringToObject(link_json, "title", title_str);
        free(title_str);
    }
}

void parse_link_type_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *type_attr = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"type", 4, &attr_len);
    
    if (!type_attr || attr_len == 0) return;
    
    char *type_str = lexbor_to_cstr(type_attr, attr_len);
    if (type_str) {
        cJSON_AddStringToObject(link_json, "mime_type", type_str);
        free(type_str);
    }
}

void parse_link_hreflang_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *hreflang = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"hreflang", 8, &attr_len);
    
    if (!hreflang || attr_len == 0) return;
    
    char *lang_str = lexbor_to_cstr(hreflang, attr_len);
    if (lang_str) {
        cJSON_AddStringToObject(link_json, "hreflang", lang_str);
        free(lang_str);
    }
}

void parse_link_ping_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *ping = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"ping", 4, &attr_len);
    
    if (!ping || attr_len == 0) return;
    
    char *ping_str = lexbor_to_cstr(ping, attr_len);
    if (!ping_str) return;
    
    cJSON_AddStringToObject(link_json, "ping", ping_str);
    
    // Parse ping URLs into array
    cJSON *ping_array = cJSON_CreateArray();
    char *saveptr;
    char *token = strtok_r(ping_str, " \t\n\r", &saveptr);
    int ping_count = 0;
    
    while (token) {
        if (strlen(token) > 0) {
            cJSON_AddItemToArray(ping_array, cJSON_CreateString(token));
            ping_count++;
        }
        token = strtok_r(NULL, " \t\n\r", &saveptr);
    }
    
    if (ping_count > 0) {
        cJSON_AddItemToObject(link_json, "ping_urls", ping_array);
        cJSON_AddNumberToObject(link_json, "ping_count", ping_count);
    } else {
        cJSON_Delete(ping_array);
    }
    
    free(ping_str);
}

void parse_link_referrerpolicy_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *referrerpolicy = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"referrerpolicy", 13, &attr_len);
    
    if (!referrerpolicy || attr_len == 0) return;
    
    char *policy_str = lexbor_to_cstr(referrerpolicy, attr_len);
    if (policy_str) {
        cJSON_AddStringToObject(link_json, "referrerpolicy", policy_str);
        free(policy_str);
    }
}

void parse_link_media_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *media = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"media", 5, &attr_len);
    
    if (!media || attr_len == 0) return;
    
    char *media_str = lexbor_to_cstr(media, attr_len);
    if (media_str) {
        cJSON_AddStringToObject(link_json, "media_query", media_str);
        free(media_str);
    }
}

void parse_link_charset_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *charset = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"charset", 7, &attr_len);
    
    if (!charset || attr_len == 0) return;
    
    char *charset_str = lexbor_to_cstr(charset, attr_len);
    if (charset_str) {
        cJSON_AddStringToObject(link_json, "charset", charset_str);
        cJSON_AddBoolToObject(link_json, "has_deprecated_charset", true);
        free(charset_str);
    }
}

void parse_link_name_attribute(lxb_dom_element_t *elem, cJSON *link_json) {
    size_t attr_len;
    const lxb_char_t *name = lxb_dom_element_get_attribute(
        elem, (lxb_char_t*)"name", 4, &attr_len);
    
    if (!name || attr_len == 0) return;
    
    char *name_str = lexbor_to_cstr(name, attr_len);
    if (name_str) {
        cJSON_AddStringToObject(link_json, "anchor_name", name_str);
        free(name_str);
    }
}

// ========== LINK TYPE DETECTION ==========

int is_mailto_link(const char *href) {
    return href && strncasecmp(href, "mailto:", 7) == 0;
}

int is_tel_link(const char *href) {
    return href && strncasecmp(href, "tel:", 4) == 0;
}

int is_javascript_link(const char *href) {
    return href && strncasecmp(href, "javascript:", 11) == 0;
}

int is_anchor_link(const char *href) {
    return href && href[0] == '#';
}

int is_ftp_link(const char *href) {
    return href && strncasecmp(href, "ftp://", 6) == 0;
}

int is_external_url(const char *href) {
    if (!href) return 0;
    
    // Check for protocol
    const char *protocols[] = {
        "http://", "https://", "ftp://", "ftps://", 
        "ws://", "wss://", "file://", "data:", NULL
    };
    
    for (int i = 0; protocols[i] != NULL; i++) {
        if (strncasecmp(href, protocols[i], strlen(protocols[i])) == 0) {
            return 1;
        }
    }
    
    return 0;
}

const char* get_link_type_from_href(const char *href) {
    if (!href) return "unknown";
    
    if (is_mailto_link(href)) return "email";
    if (is_tel_link(href)) return "phone";
    if (is_javascript_link(href)) return "javascript";
    if (is_anchor_link(href)) return "anchor";
    if (is_ftp_link(href)) return "ftp";
    if (is_external_url(href)) return "external";
    
    return "internal";
}

void detect_link_type(cJSON *link_json) {
    cJSON *href_item = cJSON_GetObjectItem(link_json, "href");
    if (!href_item || !cJSON_IsString(href_item)) {
        cJSON_AddStringToObject(link_json, "link_type", "unknown");
        return;
    }
    
    const char *href = href_item->valuestring;
    const char *type = get_link_type_from_href(href);
    
    cJSON_AddStringToObject(link_json, "link_type", type);
    
    // Add additional info based on type
    if (strcmp(type, "email") == 0) {
        cJSON_AddStringToObject(link_json, "email_address", href + 7);
    } else if (strcmp(type, "phone") == 0) {
        cJSON_AddStringToObject(link_json, "phone_number", href + 4);
    } else if (strcmp(type, "anchor") == 0) {
        cJSON_AddStringToObject(link_json, "anchor_target", href + 1);
    } else if (strcmp(type, "javascript") == 0) {
        cJSON_AddStringToObject(link_json, "javascript_code", href + 11);
    } else if (strcmp(type, "external") == 0) {
        cJSON_AddBoolToObject(link_json, "is_external_url", true);
    } else if (strcmp(type, "internal") == 0) {
        cJSON_AddBoolToObject(link_json, "is_internal_url", true);
    }
}

// ========== SECURITY ANALYSIS ==========

int is_potentially_unsafe_link(cJSON *link_json) {
    // Check for target=_blank without noopener
    cJSON *target = cJSON_GetObjectItem(link_json, "target");
    cJSON *noopener = cJSON_GetObjectItem(link_json, "rel_noopener");
    
    if (target && cJSON_IsString(target) && 
        strcasecmp(target->valuestring, "_blank") == 0) {
        if (!noopener || !cJSON_IsTrue(noopener)) {
            return 1;
        }
    }
    
    // Check for javascript: links (can be unsafe)
    cJSON *link_type = cJSON_GetObjectItem(link_json, "link_type");
    if (link_type && cJSON_IsString(link_type) &&
        strcmp(link_type->valuestring, "javascript") == 0) {
        return 1;
    }
    
    // Check for data: URLs (can be large or malicious)
    cJSON *href = cJSON_GetObjectItem(link_json, "href");
    if (href && cJSON_IsString(href) &&
        strncasecmp(href->valuestring, "data:", 5) == 0) {
        return 1;
    }
    
    return 0;
}

const char* get_security_recommendation(cJSON *link_json) {
    cJSON *target = cJSON_GetObjectItem(link_json, "target");
    cJSON *noopener = cJSON_GetObjectItem(link_json, "rel_noopener");
    
    if (target && cJSON_IsString(target) && 
        strcasecmp(target->valuestring, "_blank") == 0) {
        if (!noopener || !cJSON_IsTrue(noopener)) {
            return "target=_blank without rel=noopener";
        }
    }
    
    cJSON *link_type = cJSON_GetObjectItem(link_json, "link_type");
    if (link_type && cJSON_IsString(link_type)) {
        if (strcmp(link_type->valuestring, "javascript") == 0) {
            return "javascript: link (potentially unsafe)";
        }
        if (strcmp(link_type->valuestring, "data") == 0) {
            return "data: URL (potentially large or unsafe)";
        }
    }
    
    return NULL;
}

void analyze_link_security(cJSON *link_json) {
    if (is_potentially_unsafe_link(link_json)) {
        cJSON_AddBoolToObject(link_json, "security_warning", true);
        
        const char *recommendation = get_security_recommendation(link_json);
        if (recommendation) {
            cJSON_AddStringToObject(link_json, "security_issue", recommendation);
        }
    } else {
        cJSON_AddBoolToObject(link_json, "security_warning", false);
    }
}

// ========== MAIN PUBLIC FUNCTION ==========

cJSON* parse_link_element_complete(lxb_dom_element_t *link_elem, cJSON *base_json) {
    if (!link_elem || !base_json) return base_json;
    
    size_t attr_len;
    
    // 1. href attribute (most important)
    const lxb_char_t *href = lxb_dom_element_get_attribute(
        link_elem, (lxb_char_t*)"href", 4, &attr_len);
    if (href && attr_len > 0) {
        char *href_str = lexbor_to_cstr(href, attr_len);
        if (href_str) {
            // Clean href (remove quotes if present)
            if (href_str[0] == '"' || href_str[0] == '\'') {
                memmove(href_str, href_str + 1, strlen(href_str));
                size_t len = strlen(href_str);
                if (len > 0 && (href_str[len-1] == '"' || href_str[len-1] == '\'')) {
                    href_str[len-1] = '\0';
                }
            }
            
            cJSON_ReplaceItemInObject(base_json, "href", cJSON_CreateString(href_str));
            free(href_str);
        }
    }
    
    // 2. Parse all standard attributes
    parse_link_target_attribute(link_elem, base_json);
    parse_link_rel_attribute(link_elem, base_json);
    parse_link_download_attribute(link_elem, base_json);
    parse_link_title_attribute(link_elem, base_json);
    
    // 3. Parse other attributes (may not always be present)
    parse_link_type_attribute(link_elem, base_json);
    parse_link_hreflang_attribute(link_elem, base_json);
    parse_link_ping_attribute(link_elem, base_json);
    parse_link_referrerpolicy_attribute(link_elem, base_json);
    parse_link_media_attribute(link_elem, base_json);
    parse_link_charset_attribute(link_elem, base_json);
    parse_link_name_attribute(link_elem, base_json);
    
    // 4. Detect link type based on href
    detect_link_type(base_json);
    
    // 5. Perform security analysis
    analyze_link_security(base_json);
    
    // 6. Add some metadata
    cJSON_AddStringToObject(base_json, "element_type", "link");
    cJSON_AddNumberToObject(base_json, "parsed_at", (double)time(NULL));
    
    return base_json;
}