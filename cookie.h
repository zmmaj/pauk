// cookie.h
#ifndef COOKIE_H
#define COOKIE_H

#include <string.h>
#include <time.h>
#include <stdbool.h>

#define MAX_COOKIES 20
#define MAX_COOKIE_LEN 512

#define MAX_COOKIE_NAME   128
#define MAX_COOKIE_VAL    1024
#define MAX_COOKIE_DOMAIN 256
#define MAX_COOKIES_DISK  20

typedef struct {
    char name[MAX_COOKIE_NAME];
    char value[MAX_COOKIE_VAL];
    char domain[MAX_COOKIE_DOMAIN];
    int is_active; // 1 = aktivan, 0 = prazan slot
} disk_cookie_t;

typedef struct {
    char name[128];
    char value[256];
    char domain[128];
    char path[128];
    time_t expires;
    bool secure;
    bool httpOnly;
    bool is_session;
} Cookie;

typedef struct {
    Cookie cookies[MAX_COOKIES];
    int count;
} CookieJar;

// Funkcije
void init_cookie_jar(CookieJar *jar);
void parse_set_cookie(const char *set_cookie_header, CookieJar *jar);
bool cookie_is_valid_for_request(Cookie *cookie, const char *req_domain,
                                 const char *req_path, bool is_https);
void build_cookie_header(CookieJar *jar, const char *req_domain,
                         const char *req_path, bool is_https,
                         char *out_header, size_t max_len);
void dump_cookies_to_file(CookieJar *jar);
void dump_fetch_cookies(CookieJar *jar);


#endif
