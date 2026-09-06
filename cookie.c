// cookie.c - HelenOS kompatibilna verzija
#include <string.h>
#include <str.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include "main.h"
#include "cookie.h"


// =========================================================================
// 🍪 GLAVNE FUNKCIJE
// =========================================================================


void init_cookie_jar(CookieJar *jar) {
    if (!jar) return;
    memset(jar, 0, sizeof(CookieJar));
}

void parse_set_cookie(const char *set_cookie_header, CookieJar *jar) {
    if (!set_cookie_header || !jar) return;

    if (strncasecmp(set_cookie_header, "Set-Cookie:", 11) == 0) set_cookie_header += 11;
    while (*set_cookie_header == ' ') set_cookie_header++;

    Cookie cookie = {0};
    cookie.secure = false;
    cookie.httpOnly = false;
    cookie.is_session = true;
    strcpy(cookie.path, "/");
    cookie.domain[0] = '\0';

    const char *sep = strchr(set_cookie_header, ';');
    size_t len = sep ? (size_t)(sep - set_cookie_header) : strlen(set_cookie_header);
    
    char cookie_str[MAX_COOKIE_LEN];
    if (len >= MAX_COOKIE_LEN) len = MAX_COOKIE_LEN - 1;
    
    // ===== KORISTI memcpy UMESTO strncpy =====
    memcpy(cookie_str, set_cookie_header, len);
    cookie_str[len] = '\0';

    char *eq = strchr(cookie_str, '=');
    if (!eq) return;

    // Ime
    size_t name_len = eq - cookie_str;
    if (name_len >= sizeof(cookie.name)) name_len = sizeof(cookie.name) - 1;
    memcpy(cookie.name, cookie_str, name_len);
    cookie.name[name_len] = '\0';

    // Vrednost
    strncpy(cookie.value, eq + 1, sizeof(cookie.value) - 1);
    cookie.value[sizeof(cookie.value) - 1] = '\0';

    // ===== PARSIRANJE ATRIBUTA =====
    if (sep) {
        const char *attrs = sep + 1;
        while (*attrs == ' ') attrs++;

        char attrs_copy[512];
        strncpy(attrs_copy, attrs, sizeof(attrs_copy) - 1);
        attrs_copy[sizeof(attrs_copy) - 1] = '\0';

        char *token = strtok(attrs_copy, ";");
        while (token) {
            while (*token == ' ') token++;

            if (strncasecmp(token, "Path=", 5) == 0) {
                strncpy(cookie.path, token + 5, sizeof(cookie.path) - 1);
                cookie.path[sizeof(cookie.path) - 1] = '\0';
            }
            else if (strncasecmp(token, "Domain=", 7) == 0) {
                strncpy(cookie.domain, token + 7, sizeof(cookie.domain) - 1);
                cookie.domain[sizeof(cookie.domain) - 1] = '\0';
            }
            else if (strncasecmp(token, "Secure", 6) == 0) {
                cookie.secure = true;
            }
            else if (strncasecmp(token, "HttpOnly", 8) == 0) {
                cookie.httpOnly = true;
            }
            else if (strncasecmp(token, "Expires=", 8) == 0) {
                cookie.is_session = false;
                // cookie.expires = parse_http_date(token + 8);
            }

            token = strtok(NULL, ";");
        }
    }

    // ===== DEDUPLIKACIJA =====
    int target_index = -1;
    for (int i = 0; i < jar->count; i++) {
        if (strcmp(jar->cookies[i].name, cookie.name) == 0) {
            target_index = i;
            break;
        }
    }

    if (target_index != -1) {
        jar->cookies[target_index] = cookie;
    } else if (jar->count < MAX_COOKIES) {
        jar->cookies[jar->count++] = cookie;
    }
}

bool cookie_is_valid_for_request(Cookie *cookie, const char *req_domain,
                                 const char *req_path, bool is_https) {
    if (!cookie || !req_domain || !req_path) return false;

    // Provera domene (suffix matching)
    size_t dlen = strlen(cookie->domain);
    size_t rlen = strlen(req_domain);
    if (dlen > 0) {
        if (rlen < dlen) return false;
        if (strcasecmp(req_domain + rlen - dlen, cookie->domain) != 0) return false;
    }

    // Provera path
    if (strncmp(req_path, cookie->path, strlen(cookie->path)) != 0) return false;

    // Provera Secure atributa
    if (cookie->secure && !is_https) return false;

    // Provera isteka
    if (!cookie->is_session) {
        time_t now = time(NULL);
        if (cookie->expires < now) return false;
    }

    return true;
}

