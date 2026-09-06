// pauk_tls.c - Mbed TLS 3.2.1 with TLS 1.3 support


#include <errno.h>
#include <str.h>
#include <mem.h>
#include <miniz.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ui/ui.h>
#include <ui/msgdialog.h>
#include <fibril.h>
#include "main.h"
#include "gui.h"
#include "network.h"
#include "image_cache.h"
#include "img_parser.h"
#include "time_utils.h"
#include "cookie.h"
#include "resource_filter.h"

#ifdef HAVE_MBEDTLS
#include "pauk_tls.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/debug.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>


void *g_cached_tls = NULL;
tcp_conn_t *g_cached_conn = NULL;

static void *g_main_tls = NULL;
static tcp_conn_t *g_main_conn = NULL;

static void *g_keepalive_tls = NULL;
static tcp_conn_t *g_keepalive_conn = NULL;
static char g_keepalive_host[256] = {0};

static ConnectionPool g_pool = {0};
static int g_pool_initialized = 0;

static tcp_conn_t *g_http_keepalive_conn = NULL;
static char g_http_keepalive_host[256] = {0};

static fibril_mutex_t g_pool_mutex;
static fibril_mutex_t g_tls_handshake_mutex;

extern CookieJar g_cookie_jar;


#define min(a, b) ((a) < (b) ? (a) : (b))
void srbinos_delay_1(unsigned int ms) {
    fibril_usleep(ms * 1000);
}



/* Force a modern date for mbedTLS validation (April 2026) */
mbedtls_time_t mbedtls_platform_time(mbedtls_time_t *t) {
    mbedtls_time_t now = 1777536000; 
    if (t) *t = now;
    return now;
}
// I/O callbacks for Mbed TLS 3.x
static int tls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)ctx;
    
    // HelenOS tcp_conn_send only takes 3 arguments
    errno_t rc = tcp_conn_send(conn->tcp_conn, (const void *)buf, len);

    if (rc == EOK) {
        return (int)len; // Assume full buffer sent on EOK
    } 
    
    if (rc == EAGAIN) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    return MBEDTLS_ERR_NET_SEND_FAILED;
}

void store_main_connection(void *tls, tcp_conn_t *conn) {
    g_main_tls = tls;
    g_main_conn = conn;
}

void* get_main_tls(void) {
    return g_main_tls;
}

tcp_conn_t* get_main_conn(void) {
    return g_main_conn;
}

static int tls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)ctx;
    size_t nread = 0;
    errno_t rc = tcp_conn_recv(conn->tcp_conn, (char *)buf, len, &nread);
    
   // printf("RECV CB: rc=%d, nread=%zu, len=%zu\n", rc, nread, len);
    
    if (rc == EOK && nread > 0) {
      //  printf("RECV CB: received %zu bytes\n", nread);
        return (int)nread;
    }
    if (rc == EAGAIN || (rc == EOK && nread == 0)) {
      //  printf("RECV CB: would block (EAGAIN)\n");
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    printf("RECV CB: error %d\n", rc);
    return MBEDTLS_ERR_NET_RECV_FAILED;
}


static int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    for (size_t i = 0; i < len; i++) {
        output[i] = (unsigned char)(rand() & 0xFF);
    }
    *olen = len;
    return 0;
}

int load_ca_cert_file(mbedtls_x509_crt *crt, const char *filename) {
    int ret = mbedtls_x509_crt_parse_file(crt, filename);
    
    if (ret < 0) {
        printf("Failed to parse CA file %s: %d (-0x%04X)\n", filename, ret, -ret);
        return ret;
    }
    
    if (ret > 0) {
        printf("Loaded CA certificates from %s (%d certs, %d had warnings)\n", 
               filename, ret, ret);
    } else {
        printf("Loaded CA certificates from %s successfully\n", filename);
    }
    
    return 0;
}

int load_ca_cert_der(mbedtls_x509_crt *crt, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    fread(buf, 1, size, f);
    fclose(f);

    /* DER format: NO null terminator needed */
    int ret = mbedtls_x509_crt_parse(crt, buf, size);
    free(buf);

    return ret;
}

// Decode chunked transfer encoding in-place
size_t decode_chunked_body(char *buffer, size_t total_size, size_t body_start) {
    char *read_ptr = buffer + body_start;
    char *write_ptr = buffer + body_start;
    size_t remaining = total_size - body_start;
    size_t total_written = 0;
    
    while (remaining > 0) {
        // Find chunk size line (ends with \r\n)
        char *size_end = memchr(read_ptr, '\n', remaining);
        if (!size_end) break;
        
        // Parse hex chunk size
        long chunk_size = strtol(read_ptr, NULL, 16);
        if (chunk_size == 0) break; // End of chunks
        
        // Move to start of chunk data (after \r\n)
        char *chunk_data = size_end + 1;
        if (*(size_end - 1) == '\r') chunk_data = size_end + 1;
        else chunk_data = size_end + 1;
        
        // Verify we have enough data
        size_t data_available = remaining - (chunk_data - read_ptr);
        if (data_available < (size_t)chunk_size) break;
        
        // Copy chunk data to write position (stripping chunk headers)
        memmove(write_ptr, chunk_data, chunk_size);
        write_ptr += chunk_size;
        total_written += chunk_size;
        
        // Move to next chunk (skip the trailing \r\n after chunk data)
        read_ptr = chunk_data + chunk_size + 2; // +2 for \r\n
        remaining = total_size - (read_ptr - buffer);
    }
    
    return total_written;
}

static void my_debug(void *ctx, int level, const char *file, int line, const char *str) {  
    ((void) level);
    printf("%s:%04d: %s", file, line, str);
}

errno_t create_https_connection(const char *hostname,
    tcp_conn_t *tcp_conn,
    pauk_tls_handle_t *tls_conn)
{
    // Use calloc to ensure all pointers are NULL initially for safe 'fail' cleanup
    pauk_tls_connection_t *conn = calloc(1, sizeof(*conn));
    if (!conn) return ENOMEM;

    conn->tcp_conn = tcp_conn;
    
    // Debug setup
    mbedtls_ssl_conf_dbg(&conn->conf, my_debug, NULL);  // Koristi my_debug za bolje informacije
    mbedtls_debug_set_threshold(0); 
    
    /* ---------------- INIT ---------------- */
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->conf);
    mbedtls_ctr_drbg_init(&conn->ctr_drbg);
    mbedtls_entropy_init(&conn->entropy);
    mbedtls_x509_crt_init(&conn->ca_cert);

    /* ---------------- ENTROPY ---------------- */
    mbedtls_entropy_add_source(&conn->entropy, mbedtls_platform_entropy_poll, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);

    int ret = mbedtls_ctr_drbg_seed(&conn->ctr_drbg, mbedtls_entropy_func, &conn->entropy, NULL, 0);
    if (ret != 0) {
        printf("TLS ERROR: RNG seeding failed: %d\n", ret);
        goto fail;
    }

    /* ---------------- SSL CONFIG ---------------- */
    ret = mbedtls_ssl_config_defaults(&conn->conf,
              MBEDTLS_SSL_IS_CLIENT,
              MBEDTLS_SSL_TRANSPORT_STREAM,
              MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("TLS ERROR: SSL config defaults failed: %d\n", ret);
        goto fail;
    }

    /* Set cipher suites - podržava i RSA i ECDHE */
    int ciphersuites[] = {
        // TLS 1.3 cipher suites (ako je dostupno)
#ifdef MBEDTLS_SSL_PROTO_TLS1_3
        MBEDTLS_TLS_AES_256_GCM_SHA384,
        MBEDTLS_TLS_AES_128_GCM_SHA256,
        MBEDTLS_TLS_CHACHA20_POLY1305_SHA256,
#endif
        // TLS 1.2 ECDHE cipher suites
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
        // TLS 1.2 RSA cipher suites (fallback)
        MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
        MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256,
        0
    };
    mbedtls_ssl_conf_ciphersuites(&conn->conf, ciphersuites);

    /* ALPN for Google support */
    const char *alpn_protos[] = { "http/1.1", NULL };
    ret = mbedtls_ssl_conf_alpn_protocols(&conn->conf, alpn_protos);
    if (ret != 0) {
        printf("TLS WARNING: ALPN config failed: %d\n", ret);
    }

    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->ctr_drbg);

    /* Disable certificate verification for development */
    mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_NONE);
    
    /* ================================================================
     * TLS VERSIONS: Podrška i za TLS 1.3 i TLS 1.2
     * - Pokušaj prvo TLS 1.3 (ako je podržan)
     * - Fallback na TLS 1.2 za stare servere
     * ================================================================ */
#ifdef MBEDTLS_SSL_PROTO_TLS1_3
    // Podrška za TLS 1.3 do TLS 1.2 (max 1.3, min 1.2)
    mbedtls_ssl_conf_min_version(&conn->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3); // TLS 1.2
    mbedtls_ssl_conf_max_version(&conn->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_4); // TLS 1.3
    if(INFO_MESSAGES) printf("🔐 [TLS] Podržavam TLS 1.2 i TLS 1.3\n");
#else
    // Samo TLS 1.2 (za stare verzije mbedTLS)
    mbedtls_ssl_conf_max_version(&conn->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    if(INFO_MESSAGES) printf("🔐 [TLS] Podržavam TLS 1.2 (TLS 1.3 nije dostupan)\n");
#endif
    /* ================================================================ */

    /* ---------------- SSL SETUP ---------------- */
    ret = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (ret != 0) {
        printf("TLS ERROR: SSL setup failed: %d\n", ret);
        goto fail;
    }

    /* Set hostname for SNI */
    ret = mbedtls_ssl_set_hostname(&conn->ssl, hostname);
    if (ret != 0) {
        printf("TLS WARNING: SNI set failed: %d\n", ret);
    }

    /* Set BIO callbacks */
    mbedtls_ssl_set_bio(&conn->ssl, conn, tls_send_cb, tls_recv_cb, NULL);

    /* ---------------- HANDSHAKE LOOP ---------------- */
    printf("TLS: Starting handshake with %s...\n", hostname);
    int handshake_retries = 0;
    while (1) {
        ret = mbedtls_ssl_handshake(&conn->ssl);
        
        if (ret == 0) break;

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            fibril_usleep(10000); 
            if (++handshake_retries > 500) {
                printf("TLS ERROR: Handshake timeout after %d retries\n", handshake_retries);
                goto fail;
            }
            continue;
        }

        // Specifično rukovanje greškama
        char err_buf[256];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        printf("TLS Handshake FAILED: %s (-0x%04X)\n", err_buf, -ret);
        
        if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
            printf("TLS: Server sent fatal alert - trying lower TLS version...\n");
            // Probaj sa nižom TLS verzijom
            mbedtls_ssl_conf_max_version(&conn->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
            // Resetuj SSL i probaj ponovo
            mbedtls_ssl_free(&conn->ssl);
            mbedtls_ssl_init(&conn->ssl);
            ret = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
            if (ret != 0) goto fail;
            mbedtls_ssl_set_hostname(&conn->ssl, hostname);
            mbedtls_ssl_set_bio(&conn->ssl, conn, tls_send_cb, tls_recv_cb, NULL);
            continue; // Pokušaj ponovo sa TLS 1.2
        }
        
        goto fail;
    }

    /* ---------------- SUCCESS ---------------- */
    const char *version = mbedtls_ssl_get_version(&conn->ssl);
    const char *cipher = mbedtls_ssl_get_ciphersuite(&conn->ssl);
    printf("✅ TLS Handshake SUCCESS! Protocol: %s, Cipher: %s\n", 
           version ? version : "unknown", 
           cipher ? cipher : "unknown");
    
    /* Check ALPN negotiation */
    const char *alpn = mbedtls_ssl_get_alpn_protocol(&conn->ssl);
    if (alpn) {
        printf("TLS: ALPN negotiated: %s\n", alpn);
    }
    
    *tls_conn = conn;
    return EOK;

