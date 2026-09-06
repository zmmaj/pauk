#include "url_utils.h"
#include <lexbor/url/url.h>
#include <lexbor/html/html.h>
#include <lexbor/engine/engine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Check if URL is absolute
bool is_absolute_url(const char *url) {
    if (!url || !url[0]) return false;
    
    // Check for scheme://
    if (strstr(url, "://") != NULL) return true;
    
    // Check for data: URLs
    if (strncmp(url, "data:", 5) == 0) return true;
    
    // Check for absolute paths
    if (url[0] == '/') return true;
    
    return false;
}

// Check if URL is safe (block javascript: etc.)
bool is_safe_url(const char *url) {
    if (!url || !url[0]) return false;
    
    // Block javascript: URLs
    if (strncasecmp(url, "javascript:", 11) == 0) return false;
    
    // Block vbscript: URLs
    if (strncasecmp(url, "vbscript:", 9) == 0) return false;
    
    // Block data: URLs for now (they can be large)
    if (strncasecmp(url, "data:", 5) == 0) return false;
    
    return true;
}

// Safe string concatenation with bounds checking - FIXED VERSION
static void safe_strcat(char *dest, size_t dest_size, const char *src) {
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len >= dest_size) return; // Already full
    
    size_t available = dest_size - dest_len - 1; // -1 for null terminator
    if (src_len > available) {
        // Truncate to fit
        memcpy(dest + dest_len, src, available);
        dest[dest_size - 1] = '\0';
    } else {
        memcpy(dest + dest_len, src, src_len + 1); // +1 to include null terminator
    }
}

char *resolve_url(const char *base, const char *relative) {
    if (!base || !relative) return NULL;
    
    // If relative URL is already absolute, return copy
    if (is_absolute_url(relative)) {
        return strdup(relative);
    }
    
    // ===== UKLONI IME FAJLA IZ BASE URL-A =====
    char *base_copy = strdup(base);
    if (!base_copy) return NULL;
    
    const char *file_extensions[] = {".html", ".htm", ".php", ".asp", ".aspx", ".jsp", ".cgi", ".pl", NULL};
    for (int i = 0; file_extensions[i] != NULL; i++) {
        char *ext_pos = strstr(base_copy, file_extensions[i]);
        if (ext_pos) {
            char *last_slash = strrchr(base_copy, '/');
            if (last_slash && ext_pos > last_slash) {
                // Ukloni ime fajla (sve posle poslednje /)
                *(last_slash + 1) = '\0';
                break;
            }
        }
    }
    // ==========================================
    
    // Handle different relative URL patterns
    if (relative[0] == '/') {
        // Absolute path relative to domain
        const char *scheme_end = strstr(base, "://");
        if (scheme_end) {
            scheme_end += 3;
            const char *path_start = strchr(scheme_end, '/');
            if (path_start) {
                size_t domain_len = path_start - base;
                size_t result_size = domain_len + strlen(relative) + 1;
                char *result = malloc(result_size);
                if (!result) { free(base_copy); return NULL; }
                
                strncpy(result, base, domain_len);
                result[domain_len] = '\0';
                safe_strcat(result, result_size, relative);
                free(base_copy);
                return result;
            } else {
                size_t result_size = strlen(base) + strlen(relative) + 1;
                char *result = malloc(result_size);
                if (!result) { free(base_copy); return NULL; }
                
                strcpy(result, base);
                safe_strcat(result, result_size, relative);
                free(base_copy);
                return result;
            }
        }
    } else if (strncmp(relative, "./", 2) == 0) {
        char *result = resolve_url(base_copy, relative + 2);
        free(base_copy);
        return result;
    } else if (strncmp(relative, "../", 3) == 0) {
        const char *last_slash = strrchr(base_copy, '/');
        if (last_slash && last_slash > base_copy) {
            const char *scheme_end = strstr(base_copy, "://");
            if (!scheme_end || last_slash > scheme_end + 2) {
                size_t new_len = last_slash - base_copy;
                char *new_base = malloc(new_len + 1);
                if (!new_base) { free(base_copy); return NULL; }
                
                strncpy(new_base, base_copy, new_len);
                new_base[new_len] = '\0';
                char *result = resolve_url(new_base, relative + 3);
                free(new_base);
                free(base_copy);
                return result;
            }
        }
        free(base_copy);
        return NULL;
    }
    
    // Simple case: relative path, just concatenate
    size_t base_len = strlen(base_copy);
    size_t relative_len = strlen(relative);
    size_t result_size = base_len + relative_len + 3;
    
    char *result = malloc(result_size);
    if (!result) { free(base_copy); return NULL; }
    
    strcpy(result, base_copy);
    if (base_len > 0 && result[base_len - 1] != '/') {
        result[base_len] = '/';
        result[base_len + 1] = '\0';
    }
    
    safe_strcat(result, result_size, relative);
    
    free(base_copy);
    return result;
}