// =========================================================================
// 🍪 BEZBEDNO SKLAPANJE KOLAČIĆA (DEBLOKADA MREŽNE NITI)
// =========================================================================
void build_cookie_header(CookieJar *jar, const char *req_domain,
    const char *req_path, bool is_https,
    char *out_header, size_t max_len) 
{
if (!jar || !out_header || max_len == 0) return;
out_header[0] = '\0';

char temp_buf[4096];
temp_buf[0] = '\0';
int pos = 0;
bool first = true;

for (int i = 0; i < jar->count && i < MAX_COOKIES; i++) {
Cookie *c = &jar->cookies[i];

if (cookie_is_valid_for_request(c, req_domain, req_path, is_https)) {
int written = snprintf(temp_buf + pos, sizeof(temp_buf) - pos, 
              "%s%s=%s", first ? "" : "; ", c->name, c->value);
if (written > 0 && (size_t)(pos + written) < sizeof(temp_buf)) {
pos += written;
first = false;
}
}
}

if (!first && strlen(temp_buf) > 0) {
snprintf(out_header, max_len, "Cookie: %s\r\n", temp_buf);
}
}


// =========================================================================
// 💾 ŠTAMPANJE KOLAČIĆA U DATOTEKU (COOKIE LOG SHIELD)
// =========================================================================
void dump_cookies_to_file(CookieJar *jar) {
    if (!jar) return;

    // 📁 1. DEO: SIGURAN BINARNI UPIS ZA PRETRAŽIVAČ
    FILE *f_bin = fopen("cookie.bin", "wb"); // "wb" = write binary
    if (f_bin) {
        for (int i = 0; i < jar->count && i < MAX_COOKIES_DISK; i++) {
            disk_cookie_t record;
            memset(&record, 0, sizeof(disk_cookie_t));

            strncpy(record.name, jar->cookies[i].name, MAX_COOKIE_NAME - 1);
            strncpy(record.value, jar->cookies[i].value, MAX_COOKIE_VAL - 1);
            strncpy(record.domain, jar->cookies[i].domain, MAX_COOKIE_DOMAIN - 1);
            record.is_active = 1;

            fwrite(&record, sizeof(disk_cookie_t), 1, f_bin);
        }
        fclose(f_bin);
        kopiraj_fajl("cookie.bin"); // VFS Sync za binarni fajl
    }

    // 📝 2. DEO: TEKSTUALNI UPIS ZA TEBE (DA MOŽEŠ DA PROČITAŠ)
    FILE *f_txt = fopen("cookie.txt", "w"); // "w" = write text
    if (f_txt) {
        fprintf(f_txt, "# PaukBrowser - Živi registar mrežnih kolačića\n");
        fprintf(f_txt, "# Ukupan broj sačuvanih kolačića u RAM-u: %d\n\n", jar->count);

        for (int i = 0; i < jar->count && i < MAX_COOKIES_DISK; i++) {
            fprintf(f_txt, "KOLAČIĆ #%d:\n", i + 1);
            fprintf(f_txt, "  Ime:     %s\n", jar->cookies[i].name);
            fprintf(f_txt, "  Vrednost:%s\n", jar->cookies[i].value);
            fprintf(f_txt, "  Domen:   %s\n", jar->cookies[i].domain);
            fprintf(f_txt, "--------------------------------------------------\n");
        }
        fclose(f_txt);
        kopiraj_fajl("cookie.txt"); // VFS Sync za tekstualni fajl
    }

    printf("💾 [Cookie Guard] Uspešno izvršen dupli upis (cookie.bin i cookie.txt)!\n");
}




void dump_fetch_cookies(CookieJar *jar) {
    if (!jar) return;
    
    FILE *f = fopen("fetch_cookie.txt", "w");
    if (!f) return;
    
    fprintf(f, "# Šta Fetch vidi u memoriji:\n");
    fprintf(f, "Broj kolacica: %d\n\n", jar->count);
    
    for (int i = 0; i < jar->count && i < MAX_COOKIES; i++) {
        fprintf(f, "KOLAČIĆ #%d:\n", i + 1);
        fprintf(f, "  Ime:     %s\n", jar->cookies[i].name);
        fprintf(f, "  Vrednost:%s\n", jar->cookies[i].value);
        fprintf(f, "  Domen:   %s\n", jar->cookies[i].domain);
        fprintf(f, "  Putanja: %s\n", jar->cookies[i].path);
        fprintf(f, "  Session: %s\n", jar->cookies[i].is_session ? "DA" : "NE");
        fprintf(f, "  Secure:  %s\n", jar->cookies[i].secure ? "DA" : "NE");
        fprintf(f, "--------------------------------------------------\n");
    }
    
    fclose(f);
    kopiraj_fajl("fetch_cookie.txt");
    printf("🍪 [Cookie] Stanje kolačića sačuvano u fetch_cookie.txt (%d kolačića)\n", jar->count);
}