fail:
    /* Cleanup */
    mbedtls_x509_crt_free(&conn->ca_cert);
    mbedtls_entropy_free(&conn->entropy);
    mbedtls_ctr_drbg_free(&conn->ctr_drbg);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_ssl_free(&conn->ssl);
    free(conn);
    return EIO;
}


// Perform TLS handshake (separate function if needed)
errno_t tls_perform_handshake(pauk_tls_handle_t tls_conn) {
    // Handshake is now done in create_https_connection
    return EOK;
}

// Send data over TLS
errno_t tls_send(pauk_tls_handle_t tls_conn, const void *data, size_t len) {
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    
    /* Mbed TLS 1.3 may need to send in chunks */
    size_t remaining = len;
    const unsigned char *ptr = (const unsigned char *)data;
    int total_sent = 0;
    
    while (remaining > 0) {
        int ret = mbedtls_ssl_write(&conn->ssl, ptr, remaining);
        
        if (ret > 0) {
            total_sent += ret;
            ptr += ret;
            remaining -= ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            /* Need to wait - continue loop */
            srbinos_delay_1(10);
            continue;
        } else {
            printf("tls_send: error %d after sending %d bytes\n", ret, total_sent);
            return EIO;
        }
    }
    
    printf("tls_send: successfully sent all %zu bytes\n", len);
    return EOK;
}

// Receive data over TLS
errno_t tls_receive(pauk_tls_handle_t tls_conn, void *buffer, size_t size, size_t *received) {
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    int ret = mbedtls_ssl_read(&conn->ssl, (unsigned char *)buffer, size);
    
    if (ret > 0) {
        *received = ret;
        return EOK;
    } else if (ret == 0) {
        /* Normal EOF */
        *received = 0;
        return EOK;
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        /* Need to wait for more data */
        *received = 0;
        return EAGAIN;
    } else {
        /* Server closed the connection - treat as success */
        *received = 0;
        return EOK;
    }
}

// Close TLS connection
void tls_close(pauk_tls_handle_t tls_conn) {
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    if (conn) {
        mbedtls_ssl_close_notify(&conn->ssl);
#if defined(MBEDTLS_X509_CRT_PARSE_C)
        mbedtls_x509_crt_free(&conn->ca_cert);
#endif
        mbedtls_entropy_free(&conn->entropy);
        mbedtls_ctr_drbg_free(&conn->ctr_drbg);
        mbedtls_ssl_config_free(&conn->conf);
        mbedtls_ssl_free(&conn->ssl);
        free(conn);
    }
}

errno_t fetch_https_content(const char *url, tcp_conn_t *tcp_conn, char **content, size_t *content_size, int redirect_count) {
    if (redirect_count > MAX_REDIRECTS) {
        printf("Too many redirects (%d)\n", redirect_count);
        return ELOOP;
    }

    // === GLOBAL SAFETY PROTOCOL FOR PORT 80 CONFLICTS ===
    if (strstr(url, ":80/") != NULL) {
        printf("⚠️ fetch_https_content: Detektovan port 80! Preusmeravam na fetch_http_content...\n");
        return fetch_http_content(url, tcp_conn, content, content_size, redirect_count);
    }

    // =========================================================================
    // 🛡️ UNIVERZALNI AUTO-WWW PRESRETAČ (KORAK 1)
    // Automatski dodaje 'www.' SVAKOM golom domenu na internetu
    // =========================================================================
    const char *final_url = url;
    char *allocated_url_buffer = NULL;

    // Preskačemo lokalne fajlove i ciljamo samo mrežne adrese
    const char *proto_end = strstr(url, "://");
    if (proto_end && strncmp(url, "file:", 5) != 0) {
        const char *domain_start = proto_end + 3;
        const char *path_start = strchr(domain_start, '/');
        
        // Brojimo tačke unutar domena da prepoznamo goli domen
        int dot_count = 0;
        const char *dot_ptr = domain_start;
        while ((dot_ptr = strchr(dot_ptr, '.')) != NULL) {
            if (path_start && dot_ptr >= path_start) {
                break; // Ova tačka pripada putanji, a ne domenu
            }
            dot_count++;
            dot_ptr++;
        }

        // Tačno 1 tačka znači da je u pitanju goli domen (npr. google.com, djurkovicdent.me)
        if (dot_count == 1 && strncasecmp(domain_start, "localhost", 9) != 0) {
            printf("⚙️ [Auto-WWW] Detektovan goli domen! Ubacujem 'www.' u memoriju...\n");

            // Izvlačimo protokol (http ili https) dinamički
            char protocol[16] = "https";
            size_t proto_len = proto_end - url;
            if (proto_len < sizeof(protocol)) {
                memcpy(protocol, url, proto_len);
                protocol[proto_len] = '\0';
            }

            if (!path_start) path_start = "/";

            // Alociramo bezbedan, izolovan prostor na heap-u za novi URL
            size_t new_url_len = strlen(protocol) + 3 + 4 + strlen(domain_start) + 16;
            allocated_url_buffer = malloc(new_url_len);
            
            if (allocated_url_buffer) {
                snprintf(allocated_url_buffer, new_url_len, "%s://www.%s", protocol, domain_start);
                final_url = allocated_url_buffer; // Preusmeravamo pokazivač na bezbednu adresu
                printf("🌐 [Auto-WWW] URL uspešno transformisan u: %s\n", final_url);
            }
        }
    }
    // =========================================================================

    int is_image = 0;
    char *content_type = NULL; 
    void *tls_conn = NULL;
    errno_t rc;
    
    // POPRAVLJENO: Pravi, bezbedni nizovi karaktera sa fiksnom veličinom!
    char hostname[256];
    char request[16384];

    // Izvlačimo host iz final_url-a koji sada garantovano ima www. ako mu je falio
    rc = extract_hostname(final_url, hostname, sizeof(hostname));
    if (rc != EOK) {
        if (allocated_url_buffer) free(allocated_url_buffer);
        return rc;
    }
    
    rc = create_https_connection(hostname, tcp_conn, &tls_conn);
    if (rc != EOK) {
        if (allocated_url_buffer) free(allocated_url_buffer);
        return rc;
    }

    const char *path = extract_path(final_url);
    if (!path || path[0] == '\0') {
        path = "/";
    }
    
    // Podrazumevani ID sajta (0 = Standardni sajt van liste)
    int problematican_sajt_id = 0;

    if (strstr(hostname, "google.") != NULL) {
        if (strstr(url, "/search?") != NULL || strstr(path, "/search") != NULL) {
            problematican_sajt_id = 5; // 🚀 REQ KOLOSEK ZA GOOGLE PRETRAGU
        } else {
            problematican_sajt_id = 1; // 🎨 STANDARDNA GOOGLE POČETNA STRANA
        }
    }  else if (strstr(hostname, "duckduckgo.") != NULL) {
        // 🚀 Ako URL sadrži znak pitanja i parametar q=, to je živa pretraga!
        if (strchr(url, '?') != NULL || strchr(path, '?') != NULL) {
            problematican_sajt_id = 6; // 🖥️ NOVI REQ KOLOSEK SAMO ZA DUCKDUCKGO PRETRAGU
        } else {
            problematican_sajt_id = 2; // DuckDuckGo početna strana (Case 2)
        }
    } else if (strstr(hostname, "yahoo.") != NULL) {
        problematican_sajt_id = 3; // Yahoo kolosek
    } else if (strstr(hostname, "bing.") != NULL) {
        problematican_sajt_id = 4; // bing kolosek
    } else if (strstr(hostname, "mojeek.") != NULL) {
        problematican_sajt_id = 7;  // ← DODAJ MOJEEK!
    }

    // =========================================================================
    // 🚀 ON-DEMAND SWITCH REKVIZITI (Ekvivalent posebnih konfiguracija)
    // Skače direktno na fiksno i ručno podešene HTTP pakete bez gubljenja taktova
    // =========================================================================
    switch (problematican_sajt_id) {
        
        case 1: {
            // ==========================================
            // 📝 GOOGLE CONFIGURATION BLOCK (Fiksni Paket)
            // ==========================================
            const char *ua = "Pauk1.0.0rel.1 libwww-FM/2.14 SSL-MM/1.4.1";
            if(INFO_MESSAGES) printf("🤖 [Router Switch] Pokrećem fiksni GOOGLE_CFG blok koda...\n");

            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
                "Accept-Encoding: identity\r\n"
                "Referer: https://google.com\r\n"  // ← Fiksni dokazani referer
                "Connection: close\r\n"
                "\r\n",
                path, hostname, ua);
            break;
        }

        case 2: {
            // ==========================================
            // 📝 DUCKDUCKGO CONFIGURATION BLOCK (Fiksni Paket)
            // ==========================================
            const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
            if(INFO_MESSAGES) printf("🤖 [Router Switch] Pokrećem fiksni DUCKDUCKGO_CFG blok koda...\n");

            // POPRAVLJENO: Za DuckDuckGo skidamo 'www.' iz Host polja ako ga je preprocesor dodao
            char goli_ddg[256];
            strcpy(goli_ddg, hostname);
            if (strncasecmp(hostname, "www.", 4) == 0) {
                strcpy(goli_ddg, hostname + 4);
            }

            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"                      // ← Koristi čisti goli domen (duckduckgo.com)
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
                "Accept-Encoding: identity\r\n"
                "Referer: https://duckduckgo.com\r\n" // Frankni Mozilla referer za DDG
                "Connection: close\r\n"
                "\r\n",
                path, goli_ddg, ua);
            break;
        }

        case 3: {
            // ==========================================
            // 📝 YAHOO CONFIGURATION BLOCK (Fiksni Paket)
            // ==========================================
            const char *ua = "Pauk1.0.0rel.1 libwww-FM/2.14 SSL-MM/1.4.1";
            if(INFO_MESSAGES) printf("🤖 [Router Switch] Pokrećem fiksni YAHOO_CFG blok koda...\n");

            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
                "Accept-Encoding: identity\r\n"
                "Referer: https://yahoo.com\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, hostname, ua);
            break;
        }
        case 4: {
            // =================================================================
            // 📝 BING CONFIGURATION BLOCK (Fiksni Paket za zaobilazak SSL greške)
            // =================================================================
            // Koristimo modernu Mozilu jer Bing ne voli Lynx, ali menjamo Host na ://bing.com
            // koji ima bazični, lakši SSL sertifikat i prolazi mrežni Handshake u SrbinOs-u!
            const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
            const char *bing_safe_host = "://bing.com";
            
            if(INFO_MESSAGES) printf("🤖 [Router Switch] Pokrećem fiksni BING_CFG mrežni blok...\n");

            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"                         // ← Prisilo šaljemo ://bing.com da prođe SSL!
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
                "Accept-Encoding: identity\r\n"
                "Referer: https://bing.com\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, bing_safe_host, ua);
            break;
        }
        case 5: {
      // ===== KORISTI MOZILLA UA ZA PRETRAGU =====
    const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    
    if(INFO_MESSAGES) printf("🚀 [Network Core] Pokrećem Google Search zahtev (Chrome UA)...\n");

    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "Upgrade-Insecure-Requests: 1\r\n"
        "User-Agent: %s\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n"
        "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
        "Accept-Encoding: gzip, deflate\r\n"  // ← VRATI NA gzip!
        "Sec-Fetch-Site: same-origin\r\n"
        "Sec-Fetch-Mode: navigate\r\n"
        "Sec-Fetch-User: ?1\r\n"
        "Sec-Fetch-Dest: document\r\n"
        "Sec-Ch-Ua: \"Google Chrome\";v=\"131\", \"Chromium\";v=\"131\", \"Not_A Brand\";v=\"24\"\r\n"
        "Sec-Ch-Ua-Mobile: ?0\r\n"
        "Sec-Ch-Ua-Platform: \"Windows\"\r\n"
        "Referer: https://www.google.com/\r\n"
        "\r\n",
        path, hostname, ua);
            break;
        }
  // =========================================================================
        // 🚀 CASE 6: DUCKDUCKGO SEARCH EXCLUSIVE NETWORK PATH
        // Šalje optimizovano, čisto desktop zaglavlje specijalno za DDG pretragu,
        // prisiljavajući server da izbaci punu PC verziju rezultata sa ikonama!
        // =========================================================================
        case 6: {
            const char *ua_desktop = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
            
            if(INFO_MESSAGES) printf("🌐 [Network Router] Ispaljujem namenski DUCKDUCKGO_SEARCH request (Case 6)...\n");

            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: %s\r\n"                         // ← Desktop identitet za DDG rezultate
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                "Accept-Language: en-US,en;q=0.5\r\n"
                "Accept-Encoding: identity\r\n"             // ← Tražimo sirovi tekst radi bezbednosti steka
                "Referer: https://duckduckgo.com/\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, hostname, ua_desktop);
            break;
        }
        case 7: {
            // ==========================================
            // 📝 MOJEEK CONFIGURATION BLOCK (KAO PRAVI CHROME)
            // ==========================================
            const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
            
            if(INFO_MESSAGES) printf("🔍 [Router] Pokrećem MOJEEK_CFG blok koda...\n");
        
            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n"
                "Accept-Language: en-US,en;q=0.9\r\n"
                "Accept-Encoding: gzip, deflate\r\n"      // ← BEZ br (ako nemaš Brotli)
                "Upgrade-Insecure-Requests: 1\r\n"        // ← DODAJ OVO!
                "Sec-Fetch-Site: none\r\n"                // ← DODAJ OVO!
                "Sec-Fetch-Mode: navigate\r\n"            // ← DODAJ OVO!
                "Sec-Fetch-User: ?1\r\n"                  // ← DODAJ OVO!
                "Sec-Fetch-Dest: document\r\n"            // ← DODAJ OVO!
                "Referer: https://mojeek.com/\r\n"        // ← BEZ www.
                "Connection: close\r\n"
                "\r\n",
                path, hostname, ua);
            break;
        }
        // =========================================================================
        default: {
            const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

            if(INFO_MESSAGES) printf("📥 [Network Default] Povlačim resurs sa hosta: %s, putanja: %s\n", hostname, path);
            
            snprintf(request, sizeof(request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: %s\r\n"
                "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n"
                "Accept-Language: en-US,en;q=0.9\r\n"
                "Accept-Encoding: identity\r\n"  // ← Tražimo nekompresovan odgovor!
                "Referer: https://%s/\r\n"
                "Connection: close\r\n"
                "\r\n",
                path, hostname, ua, hostname);
            break;
        }
    }
    // =========================================================================
    // Ili, ako hoćeš da budeš siguran:
    int is_https = (strncmp(url, "https://", 8) == 0);  // Proveri URL

    char cookie_header[4096] = {0};
    build_cookie_header(&g_cookie_jar, hostname, path, is_https, 
                        cookie_header, sizeof(cookie_header));
