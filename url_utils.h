
#ifndef URL_UTILS_H
#define URL_UTILS_H


#include <lexbor/url/url.h>
#include <lexbor/html/html.h>
#include <lexbor/engine/engine.h>
#include <stdbool.h>

// URL resolution functions
char *resolve_url(const char *base, const char *relative);
bool is_absolute_url(const char *url);
bool is_safe_url(const char *url);
char *get_base_url_from_document(lxb_html_document_t *document);
char *get_base_url_from_element(lxb_dom_element_t *element);
void normalize_url(char *url);
char* resolve_urls_in_srcset(const char *base_url, const char *srcset);
// Memory management
void url_cleanup(void);

#endif