// Get base URL from <base> element in document
char *get_base_url_from_document(lxb_html_document_t *document) {
    if (!document) return NULL;
    
    // Look for <base href="..."> element
    lxb_dom_node_t *root = lxb_dom_interface_node(document);
    lxb_dom_node_t *node = lxb_dom_node_first_child(root);
    
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *element = lxb_dom_interface_element(node);
            size_t name_len;
            const lxb_char_t *name = lxb_dom_element_local_name(element, &name_len);
            
            if (name && name_len == 4 && memcmp(name, "base", 4) == 0) {
                // Found base element, get href attribute
                lxb_dom_attr_t *href_attr = lxb_dom_element_attr_by_name(element, 
                    (lxb_char_t*)"href", 4);
                
                if (href_attr) {
                    size_t href_len;
                    const lxb_char_t *href_val = lxb_dom_attr_value(href_attr, &href_len);
                    
                    if (href_val && href_len > 0) {
                        char *base_url = malloc(href_len + 1);
                        if (!base_url) return NULL;
                        
                        memcpy(base_url, href_val, href_len);
                        base_url[href_len] = '\0';
                     //   printf("Found base URL in document: %s\n", base_url);
                        return base_url;
                    }
                }
            }
        }
        node = lxb_dom_node_next(node);
    }
    
    return NULL;
}

// Get base URL from element's document location
char *get_base_url_from_element(lxb_dom_element_t *element) {
    if (!element) return NULL;
    
    // In a real browser, this would return the current page URL
    // For now, return a default
    return strdup("file:///current/document/");
}

// Normalize URL (remove ../, ./ etc.)
void normalize_url(char *url) {
    if (!url) return;
    
    // Simple normalization: remove trailing slash if it's not just "/"
    size_t len = strlen(url);
    if (len > 1 && url[len-1] == '/') {
        url[len-1] = '\0';
    }
}


// Helper function to resolve URLs in srcset attribute
char* resolve_urls_in_srcset(const char *base_url, const char *srcset) {
    if (!base_url || !srcset) return NULL;
    
    char *result = malloc(strlen(srcset) * 2 + 1);
    if (!result) return NULL;
    
    result[0] = '\0';
    char *srcset_copy = strdup(srcset);
    if (!srcset_copy) {
        free(result);
        return NULL;
    }
    
    char *saveptr;
    char *token = strtok_r(srcset_copy, ",", &saveptr);
    int first = 1;
    
    while (token) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        
        // Find space or end of URL
        char *space = strchr(token, ' ');
        char *url_part = token;
        char *descriptor = NULL;
        
        if (space) {
            *space = '\0';
            descriptor = space + 1;
            while (*descriptor == ' ') descriptor++;
        }
        
        // Resolve the URL
        char *resolved_url = resolve_url(base_url, url_part);
        
        if (!first) {
            strcat(result, ", ");
        }
        
        if (resolved_url) {
            strcat(result, resolved_url);
            free(resolved_url);
        } else {
            strcat(result, url_part);
        }
        
        if (descriptor && *descriptor) {
            strcat(result, " ");
            strcat(result, descriptor);
        }
        
        first = 0;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(srcset_copy);
    return result;
}

void url_cleanup(void) {
    // Cleanup any global URL resources if needed
}