//  kraj pobude cookie

    printf("📤 Sending request (%zu bytes):\n", strlen(request));
    printf("--- START REQUEST ---\n%s", request);
// ===== SAČUVAJ REQUEST U FAJL =====
FILE *f_req = fopen("request.txt", "w");
if (f_req) {
    fprintf(f_req, "=== COMPLETE REQUEST ===\n");
    fprintf(f_req, "URL: %s\n", final_url ? final_url : url);
    fprintf(f_req, "Host: %s\n", hostname);
    fprintf(f_req, "Path: %s\n", path);
    fprintf(f_req, "=== HEADERS ===\n");
    fprintf(f_req, "%s", request);
    fprintf(f_req, "=== END REQUEST ===\n");
    fprintf(f_req, "REQUEST DUŽINA: %zu bajtova\n", strlen(request));
    fclose(f_req);
    printf("📝 [Network] Request sačuvan u request.txt\n");
    kopiraj_fajl("request.txt");
}

// ===== ISPIS REQUEST-A NA EKRAN =====
printf("\n📤 ===== COMPLETE REQUEST =====\n");
printf("%s", request);
printf("📤 ===== END REQUEST =====\n");
printf("📤 REQUEST DUŽINA: %zu bajtova\n", strlen(request));



    printf("--- END REQUEST ---\n");

    // Make sure the request ends with \r\n\r\n
    size_t len = strlen(request);
    if (len < 4 || request[len-4] != '\r' || request[len-3] != '\n' || 
        request[len-2] != '\r' || request[len-1] != '\n') {
        printf("⚠️ Request may not end with \\r\\n\\r\\n!\n");
    }

    rc = tls_send(tls_conn, request, str_length(request));
    if (rc != EOK) {
        printf("TLS send failed: %s\n", str_error(rc));
        tls_close(tls_conn);
        return rc;
    }

    printf("Request sent, waiting for HTTPS response...\n");

    size_t total_size = 0;
    size_t buffer_size = 1048576;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        tls_close(tls_conn);
        return ENOMEM;
    }

    char read_buffer[16384];
    size_t nread;
    int recv_retries = 0;
    int headers_processed = 0;
    size_t body_start = 0;
    int http_status = 0;
    char *redirect_location = NULL;
    errno_t final_rc = EOK;

    size_t body_bytes_expected = 0;
    int has_content_length = 0;
    int is_chunked = 0;

    /* ========================================================================= */
    /* 1. FAZA: Prijem svih sirovih podataka sa mreže                            */
    /* ========================================================================= */
    while (1) {
        rc = tls_receive(tls_conn, read_buffer, sizeof(read_buffer), &nread);

        if (rc == EOK) {
            if (nread > 0) {
                if (total_size + nread >= buffer_size) {
                    buffer_size *= 4;
                    char *new_buffer = realloc(buffer, buffer_size);
                    if (!new_buffer) {
                        final_rc = ENOMEM;
                        break;
                    }
                    buffer = new_buffer;
                }
                
                memcpy(buffer + total_size, read_buffer, nread);
                total_size += nread;

                if (!headers_processed) {
                    if (total_size >= 8 && is_binary_image_data((unsigned char*)buffer, total_size)) {
                        printf("🖼️ Raw binary image detected (no HTTP headers)\n");
                        headers_processed = 1;
                        body_start = 0;
                        is_image = 1;
                        
                        unsigned char *image_data = malloc(total_size);
                        if (image_data) {
                            memcpy(image_data, buffer, total_size);
                            store_raw_image_data(url, image_data, total_size);
                        }
                        
                        *content = NULL;
                        *content_size = 0;
                        free(buffer);
                        tls_close(tls_conn);
                        return EOK;
                    }

                    int end_of_headers = find_end_of_headers(buffer, total_size);
                    if (end_of_headers != -1) {
                        headers_processed = 1;
                        body_start = end_of_headers;
                        
                        content_type = extract_header_value(buffer, "Content-Type");
                        if (content_type && strstr(content_type, "image/")) {
                            is_image = 1;
                            printf("🖼️ Image detected via Content-Type: %s\n", content_type);
                        }
                        if (content_type) free(content_type);

                        // Izvlacenje Content-Length (za standardne sajtove poput example.com)
                        char *cl_header = extract_header_value(buffer, "Content-Length");
                        if (cl_header) {
                            body_bytes_expected = strtoul(cl_header, NULL, 10);
                            has_content_length = 1;
                            free(cl_header);
                        }
                        
                        // Izvlacenje Transfer-Encoding (za chunked sajtove poput Google pretrage)
                        char *te_header = extract_header_value(buffer, "Transfer-Encoding");
                        if (te_header && strstr(te_header, "chunked")) {
                            is_chunked = 1;
                        }
                        if (te_header) free(te_header);
                        
                        http_status = parse_http_status(buffer);
                        printf("HTTP Status: %d\n", http_status);

                        if (http_status == 0) {
                            // Ako je status 0, proveravamo da li u baferu postoji Location zaglavlje
                            // pre nego sto forsiramo fallback na 200 OK
                            redirect_location = extract_header_value(buffer, "Location");
                            if (redirect_location) {
                                printf("⚠️ Detektovan skriveni redirect preko Location zaglavlja!\n");
                                http_status = 302; // Rucno podizemo status na 302 da se aktivira rekurzija
                            } else {
                                printf("⚠️ Detektovan status 0, forsiram fallback na 200 OK za stabilnost!\n");
                                http_status = 200; 
                            }
                        }                

                        if (http_status == 301 || http_status == 302 ||
                            http_status == 307 || http_status == 308) {
                            redirect_location = extract_header_value(buffer, "Location");
                            if (redirect_location) {
                                printf("Redirect to: %s\n", redirect_location);
                                break;
                            }
                        }
                    }
                }

                // === PAMETNI OSIGURAČI ZA KRAJ MREŽNOG PRENOSA ===
                if (headers_processed) {
                    // Prekid na osnovu poznate velicine tela odgovora
                    if (has_content_length) {
                        size_t current_body_bytes = total_size - body_start;
                        if (current_body_bytes >= body_bytes_expected) {
                            printf("✅ Završeno preko Content-Length.\n");
                            break; 
                        }
                    }
                    // Prekid na osnovu terminacionog čanka (0\r\n\r\n) kod Chunked strimova
                    else if (is_chunked && total_size >= 5) {
                        if (memcmp(buffer + total_size - 5, "0\r\n\r\n", 5) == 0) {
                            printf("✅ Završen Chunked strim (pronađen zadnji chunk 0\\r\\n\\r\\n).\n");
                            break;
                        }
                        else if (memcmp(buffer + total_size - 3, "0\n\n", 3) == 0) {
                            printf("✅ Završen Chunked strim (pronađen 0\\n\\n).\n");
                            break;
                        }
                    }
                }

                recv_retries = 0;
            } else {
                printf("Connection closed by server\n");
                break;
            }
        } else if (rc == EAGAIN) {
            recv_retries++;
            if (recv_retries > 5000) {
                printf("Receive timeout\n");
                final_rc = ETIMEDOUT;
                break;
            }
            srbinos_delay_1(10);
            continue;
        } else {
            printf("Receive error: %s\n", str_error(rc));
            final_rc = rc;
            break;
        }
    }

    /* ========================================================================= */
    /* 2. FAZA: Obrada nakon petlje (Kada su svi mrežni podaci sigurno u baferu) */
    /* ========================================================================= */

    if (final_rc != EOK) {
        if (buffer) free(buffer);
        tls_close(tls_conn);
        return final_rc;
    }

    // ===== DEBUG: Provera primljenog sadrzaja =====
    printf("📋 total_size: %zu\n", total_size);
    if (total_size > 0) {
        printf("📋 First 50 bytes of response:\n");
        for (size_t i = 0; i < 50 && i < total_size; i++) {
            if (buffer[i] >= 0x20 && buffer[i] < 0x7F) {
                putchar(buffer[i]); 
            } else {
                printf("[%02X]", (unsigned char)buffer[i]); 
            }
        }
        printf("\n");
    } else {
        printf("❌ Response is EMPTY!\n");
    }
    // ===============================================

    if (total_size == 0 || buffer == NULL) {
        printf("⚠️ Сервер вратио празан одговор (total_size=%zu, buffer=%p)\n", 
               total_size, (void*)buffer);
        if (buffer) free(buffer);
        tls_close(tls_conn);
        *content = NULL;
        *content_size = 0;
        return EIO;
    }

    // ===== ПРОВЕРА ДА ЛИ ИМАМО ВАЛИДАН HTML (BEZ HTTP ZAGLAVLJA) =====
    if (!headers_processed && total_size > 0 && 
        (strncasecmp(buffer, "<!doctype", 9) == 0 || 
         strncasecmp(buffer, "<html", 5) == 0 ||
         strncasecmp(buffer, "<HTML", 5) == 0 ||
         strstr(buffer, "<head") != NULL)) {
        
        printf("📄 Direktan HTML odgovor\n");
        *content = malloc(total_size + 1);
        if (*content) {
            memcpy(*content, buffer, total_size);
            (*content)[total_size] = '\0';
            *content_size = total_size;
            free(buffer);
            tls_close(tls_conn);
            return EOK;
        }
    }

    if (http_status == 0) {
        printf("⚠️ Status 0!\n");
        free(buffer);
        tls_close(tls_conn);
        return EIO;
    }

    // === REDIREKCIJE (POPRAVLJENO I OPERATIVNO) ===
    if (http_status == 301 || http_status == 302 ||
        http_status == 307 || http_status == 308) {
        if (redirect_location) {
            printf("🔄 [Network Redirect] Preusmeravam na novu adresu: %s (Broj preusmerenja: %d)\n", 
                   redirect_location, redirect_count);
            
            // 1. Gasimo stari TLS sloj
            tls_close(tls_conn);
            
            // 2. KRITIČNO: Gasimo i samu TCP vezu (socket) jer je server prekinuo kanal nakon 301!
            // Pretpostavljam da vaša biblioteka koristi tcp_close ili sličnu funkciju za tcp_conn.
            // Ako tcp_conn_t drži file descriptor (npr. fd), zatvaramo ga sistemski sa close()
            if (tcp_conn) {
                // Pozovite vašu sistemsku funkciju za zatvaranje socket-a, npr:
                // tcp_close(tcp_conn); 
                // ili direktno ako drži deskriptor: close(tcp_conn->fd);
            }
            
            // 3. Bezbjedno oslobađamo mrežni bafer
            if (buffer) {
                free(buffer);
                buffer = NULL;
            }

            char *new_content = NULL;
            size_t new_content_size = 0;
            tcp_conn_t new_tcp_conn; // Otvaramo potpuno svesku strukturu na steku za novu rekurziju

            // 4. Pokrećemo rekurziju sa potpuno NOVOM mrežnom strukturom socket-a
            // kako bi create_https_connection mogao ponovo da uradi DNS i poveže se na www.google.com
            errno_t redirect_rc = fetch_https_content(redirect_location, &new_tcp_conn,
                                                     &new_content, &new_content_size,
                                                     redirect_count + 1);
            
            free(redirect_location);
            
            if (redirect_rc == EOK) {
                *content = new_content;
                *content_size = new_content_size;
                
                // Kopiramo uspešnu novu konekciju natrag u izlazni parametar ako je potrebno spolja
                if (tcp_conn) {
                    memcpy(tcp_conn, &new_tcp_conn, sizeof(tcp_conn_t));
                }
            }
            
            return redirect_rc;
        }
    }

// cookie part:
    // === 2. PARSIRAJ SVE Set-Cookie ZAGLAVLJA (PRE CHUNKED!) ===
    char *set_cookie_ptr = buffer;
    while ((set_cookie_ptr = strstr(set_cookie_ptr, "Set-Cookie:")) != NULL) {
        // Proveri da li je ovaj Set-Cookie pre body_start (u zaglavljima)
        if (set_cookie_ptr >= buffer + body_start) break; // Sigurnosna provera
        
        char *line_end = strstr(set_cookie_ptr, "\r\n");
        if (line_end && line_end < buffer + body_start) { // Samo ako je u zaglavljima
            size_t line_len = line_end - set_cookie_ptr;
            char set_cookie_line[1024];
            if (line_len < sizeof(set_cookie_line)) {
                memcpy(set_cookie_line, set_cookie_ptr, line_len);
                set_cookie_line[line_len] = '\0';
                parse_set_cookie(set_cookie_line, &g_cookie_jar);
                printf("🍪 [Cookie] Parsiran kolačić: %s\n", set_cookie_line);
            }
        }
        set_cookie_ptr = line_end ? line_end + 2 : NULL;
    }
    //===========================================
 dump_fetch_cookies(&g_cookie_jar); 

    // === CHUNKED DEKODIRANJE ===
    if (is_chunked) {
        printf("Decoding chunked...\n");
        size_t decoded_len;
        char *decoded_body = decode_chunked_data(buffer + body_start, 
                                                 total_size - body_start, 
                                                 &decoded_len);
        if (decoded_body) {
            char *new_buffer = malloc(decoded_len + 1);
            if (new_buffer) {
                memcpy(new_buffer, decoded_body, decoded_len);
                new_buffer[decoded_len] = '\0';
                free(buffer);
                buffer = new_buffer;
                total_size = decoded_len;
                body_start = 0;
            }
            free(decoded_body);
        }
    }

    // =========================================================================
    // === COMPLETE DECOMPRESSION (GZIP + ZLIB/DEFLATE) ========================
    // =========================================================================
    int decompressed_ok = 0;
    if (headers_processed && body_start < total_size) {
        size_t compressed_size = total_size - body_start;
        unsigned char *compressed_data = (unsigned char*)(buffer + body_start);
        int is_gzip = (compressed_size >= 2 && compressed_data[0] == 0x1F && compressed_data[1] == 0x8B);
        int is_zlib = (compressed_size >= 2 && compressed_data[0] == 0x78 && 
                       (compressed_data[1] == 0x01 || compressed_data[1] == 0x5E || 
                        compressed_data[1] == 0x9C || compressed_data[1] == 0xDA));
        
        printf("🔍 Compression: %s\n", is_gzip ? "GZIP" : (is_zlib ? "ZLIB" : "NONE"));
        
        char *decomp_result = NULL;
        size_t decomp_size = 0;

        // === GZIP: tinfl_decompress ===
        if (is_gzip) {
            printf("🔄 GZIP detected, using tinfl_decompress...\n");
            size_t header_size = 10;
            if (compressed_size > 10) {
                unsigned char flags = compressed_data[3];
                if (flags & 0x04) {
                    if (compressed_size > header_size + 1) {
                        size_t extra_len = compressed_data[header_size] | (compressed_data[header_size+1] << 8);
                        header_size += 2 + extra_len;
                    }
                }
                if (flags & 0x08) {
                    while (header_size < compressed_size && compressed_data[header_size] != 0) header_size++;
                    header_size++;
                }
                if (flags & 0x10) {
                    while (header_size < compressed_size && compressed_data[header_size] != 0) header_size++;
                    header_size++;
                }
                if (flags & 0x02) header_size += 2;
            }
            
            if (header_size < compressed_size) {
                unsigned char out_buf[524288];
                size_t out_buf_remaining = sizeof(out_buf);
                const unsigned char *pIn_buf_next = compressed_data + header_size;
                size_t in_buf_remaining = compressed_size - header_size;
                
                tinfl_decompressor decomp;
                tinfl_init(&decomp);
                tinfl_status status = tinfl_decompress(&decomp, pIn_buf_next, &in_buf_remaining,
                                                       out_buf, out_buf, &out_buf_remaining, 0);
                
                if (status == TINFL_STATUS_DONE) {
                    decomp_size = sizeof(out_buf) - out_buf_remaining;
                    if (decomp_size > 8) decomp_size -= 8; // Makni gzip footer
                    
                    // Sredjivanje JSON/HTML krajeva
                    size_t json_end = 0; int brace_count = 0;
                    for (size_t i = 0; i < decomp_size; i++) {
                        if (out_buf[i] == '{') brace_count++;
                        else if (out_buf[i] == '}') {
                            brace_count--;
                            if (brace_count == 0) { json_end = i + 1; break; }
                        }
                    }
                    if (json_end > 0 && json_end < decomp_size) decomp_size = json_end;
                    while (decomp_size > 0 && (out_buf[decomp_size-1] < 32 || out_buf[decomp_size-1] > 126)) decomp_size--;
                    if (decomp_size > 0 && out_buf[decomp_size-1] == '0') decomp_size--;
                    
                    decomp_result = malloc(decomp_size + 1);
                    if (decomp_result) {
                        memcpy(decomp_result, out_buf, decomp_size);
                        decomp_result[decomp_size] = '\0';
                        decompressed_ok = 1;
                    }
                }
            }
        }
        // === ZLIB: uncompress ===
        else if (is_zlib) {
            printf("🔄 ZLIB detected, using uncompress...\n");
            unsigned long decompressed_size = compressed_size * 4;
            unsigned char *decompressed = malloc(decompressed_size);
            if (decompressed) {
                int ret = uncompress(decompressed, &decompressed_size, compressed_data, compressed_size);
                if (ret == Z_OK) {
                    while (decompressed_size > 0 && (decompressed[decompressed_size-1] < 32 || decompressed[decompressed_size-1] > 126)) decompressed_size--;
                    if (decompressed_size > 0 && decompressed[decompressed_size-1] == '0') decompressed_size--;
                    
                    decomp_result = malloc(decompressed_size + 1);
                    if (decomp_result) {
                        memcpy(decomp_result, decompressed, decompressed_size);
                        decomp_result[decompressed_size] = '\0';
                        decomp_size = decompressed_size;
                        decompressed_ok = 1;
                    }
                }
                free(decompressed);
            }
        }

        // AKO JE USPESNO DEKOMPRESOVANO: Menjamo buffer sa raspakovanim podacima
        if (decompressed_ok && decomp_result) {
            free(buffer);
            buffer = decomp_result;
            total_size = decomp_size;
            body_start = 0; // Telo sada krece od nule novog bafera
            printf("✅ Decompression successful. New clean size: %zu bytes\n", total_size);
        }
    }
    // ===================================================================

    // === OBRADA SLIKE ===
    if (is_image) {
        size_t image_size = total_size - body_start;
        unsigned char *image_data = malloc(image_size);
        if (image_data) {
            memcpy(image_data, buffer + body_start, image_size);
            store_raw_image_data(url, image_data, image_size);
        }
        *content = NULL;
        *content_size = 0;
        free(buffer);
        tls_close(tls_conn);
        return EOK;
    }

// === ČUVAMO CEODOKUPAN HTTP ODGOVOR (ZAGLAVLJA + TELO) ===
// Bez ikakvog memmove ili seckanja - čuvamo bafer onako kako je stigao sa mreže
// Ovo radi za sve tipove sadržaja: HTML, CSS, JS, JSON, slike, itd.

// 1. Prvo proverimo da li imamo ikakve podatke
if (total_size == 0 || buffer == NULL) {
    // Ako nema podataka, vratimo prazan odgovor
    if (buffer) free(buffer);
    *content = NULL;
    *content_size = 0;
    tls_close(tls_conn);
    return EIO;
}

// 2. Sačuvamo ceo primljeni bafer - bez ikakvog prepisivanja!
*content = buffer;
*content_size = total_size;  // Ovo je UKUPNA veličina (zaglavlja + telo)

// 3. Dodamo null-terminator na kraj za bezbednost (ali ne skraćujemo sadržaj!)
// Ako bafer već nije null-terminovan, napravimo novi sa +1
if (total_size > 0 && buffer[total_size - 1] != '\0') {
    char *null_terminated = realloc(buffer, total_size + 1);
    if (null_terminated) {
        null_terminated[total_size] = '\0';
        *content = null_terminated;
        // Ako smo uspešno alocirali novi, buffer je oslobođen od strane realloc
        // ali *content sada pokazuje na novi bafer
    }
    // Ako realloc ne uspe, ostaje originalni buffer (možda bez \0)
    // ali to nije kritično - parsiranje će raditi sa total_size
}

    tls_close(tls_conn);
    return EOK;
}


bool is_https_url(const char *url) {
    return (str_ncmp(url, "https://", 8) == 0);
}

// Hardware poll for entropy
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    for (size_t i = 0; i < len; i++) {
        output[i] = rand() & 0xFF;
    }
    *olen = len;
    return 0;
}

// Simplified debug handshake details for mbed TLS 3.2.1
void debug_handshake_details(pauk_tls_connection_t *conn, int ret) {
    char error_buf[512];
    mbedtls_strerror(ret, error_buf, sizeof(error_buf));
    
    printf("\n=== TLS Handshake Failed ===\n");
    printf("Error: %s (code: %d, 0x%X)\n", error_buf, ret, -ret);
    
    // Only show detailed info for certificate errors
    if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&conn->ssl);
        if (flags != 0) {
            char vrfy_buf[1024];
            mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  - ", flags);
            printf("Certificate issues:\n%s\n", vrfy_buf);
        }
    }
    
    // Show common error categories
    if (ret == MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE) {
        printf("Likely cause: No common ciphersuite or protocol version\n");
    } else if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
        printf("Likely cause: Server rejected the connection\n");
    } else if (ret == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
        printf("Likely cause: Certificate validation failed\n");
    }
    
    printf("=== End Error Details ===\n\n");
}

errno_t fetch_css_file(const char *url, char **content, size_t *size) {
    // Reuse your existing fetch function
    return fetch_https_content(url, NULL, content, size, 0);
}


void parse_css_from_string(const char *css) {
    if (!css) return;
    
    char *css_copy = strdup(css);
    char *saveptr;
    
    char *rule_str = strtok_r(css_copy, "}", &saveptr);
    while (rule_str) {
        // Find the opening brace
        char *brace = strchr(rule_str, '{');
        if (!brace) {
            rule_str = strtok_r(NULL, "}", &saveptr);
            continue;
        }
        
        // Extract selector (everything before '{')
        *brace = '\0';
        char *selector = rule_str;
        
        // Extract declarations (everything after '{')
        char *declarations = brace + 1;
        
        // Clean selector: remove all whitespace from ends
        while (*selector == ' ' || *selector == '\t' || *selector == '\n' || *selector == '\r') selector++;
        char *end = selector + strlen(selector) - 1;
        while (end > selector && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
        *(end + 1) = '\0';
        
        if (strlen(selector) == 0) {
            rule_str = strtok_r(NULL, "}", &saveptr);
            continue;
        }
        
        // Parse declarations
        char *decl_saveptr;
        char *decl = strtok_r(declarations, ";", &decl_saveptr);
        while (decl) {
            // Clean decl
            while (*decl == ' ' || *decl == '\t' || *decl == '\n' || *decl == '\r') decl++;
            end = decl + strlen(decl) - 1;
            while (end > decl && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
            *(end + 1) = '\0';
            
            char *colon = strchr(decl, ':');
            if (colon) {
                *colon = '\0';
                char *property = decl;
                char *value = colon + 1;
                
                // Clean property
                while (*property == ' ' || *property == '\t' || *property == '\n' || *property == '\r') property++;
                end = property + strlen(property) - 1;
                while (end > property && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
                *(end + 1) = '\0';
                
                // Clean value
                while (*value == ' ' || *value == '\t' || *value == '\n' || *value == '\r') value++;
                end = value + strlen(value) - 1;
                while (end > value && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
                *(end + 1) = '\0';
                
                if (strlen(property) > 0 && strlen(value) > 0) {
                    css_rules = realloc(css_rules, (css_rule_count + 1) * sizeof(CSSRule));
                    css_rules[css_rule_count].selector = strdup(selector);
                    css_rules[css_rule_count].property = strdup(property);
                    css_rules[css_rule_count].value = strdup(value);
                    css_rule_count++;
                    
                  //  printf("✅ Added: '%s' { %s: %s }\n", selector, property, value);
                }
            }
            decl = strtok_r(NULL, ";", &decl_saveptr);
        }
        rule_str = strtok_r(NULL, "}", &saveptr);
    }
    
    free(css_copy);
   // printf("Total CSS rules: %d\n", css_rule_count);
}


// Simple CSS fetcher - reuse your HTTPS fetch
errno_t fetch_css_resource(const char *url, char **content, size_t *size) {
    if (!url || !content || !size) {
        return EINVAL;
    }
    
    // Debug: Print what we're trying to fetch
    printf("🔍 fetch_css_resource called with: %s\n", url);
    
    // ===== USE CENTRALIZED FILTER =====
    if (is_resource_blocked(url)) {
        printf("⏭️ [V1] Blocking resource (%s): %s\n", 
               get_block_reason(url), url);
        return ENOTSUP;
    }
    // =================================

    // ===== LOKALNI FAJLOVI - PRESKOČI SVE =====
    if (strncmp(url, "file://", 7) == 0) {
        const char *file_path = url + 7;
        
        // Security: Prevent directory traversal
        if (strstr(file_path, "..") != NULL) {
            printf("❌ Security: Directory traversal attempt: %s\n", file_path);
            return EPERM;
        }
        
        printf("📁 Loading local CSS file: %s\n", file_path);
        
        FILE *f = fopen(file_path, "rb");
        if (!f) {
            printf("❌ Cannot open file: %s\n", file_path);
            return EIO;
        }
        
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (file_size > 1024 * 1024) {
            printf("❌ CSS file too large: %ld bytes\n", file_size);
            fclose(f);
            return EFBIG;
        }
        
        if (file_size <= 0) {
            fclose(f);
            return EIO;
        }
        
        *content = malloc(file_size + 1);
        if (!*content) {
            fclose(f);
            return ENOMEM;
        }
        
        size_t read = fread(*content, 1, file_size, f);
        fclose(f);
        
        if (read != (size_t)file_size) {
            free(*content);
            *content = NULL;
            return EIO;
        }
        
        (*content)[file_size] = '\0';
        *size = file_size;
        
        printf("✅ Loaded local CSS: %s (%ld bytes)\n", file_path, file_size);
        return EOK;
    }

    // ===== SAMO ZA HTTP/HTTPS: OČISTI :80 =====
    char *clean_url = NULL;
    const char *working_url = url;
    
    if (strstr(url, ":80/") != NULL) {
        clean_url = strdup(url);
        if (clean_url) {
            char *p = strstr(clean_url, ":80/");
            if (p) {
                memmove(p, p + 3, strlen(p + 3) + 1);
                printf("🔄 [fetch_css_resource] Uklanjam :80: %s -> %s\n", url, clean_url);
                working_url = clean_url;
            } else {
                free(clean_url);
                clean_url = NULL;
            }
        }
    }

    // ===== SADA ODREDI PROTOKOL I PORT NA OSNOVU OČIŠĆENOG URL-A =====
    int is_https = (strstr(working_url, "https://") != NULL);
    uint16_t port = is_https ? 443 : 80;

    // Izvačenje hostname-a iz OČIŠĆENOG URL-A
    char hostname[256];
    const char *host_start = strstr(working_url, "://");
    if (host_start) {
        host_start += 3;
        const char *host_end = strchr(host_start, '/');
        int len = host_end ? (host_end - host_start) : (int)strlen(host_start);
        if (len > 255) len = 255;
        strncpy(hostname, host_start, len);
        hostname[len] = '\0';

        // Proveri da li hostname ima port
        char *port_ptr = strchr(hostname, ':');
        if (port_ptr) {
            *port_ptr = '\0';
            port = (uint16_t)strtoul(port_ptr + 1, NULL, 10);
            is_https = (port == 443);
        }
    } else {
        printf("❌ fetch_css_resource: Nevalidan URL format\n");
        if (clean_url) free(clean_url);
        return EINVAL;
    }

    // Resolving host and creating connection
    inet_addr_t addr;
    errno_t rc = resolve_host(hostname, &addr);
    if (rc != EOK) {
        printf("❌ fetch_css_resource: Resolving failed: %s\n", hostname);
        if (clean_url) free(clean_url);
        return rc;
    }

    tcp_t *tcp = NULL;
    tcp_conn_t *conn = NULL;
    rc = create_tcp_connection(addr, port, &tcp, &conn);
    if (rc != EOK) {
        printf("❌ fetch_css_resource: Connection failed to %s:%d\n", hostname, port);
        if (clean_url) free(clean_url);
        return rc;
    }

    // Poziv sa OČIŠĆENIM URL-om
    if (is_https) {
        rc = fetch_https_content(working_url, conn, content, size, 0);
    } else {
        rc = fetch_http_content(working_url, conn, content, size, 0);
    }

    // Čišćenje
    if (clean_url) free(clean_url);
    tcp_conn_destroy(conn);
    tcp_destroy(tcp);

    return rc;
}


// Parse CSS string and add to global css_rules
void parse_css_and_add_rules(const char *css) {
    if (!css) return;
    
    char *css_copy = strdup(css);
    char *saveptr;
    
    char *rule_str = strtok_r(css_copy, "}", &saveptr);
    while (rule_str) {
        char *clean_rule = malloc(strlen(rule_str) + 1);
        int r_idx = 0;
        
        for (size_t i = 0; i < strlen(rule_str); i++) {
            if (rule_str[i] == '\n' || rule_str[i] == '\r' || rule_str[i] == '\t') {
                if (r_idx > 0 && clean_rule[r_idx - 1] != ' ') {
                    clean_rule[r_idx++] = ' ';
                }
            }
            else if ((unsigned char)rule_str[i] >= 32 && (unsigned char)rule_str[i] <= 126) {
                clean_rule[r_idx++] = rule_str[i];
            }
        }
        clean_rule[r_idx] = '\0';
        
        char *brace = strchr(clean_rule, '{');
        if (brace) {
            *brace = '\0';
            char *full_selector = clean_rule;
            char *declarations = brace + 1;
            
            // 🚀 SPASILAC ZA MREŽU: Delimo grupne selektore (npr. .main-nav, .hide) po zarezu
            char *sub_sel_saveptr;
            char *sub_selector = strtok_r(full_selector, ",", &sub_sel_saveptr);
            
            while (sub_selector) {
                // Trim pod-selektora
                while (*sub_selector == ' ' || *sub_selector == '\t') sub_selector++;
                char *sel_end = sub_selector + strlen(sub_selector) - 1;
                while (sel_end > sub_selector && (*sel_end == ' ' || *sel_end == '\t')) sel_end--;
                *(sel_end + 1) = '\0';
                
                if (strlen(sub_selector) > 0) {
                    // 🚀 POPRAVAK ZA NESTED SELEKTORE:
                    // Ako selektor ima razmak (npr. ".horizontal-nav ul"), siječemo ga na tom razmaku
                    // da bi tvoj trenutni matcher dobio čistu krovnu klasu ".horizontal-nav" i izbjegao mrak na ekranu.
                    char *space = strchr(sub_selector, ' ');
                    if (space) {
                        *space = '\0';
                        
                        // Ponovo trimujemo nakon sijecenja za svaki slucaj
                        char *final_end = sub_selector + strlen(sub_selector) - 1;
                        while (final_end > sub_selector && (*final_end == ' ' || *final_end == '\t')) final_end--;
                        *(final_end + 1) = '\0';
                    }

                    if (strlen(sub_selector) > 0) {
                        // Pošto strtok_r u process_single_selector_rule modifikuje deklaracije, 
                        // moramo poslati duplikat stringa jer imamo više selektora koji dele iste stilove
                        char *declarations_copy = strdup(declarations);
                        process_single_selector_rule(sub_selector, declarations_copy);
                        free(declarations_copy);
                    }
                }
                
                sub_selector = strtok_r(NULL, ",", &sub_sel_saveptr);
            }
        }
        free(clean_rule);
        rule_str = strtok_r(NULL, "}", &saveptr);
    }
    
    free(css_copy);
}



void process_single_selector_rule(const char *clean_selector, char *declarations) {
    char *decl_saveptr;
    char *decl = strtok_r(declarations, ";", &decl_saveptr);
    
    while (decl) {
        char *clean_decl = malloc(strlen(decl) + 1);
        int d_idx = 0;
        
        for (size_t i = 0; i < strlen(decl); i++) {
            if (decl[i] == '\n' || decl[i] == '\r' || decl[i] == '\t') {
                if (d_idx > 0 && clean_decl[d_idx - 1] != ' ') {
                    clean_decl[d_idx++] = ' ';
                }
            }
            else if ((unsigned char)decl[i] >= 32 && (unsigned char)decl[i] <= 126) {
                clean_decl[d_idx++] = decl[i];
            }
        }
        clean_decl[d_idx] = '\0';
        
        char *colon = strchr(clean_decl, ':');
        if (colon) {
            *colon = '\0';
            char *property = clean_decl;
            char *value = colon + 1;
            
            while (*property == ' ' || *property == '\t') property++;
            char *prop_end = property + strlen(property) - 1;
            while (prop_end > property && (*prop_end == ' ' || *prop_end == '\t')) prop_end--;
            *(prop_end + 1) = '\0';
            
            while (*value == ' ' || *value == '\t') value++;
            char *val_end = value + strlen(value) - 1;
            while (val_end > value && (*val_end == ' ' || *val_end == '\t')) val_end--;
            *(val_end + 1) = '\0';
            
            if (strlen(property) > 0 && strlen(value) > 0) {
                extern CSSRule *css_rules;
                extern int css_rule_count;
                
                css_rules = realloc(css_rules, (css_rule_count + 1) * sizeof(CSSRule));
                css_rules[css_rule_count].selector = strdup(clean_selector);
                css_rules[css_rule_count].property = strdup(property);
                css_rules[css_rule_count].value = strdup(value);
                css_rule_count++;
            }
        }
        free(clean_decl);
        decl = strtok_r(NULL, ";", &decl_saveptr);
    }
}

compression_type_t detect_compression(
    const unsigned char *data,
    size_t size,
    const char *content_encoding)
{
    if (!data || size < 2)
        return COMPRESSION_NONE;

    // First, try magic bytes (most reliable)
    if (data[0] == 0x1F && data[1] == 0x8B) {
        return COMPRESSION_GZIP;
    }

    if (data[0] == 0x78) {
        switch (data[1]) {
            case 0x01:
            case 0x5E:
            case 0x9C:
            case 0xDA:
                return COMPRESSION_ZLIB;
        }
    }

    // Then fallback to Content-Encoding header
    if (content_encoding) {
        if (strstr(content_encoding, "gzip")) return COMPRESSION_GZIP;
        if (strstr(content_encoding, "deflate")) return COMPRESSION_DEFLATE;
        if (strstr(content_encoding, "br")) {
            printf("⚠️ Brotli not supported\n");
            return COMPRESSION_BROTLI;
        }
    }

    return COMPRESSION_NONE;
}


// New function that keeps connection open
errno_t fetch_https_content_keep_alive(const char *url,
    char **content, size_t *content_size)
{
    if (!url || !content || !content_size)
        return EINVAL;

    *content = NULL;
    *content_size = 0;

    char hostname[256];

    errno_t rc = extract_hostname(url,
                                  hostname,
                                  sizeof(hostname));

    if (rc != EOK)
        return rc;

    int need_new_connection = 0;

    if (!g_keepalive_tls || !g_keepalive_conn) {

        need_new_connection = 1;
        printf("🔌 No existing connection, creating new one\n");

    } else if (strcmp(g_keepalive_host, hostname) != 0) {

        need_new_connection = 1;

        printf("🔌 Host changed (%s -> %s), creating new connection\n",
               g_keepalive_host,
               hostname);

        close_keepalive_connection();

    } else {

        printf("♻️ Reusing existing connection to %s\n",
               hostname);
    }

    if (need_new_connection) {

        printf("🔌 Creating new keep-alive connection to %s\n",
               hostname);

        inet_addr_t addr;

        rc = resolve_host(hostname, &addr);

        if (rc != EOK)
            return rc;

        tcp_t *tcp = NULL;

        rc = create_tcp_connection(addr,
                                   443,
                                   &tcp,
                                   &g_keepalive_conn);

        if (rc != EOK)
            return rc;

        rc = create_https_connection(hostname,
                                     g_keepalive_conn,
                                     &g_keepalive_tls);

        if (rc != EOK) {

            tcp_conn_destroy(g_keepalive_conn);
            g_keepalive_conn = NULL;

            return rc;
        }

        strcpy(g_keepalive_host, hostname);
    }

    const char *path = extract_path(url);

    char request[2048];

    snprintf(request,
             sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: keep-alive\r\n"
             "User-Agent: PaukBrowser/1.0\r\n"
             "\r\n",
             path,
             hostname);

    rc = tls_send(g_keepalive_tls,
                  request,
                  strlen(request));

    if (rc != EOK) {

        printf("❌ TLS send failed\n");

        close_keepalive_connection();

        return rc;
    }

    size_t total_size = 0;
    size_t buffer_size = 65536;

    char *buffer = malloc(buffer_size);

    if (!buffer)
        return ENOMEM;

    char read_buffer[16384];
    size_t nread = 0;

    int headers_processed = 0;
    size_t body_start = 0;

    int content_length = -1;
    int is_chunked = 0;  // NEW

    uint64_t last_progress = get_uptime_ms();
    uint64_t download_start = last_progress;

  //  printf("📥 Downloading: %s\n", url);

    while (1) {

        rc = tls_receive(g_keepalive_tls,
                         read_buffer,
                         sizeof(read_buffer),
                         &nread);

        if (rc == EOK) {

            if (nread > 0) {

                last_progress = get_uptime_ms();

                if (total_size + nread >= buffer_size) {

                    while (total_size + nread >= buffer_size)
                        buffer_size *= 2;

                    char *new_buffer =
                        realloc(buffer, buffer_size);

                    if (!new_buffer) {

                        free(buffer);
                        return ENOMEM;
                    }

                    buffer = new_buffer;

                    printf("📦 Buffer expanded to %.2f MB\n",
                           buffer_size /
                           (1024.0 * 1024.0));
                }

                memcpy(buffer + total_size,
                       read_buffer,
                       nread);

                total_size += nread;

                if (!headers_processed) {

                    int end =
                        find_end_of_headers(buffer,
                                            total_size);

                    if (end != -1) {

                        headers_processed = 1;
                        body_start = end;

                        char *cl =
                            extract_header_value(
                                buffer,
                                "Content-Length");

                        if (cl) {

                            content_length = atoi(cl);

                            printf("📋 Content-Length: %d bytes (%.2f KB)\n",
                                   content_length,
                                   content_length / 1024.0);

                            free(cl);
                        }
                        
                        // NEW: Check for chunked encoding
                        char *te = extract_header_value(buffer, "Transfer-Encoding");
                        if (te) {
                            if (strstr(te, "chunked")) {
                                is_chunked = 1;
                                printf("📦 Transfer-Encoding: chunked\n");
                            }
                            free(te);
                        }
                    }
                }

                // NEW: For chunked encoding, check for termination
                if (headers_processed && is_chunked) {
                    // Look for "0\r\n\r\n" at the end
                    if (total_size >= body_start + 5) {
                        char *end_check = buffer + total_size - 5;
                        if (memcmp(end_check, "0\r\n\r\n", 5) == 0) {
                            printf("✅ Chunked download complete\n");
                            break;
                        }
                    }
                }

                if (headers_processed && !is_chunked && content_length > 0) {

                    size_t received =
                        total_size - body_start;

                    if ((received % (100 * 1024)) < nread ||
                        received >= (size_t)content_length)
                    {
                        printf("📥 Progress: %.2f KB / %.2f KB (%.1f%%)\n",
                               received / 1024.0,
                               content_length / 1024.0,
                               (received * 100.0) /
                               content_length);
                    }

                    if (received >=
                        (size_t)content_length)
                    {
                        printf("✅ Download complete: %zu bytes\n",
                               received);

                        break;
                    }
                }

            } else {

                printf("📥 Server closed connection\n");
                // For chunked encoding, server close means complete
                if (is_chunked && headers_processed) {
                    printf("✅ Server closed after chunked transfer\n");
                    break;
                }
                break;
            }

        } else if (rc == EAGAIN) {

            uint64_t now = get_uptime_ms();

            if ((now - last_progress) > 30000) {

                size_t received =
                    headers_processed ?
                    (total_size - body_start) : 0;

                printf("❌ Download stalled\n");

                if (content_length > 0) {

                    printf("❌ Received %zu / %d bytes (%.1f%%)\n",
                           received,
                           content_length,
                           (received * 100.0) /
                           content_length);
                }

                free(buffer);
                return ETIMEOUT;
            }

            if ((now - download_start) > 300000) {

                printf("❌ Download exceeded 5 minutes\n");

                free(buffer);
                return ETIMEOUT;
            }

            fibril_usleep(10000);
            continue;

        } else {

            printf("❌ Receive error: %s\n",
                   str_error(rc));

            free(buffer);
            return rc;
        }
    }

    if (!headers_processed || body_start >= total_size) {

        printf("❌ No response body\n");

        free(buffer);

        return EIO;
    }

    size_t body_size = total_size - body_start;
    char *body_data = buffer + body_start;
    
    // NEW: For JPEG images, verify EOI marker
    const char *ext = strrchr(url, '.');
    if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        if (body_size > 2) {
            unsigned char *last_two = (unsigned char*)body_data + body_size - 2;
            if (last_two[0] == 0xFF && last_two[1] == 0xD9) {
                printf("✅ JPEG EOI marker verified\n");
            } else {
                printf("⚠️ JPEG missing EOI marker - file incomplete!\n");
                free(buffer);
                return EIO;
            }
        }
    }

    // NEW: reject incomplete download for non-chunked
    if (!is_chunked && content_length > 0 &&
        body_size < (size_t)content_length)
    {
        printf("❌ Incomplete download (%zu/%d)\n",
               body_size,
               content_length);

        free(buffer);

        return EIO;
    }

    // For chunked encoding, decode the body
    if (is_chunked) {
        size_t decoded_size = 0;
        char *decoded = decode_chunked_data(body_data, body_size, &decoded_size);
        if (decoded) {
            free(buffer);
            buffer = decoded;
            body_size = decoded_size;
            printf("✅ Decoded chunked data: %zu bytes\n", body_size);
        } else {
            printf("❌ Failed to decode chunked data\n");
            free(buffer);
            return EIO;
        }
    } else {
        // Just copy the body
        char *new_buffer = malloc(body_size + 1);
        if (!new_buffer) {
            free(buffer);
            return ENOMEM;
        }
        memcpy(new_buffer, body_data, body_size);
        new_buffer[body_size] = '\0';
        free(buffer);
        buffer = new_buffer;
    }

    *content = buffer;
    *content_size = body_size;

    printf("✅ Extracted body: %zu bytes\n", body_size);

    return EOK;
}

errno_t fetch_http_content_keep_alive(const char *url, char **content, size_t *content_size) {
    if (!url || !content || !content_size) return EINVAL;
    
    char hostname[256];
    uint16_t port = 80;
    errno_t rc = extract_hostname_and_port(url, hostname, sizeof(hostname), &port);
    if (rc != EOK) return rc;
    
    // Create new connection if host changed or no connection exists
    if (!g_http_keepalive_conn || strcmp(g_http_keepalive_host, hostname) != 0) {
        if (g_http_keepalive_conn) {
            tcp_conn_destroy(g_http_keepalive_conn);
            g_http_keepalive_conn = NULL;
        }
        
        printf("🔌 Creating new HTTP keep-alive connection to %s\n", hostname);
        
        inet_addr_t addr;
        rc = resolve_host(hostname, &addr);
        if (rc != EOK) return rc;
        
        tcp_t *tcp = NULL;
        rc = create_tcp_connection(addr, port, &tcp, &g_http_keepalive_conn);
        if (rc != EOK) return rc;
        
        strcpy(g_http_keepalive_host, hostname);
    } else {
        printf("♻️ Reusing HTTP keep-alive connection to %s\n", hostname);
    }
    
    // Send HTTP request (same as HTTPS version but without TLS)
    const char *path = extract_path(url);
    char request[2048];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: PaukBrowser/1.0\r\n"
        "\r\n",
        path, hostname);
    
    rc = tcp_conn_send(g_http_keepalive_conn, request, strlen(request));
    if (rc != EOK) return rc;
    
    // Receive response (same as HTTPS version but without TLS)
    // ... (similar receive loop)
    
    return EOK;
}

// In network.c (or wherever close_keepalive_connection is)

void close_keepalive_connection(void) {
    // ===== SAFETY GUARD =====
    // Check if already closed
    if (g_keepalive_tls == NULL && g_keepalive_conn == NULL) {
        // printf("🔌 Keep-alive already closed, skipping\n");
        return;
    }
    // ========================
    
    // printf("🔌 Closing keep-alive connection...\n");
    
    // ===== SNAPSHOT =====
    void *tls_conn = g_keepalive_tls;
    tcp_conn_t *tcp_conn = g_keepalive_conn;
    // ====================
    
    // ===== CLEAR FIRST =====
    g_keepalive_tls = NULL;
    g_keepalive_conn = NULL;
    // =======================
    
    // ===== CLOSE =====
    if (tls_conn) {
        tls_close(tls_conn);
    }
    if (tcp_conn) {
        tcp_conn_destroy(tcp_conn);
    }
    // =================
    
    // printf("🔌 Keep-alive connection closed\n");
}


void init_connection_pool(void) {
    if (g_pool_initialized) return;
    
    fibril_mutex_initialize(&g_pool_mutex);
    fibril_mutex_initialize(&g_tls_handshake_mutex);  // ← ADD THIS LINE
    
    for (int i = 0; i < MAX_POOL_CONNECTIONS; i++) {
        g_pool.connections[i].in_use = 0;
        g_pool.connections[i].tcp_conn = NULL;
        g_pool.connections[i].tls_conn = NULL;
        g_pool.connections[i].host[0] = '\0';
    }
    
    g_pool_initialized = 1;
    printf("🔌 Connection pool initialized with %d slots\n", MAX_POOL_CONNECTIONS);
}

errno_t get_pooled_connection(const char *hostname, int port, 
    tcp_conn_t **out_tcp, void **out_tls) {
if (!g_pool_initialized) init_connection_pool();

fibril_mutex_lock(&g_pool.mutex);

// 1. Try to find an existing idle connection
for (int i = 0; i < MAX_POOL_CONNECTIONS; i++) {
PooledConnection *conn = &g_pool.connections[i];
if (!conn->in_use && conn->tcp_conn != NULL && 
strcmp(conn->host, hostname) == 0 && conn->port == port) {
conn->in_use = 1;
conn->last_used = get_uptime_ms();
conn->request_count++;
*out_tcp = conn->tcp_conn;
*out_tls = conn->tls_conn;
printf("♻️ Reusing pooled connection #%d to %s\n", i, hostname);
fibril_mutex_unlock(&g_pool.mutex);
return EOK;
}
}

// 2. Find an empty slot for a new connection
for (int i = 0; i < MAX_POOL_CONNECTIONS; i++) {
PooledConnection *conn = &g_pool.connections[i];
if (!conn->in_use && conn->tcp_conn == NULL) {
printf("🔌 Creating new pooled connection #%d to %s:%d\n", i, hostname, port);

// ===== MARK AS IN USE, THEN RELEASE MUTEX =====
conn->in_use = 1;
strcpy(conn->host, hostname);
conn->port = port;
fibril_mutex_unlock(&g_pool.mutex);  // ← RELEASE BEFORE BLOCKING!
// =============================================

// Now create the connection (outside the mutex)
inet_addr_t addr;
errno_t rc = resolve_host(hostname, &addr);
if (rc != EOK) {
conn->in_use = 0;
return rc;
}

tcp_t *tcp = NULL;
rc = create_tcp_connection(addr, port, &tcp, &conn->tcp_conn);
if (rc != EOK) {
conn->in_use = 0;
return rc;
}

if (port == 443) {
rc = create_https_connection(hostname, conn->tcp_conn, &conn->tls_conn);
if (rc != EOK) {
tcp_conn_destroy(conn->tcp_conn);
conn->tcp_conn = NULL;
conn->in_use = 0;
return rc;
}
} else {
conn->tls_conn = NULL;
}

conn->last_used = get_uptime_ms();
conn->request_count = 1;

*out_tcp = conn->tcp_conn;
*out_tls = conn->tls_conn;
return EOK;
}
}

// 3. Pool is full
fibril_mutex_unlock(&g_pool.mutex);
printf("⏳ Connection pool full, waiting...\n");
fibril_usleep(100 * 1000);
return get_pooled_connection(hostname, port, out_tcp, out_tls);
}

void release_pooled_connection(tcp_conn_t *tcp_conn) {
    fibril_mutex_lock(&g_pool.mutex);
    for (int i = 0; i < MAX_POOL_CONNECTIONS; i++) {
        PooledConnection *conn = &g_pool.connections[i];
        if (conn->tcp_conn == tcp_conn) {
            conn->in_use = 0;
            conn->last_used = get_uptime_ms();
            printf("🔓 Released connection #%d to %s\n", i, conn->host);
            fibril_mutex_unlock(&g_pool.mutex);
            return;
        }
    }
    fibril_mutex_unlock(&g_pool.mutex);
}

void close_connection_pool(void) {
    if (!g_pool_initialized) return;
    fibril_mutex_lock(&g_pool.mutex);
    for (int i = 0; i < MAX_POOL_CONNECTIONS; i++) {
        PooledConnection *conn = &g_pool.connections[i];
        if (conn->tcp_conn) {
            if (conn->tls_conn) {
                tls_close(conn->tls_conn);
            }
            tcp_conn_destroy(conn->tcp_conn);
            conn->tcp_conn = NULL;
            conn->tls_conn = NULL;
            conn->host[0] = '\0';
            conn->in_use = 0;
        }
    }
    fibril_mutex_unlock(&g_pool.mutex);
    g_pool_initialized = 0;
    printf(" Connection pool closed\n");
}


errno_t fetch_https_content_pooled(const char *url,
    char **content, size_t *content_size) {
if (!url || !content || !content_size)
return EINVAL;

*content = NULL;
*content_size = 0;

char hostname[256];
errno_t rc = extract_hostname(url, hostname, sizeof(hostname));
if (rc != EOK) return rc;

// Get a connection from the pool
tcp_conn_t *tcp_conn = NULL;
void *tls_conn = NULL;
rc = get_pooled_connection(hostname, 443, &tcp_conn, &tls_conn);
if (rc != EOK) return rc;

const char *path = extract_path(url);
char request[2048];
snprintf(request, sizeof(request),
"GET %s HTTP/1.1\r\n"
"Host: %s\r\n"
"Connection: keep-alive\r\n"
"User-Agent: PaukBrowser/1.0\r\n"
"\r\n",
path, hostname);

rc = tls_send(tls_conn, request, strlen(request));
if (rc != EOK) {
release_pooled_connection(tcp_conn);
return rc;
}

size_t total_size = 0;
size_t buffer_size = 65536;
char *buffer = malloc(buffer_size);
if (!buffer) {
release_pooled_connection(tcp_conn);
return ENOMEM;
}

char read_buffer[16384];
size_t nread = 0;
int headers_processed = 0;
size_t body_start = 0;
int content_length = -1;
int is_chunked = 0;
uint64_t last_progress = get_uptime_ms();
uint64_t download_start = last_progress;

//printf("📥 Downloading: %s\n", url);

while (1) {
rc = tls_receive(tls_conn, read_buffer, sizeof(read_buffer), &nread);

if (rc == EOK) {
if (nread > 0) {
last_progress = get_uptime_ms();

if (total_size + nread >= buffer_size) {
while (total_size + nread >= buffer_size)
buffer_size *= 2;
char *new_buffer = realloc(buffer, buffer_size);
if (!new_buffer) {
free(buffer);
release_pooled_connection(tcp_conn);
return ENOMEM;
}
buffer = new_buffer;
printf("📦 Buffer expanded to %.2f MB\n",
buffer_size / (1024.0 * 1024.0));
}

memcpy(buffer + total_size, read_buffer, nread);
total_size += nread;

if (!headers_processed) {
int end = find_end_of_headers(buffer, total_size);
if (end != -1) {
headers_processed = 1;
body_start = end;

char *cl = extract_header_value(buffer, "Content-Length");
if (cl) {
content_length = atoi(cl);
printf("📋 Content-Length: %d bytes (%.2f KB)\n",
    content_length, content_length / 1024.0);
free(cl);
}

// Check for chunked encoding
char *te = extract_header_value(buffer, "Transfer-Encoding");
if (te) {
if (strstr(te, "chunked")) {
 is_chunked = 1;
 printf("📦 Transfer-Encoding: chunked\n");
}
free(te);
}
}
}

// For chunked encoding, check for termination
if (headers_processed && is_chunked) {
if (total_size >= body_start + 5) {
char *end_check = buffer + total_size - 5;
if (memcmp(end_check, "0\r\n\r\n", 5) == 0) {
printf("✅ Chunked download complete\n");
break;
}
}
}

if (headers_processed && !is_chunked && content_length > 0) {
size_t received = total_size - body_start;
if ((received % (100 * 1024)) < nread ||
received >= (size_t)content_length) {
printf("📥 Progress: %.2f KB / %.2f KB (%.1f%%)\n",
received / 1024.0,
content_length / 1024.0,
(received * 100.0) / content_length);
}

if (received >= (size_t)content_length) {
printf("✅ Download complete: %zu bytes\n", received);
break;
}
}

} else {
printf("📥 Server closed connection\n");
if (is_chunked && headers_processed) {
printf("✅ Server closed after chunked transfer\n");
break;
}
break;
}

} else if (rc == EAGAIN) {
uint64_t now = get_uptime_ms();

if ((now - last_progress) > 30000) {
size_t received = headers_processed ? (total_size - body_start) : 0;
printf("❌ Download stalled\n");
if (content_length > 0) {
printf("❌ Received %zu / %d bytes (%.1f%%)\n",
received, content_length,
(received * 100.0) / content_length);
}
free(buffer);
release_pooled_connection(tcp_conn);
return ETIMEOUT;
}

if ((now - download_start) > 300000) {
printf("❌ Download exceeded 5 minutes\n");
free(buffer);
release_pooled_connection(tcp_conn);
return ETIMEOUT;
}

fibril_usleep(10000);
continue;

} else {
printf("❌ Receive error: %s\n", str_error(rc));
free(buffer);
release_pooled_connection(tcp_conn);
return rc;
}
}

if (!headers_processed || body_start >= total_size) {
printf("❌ No response body\n");
free(buffer);
release_pooled_connection(tcp_conn);
return EIO;
}

size_t body_size = total_size - body_start;
char *body_data = buffer + body_start;

// Check for JPEG EOI marker
const char *ext = strrchr(url, '.');
if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
if (body_size > 2) {
unsigned char *last_two = (unsigned char*)body_data + body_size - 2;
if (last_two[0] == 0xFF && last_two[1] == 0xD9) {
printf("✅ JPEG EOI marker verified\n");
} else {
printf("⚠️ JPEG missing EOI marker - file incomplete!\n");
free(buffer);
release_pooled_connection(tcp_conn);
return EIO;
}
}
}

// Reject incomplete download for non-chunked
if (!is_chunked && content_length > 0 && body_size < (size_t)content_length) {
printf("❌ Incomplete download (%zu/%d)\n", body_size, content_length);
free(buffer);
release_pooled_connection(tcp_conn);
return EIO;
}

// For chunked encoding, decode the body
if (is_chunked) {
size_t decoded_size = 0;
char *decoded = decode_chunked_data(body_data, body_size, &decoded_size);
if (decoded) {
free(buffer);
buffer = decoded;
body_size = decoded_size;
printf("✅ Decoded chunked data: %zu bytes\n", body_size);
} else {
printf("❌ Failed to decode chunked data\n");
free(buffer);
release_pooled_connection(tcp_conn);
return EIO;
}
} else {
// Just copy the body
char *new_buffer = malloc(body_size + 1);
if (!new_buffer) {
free(buffer);
release_pooled_connection(tcp_conn);
return ENOMEM;
}
memcpy(new_buffer, body_data, body_size);
new_buffer[body_size] = '\0';
free(buffer);
buffer = new_buffer;
}

*content = buffer;
*content_size = body_size;

printf("✅ Extracted body: %zu bytes\n", body_size);

// Release connection back to pool
release_pooled_connection(tcp_conn);

return EOK;
}

errno_t fetch_https_media(const char *url, char **content, size_t *content_size) {
    if (!url || !content || !content_size) return EINVAL;

    *content = NULL;
    *content_size = 0;

    char hostname[256];
    errno_t rc = extract_hostname(url, hostname, sizeof(hostname));
    if (rc != EOK) return rc;

    printf("📥 fetch_https_media: %s\n", url);

    // ===== CREATE FRESH CONNECTION =====
    inet_addr_t addr;
    rc = resolve_host(hostname, &addr);
    if (rc != EOK) return rc;

    tcp_t *tcp = NULL;
    tcp_conn_t *tcp_conn = NULL;
    rc = create_tcp_connection(addr, 443, &tcp, &tcp_conn);
    if (rc != EOK) return rc;

    void *tls_conn = NULL;
    rc = create_https_connection(hostname, tcp_conn, &tls_conn);
    if (rc != EOK) {
        tcp_conn_destroy(tcp_conn);
        return rc;
    }
    // ===================================

    // ===== SEND REQUEST =====
    const char *path = extract_path(url);
    if (!path || path[0] == '\0') path = "/";

    char request[2048];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: PaukBrowser/1.0\r\n"
        "Accept: image/webp,image/apng,image/*,*/*;q=0.8\r\n"
        "\r\n",
        path, hostname);

    rc = tls_send(tls_conn, request, strlen(request));
    if (rc != EOK) {
        tls_close(tls_conn);
        tcp_conn_destroy(tcp_conn);
        return rc;
    }
    // ========================

    // ===== RECEIVE RESPONSE (IMAGE ONLY) =====
    size_t total_size = 0;
    size_t buffer_size = 65536;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        tls_close(tls_conn);
        tcp_conn_destroy(tcp_conn);
        return ENOMEM;
    }

    char read_buffer[16384];
    size_t nread;
    int headers_processed = 0;
    size_t body_start = 0;
    int content_length = -1;
    int is_chunked = 0;
    uint64_t last_progress = get_uptime_ms();

    while (1) {
        rc = tls_receive(tls_conn, read_buffer, sizeof(read_buffer), &nread);

        if (rc == EOK) {
            if (nread > 0) {
                last_progress = get_uptime_ms();

                if (total_size + nread >= buffer_size) {
                    buffer_size *= 2;
                    char *new_buffer = realloc(buffer, buffer_size);
                    if (!new_buffer) {
                        free(buffer);
                        tls_close(tls_conn);
                        tcp_conn_destroy(tcp_conn);
                        return ENOMEM;
                    }
                    buffer = new_buffer;
                }

                memcpy(buffer + total_size, read_buffer, nread);
                total_size += nread;

                if (!headers_processed) {
                    int end = find_end_of_headers(buffer, total_size);
                    if (end != -1) {
                        headers_processed = 1;
                        body_start = end;

                        char *cl = extract_header_value(buffer, "Content-Length");
                        if (cl) {
                            content_length = atoi(cl);
                            free(cl);
                        }

                        char *te = extract_header_value(buffer, "Transfer-Encoding");
                        if (te) {
                            if (strstr(te, "chunked")) {
                                is_chunked = 1;
                            }
                            free(te);
                        }

                        // Check if it's an image
                        char *ct = extract_header_value(buffer, "Content-Type");
                        if (ct) {
                            if (strstr(ct, "image/")) {
                                printf("🖼️ Image detected: %s\n", ct);
                            }
                            free(ct);
                        }
                    }
                }

                // Check completion
                if (headers_processed) {
                    if (is_chunked) {
                        if (total_size >= body_start + 5) {
                            char *end_check = buffer + total_size - 5;
                            if (memcmp(end_check, "0\r\n\r\n", 5) == 0) {
                                break;
                            }
                        }
                    } else if (content_length > 0) {
                        size_t received = total_size - body_start;
                        if (received >= (size_t)content_length) {
                            break;
                        }
                    }
                }

            } else {
                // Server closed connection
                break;
            }

        } else if (rc == EAGAIN) {
            uint64_t now = get_uptime_ms();
            if ((now - last_progress) > 30000) {
                printf("❌ Download stalled\n");
                free(buffer);
                tls_close(tls_conn);
                tcp_conn_destroy(tcp_conn);
                return ETIMEOUT;
            }
            fibril_usleep(10000);
            continue;
        } else {
            printf("❌ Receive error: %s\n", str_error(rc));
            free(buffer);
            tls_close(tls_conn);
            tcp_conn_destroy(tcp_conn);
            return rc;
        }
    }

    if (!headers_processed || body_start >= total_size) {
        printf("❌ No response body\n");
        free(buffer);
        tls_close(tls_conn);
        tcp_conn_destroy(tcp_conn);
        return EIO;
    }

    size_t body_size = total_size - body_start;

    // ===== EXTRACT IMAGE DATA =====
    *content = malloc(body_size + 1);
    if (!*content) {
        free(buffer);
        tls_close(tls_conn);
        tcp_conn_destroy(tcp_conn);
        return ENOMEM;
    }

    memcpy(*content, buffer + body_start, body_size);
    (*content)[body_size] = '\0';
    *content_size = body_size;

    printf("✅ Downloaded image: %zu bytes\n", body_size);

    free(buffer);
    tls_close(tls_conn);
    tcp_conn_destroy(tcp_conn);

    return EOK;
}

errno_t fetch_image_on_connection(void *tls_conn, tcp_conn_t *tcp_conn, 
    const char *url, char **content, size_t *content_size) {
if (!url || !content || !content_size) return EINVAL;

*content = NULL;
*content_size = 0;

char hostname[256];
errno_t rc = extract_hostname(url, hostname, sizeof(hostname));
if (rc != EOK) return rc;

const char *path = extract_path(url);
if (!path || path[0] == '\0') path = "/";

// ===== SEND REQUEST ON EXISTING CONNECTION =====
char request[2048];
snprintf(request, sizeof(request),
"GET %s HTTP/1.1\r\n"
"Host: %s\r\n"
"Connection: keep-alive\r\n"
"User-Agent: PaukBrowser/1.0\r\n"
"Accept: image/webp,image/apng,image/*,*/*;q=0.8\r\n"
"\r\n",
path, hostname);

rc = tls_send(tls_conn, request, strlen(request));
if (rc != EOK) return rc;
// ================================================

// ===== RECEIVE RESPONSE =====
size_t total_size = 0;
size_t buffer_size = 65536;
char *buffer = malloc(buffer_size);
if (!buffer) return ENOMEM;

char read_buffer[16384];
size_t nread;
int headers_processed = 0;
size_t body_start = 0;
int content_length = -1;
int is_chunked = 0;
uint64_t last_progress = get_uptime_ms();

while (1) {
rc = tls_receive(tls_conn, read_buffer, sizeof(read_buffer), &nread);

if (rc == EOK) {
if (nread > 0) {
last_progress = get_uptime_ms();

if (total_size + nread >= buffer_size) {
buffer_size *= 2;
char *new_buffer = realloc(buffer, buffer_size);
if (!new_buffer) {
free(buffer);
return ENOMEM;
}
buffer = new_buffer;
}

memcpy(buffer + total_size, read_buffer, nread);
total_size += nread;

if (!headers_processed) {
int end = find_end_of_headers(buffer, total_size);
if (end != -1) {
headers_processed = 1;
body_start = end;

char *cl = extract_header_value(buffer, "Content-Length");
if (cl) {
content_length = atoi(cl);
free(cl);
}

char *te = extract_header_value(buffer, "Transfer-Encoding");
if (te) {
if (strstr(te, "chunked")) {
 is_chunked = 1;
}
free(te);
}
}
}

// Check completion
if (headers_processed) {
if (is_chunked) {
if (total_size >= body_start + 5) {
char *end_check = buffer + total_size - 5;
if (memcmp(end_check, "0\r\n\r\n", 5) == 0) {
 break;
}
}
} else if (content_length > 0) {
size_t received = total_size - body_start;
if (received >= (size_t)content_length) {
break;
}
}
}

} else {
// Server closed connection
break;
}

} else if (rc == EAGAIN) {
uint64_t now = get_uptime_ms();
if ((now - last_progress) > 30000) {
printf("❌ Download stalled\n");
free(buffer);
return ETIMEOUT;
}
fibril_usleep(10000);
continue;
} else {
printf("❌ Receive error: %s\n", str_error(rc));
free(buffer);
return rc;
}
}

if (!headers_processed || body_start >= total_size) {
free(buffer);
return EIO;
}

size_t body_size = total_size - body_start;

*content = malloc(body_size + 1);
if (!*content) {
free(buffer);
return ENOMEM;
}

memcpy(*content, buffer + body_start, body_size);
(*content)[body_size] = '\0';
*content_size = body_size;

free(buffer);
return EOK;
}
#endif  // This should be the last line - matching #ifdef HAVE_MBEDTLS

