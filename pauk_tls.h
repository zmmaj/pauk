#ifndef PAUK_TLS_H
#define PAUK_TLS_H

#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include "main.h"
#include "gui.h"
#include "network.h"

#ifdef HAVE_MBEDTLS
// mbed TLS 3.2.1 headers
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/debug.h>
#include <mbedtls/error.h>
#endif

typedef struct {
    tcp_conn_t *tcp_conn;
    int socket_fd;

#ifdef HAVE_MBEDTLS
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca_cert;
#endif

} pauk_tls_connection_t;

typedef enum {
    COMPRESSION_NONE = 0,
    COMPRESSION_GZIP = 1,
    COMPRESSION_DEFLATE = 2,
    COMPRESSION_ZLIB = 3,
    COMPRESSION_BROTLI = 4,
    COMPRESSION_ZSTD = 5
} compression_type_t;

#define MAX_POOL_CONNECTIONS 6  // Chrome uses 6 per host

typedef struct {
    char host[256];
    int port;
    tcp_conn_t *tcp_conn;
    void *tls_conn;
    int in_use;
    uint64_t last_used;
    int request_count;
} PooledConnection;

typedef struct {
    PooledConnection connections[MAX_POOL_CONNECTIONS];
    fibril_mutex_t mutex;
} ConnectionPool;

extern void *g_cached_tls;
extern tcp_conn_t *g_cached_conn;

typedef void* pauk_tls_handle_t;

// TLS functions
errno_t pauk_tls_library_init(void);
errno_t create_https_connection(const char *hostname, tcp_conn_t *tcp_conn, pauk_tls_handle_t *tls_conn);
errno_t tls_perform_handshake(pauk_tls_handle_t tls_conn);
errno_t tls_send(pauk_tls_handle_t tls_conn, const void *data, size_t len);
errno_t tls_receive(pauk_tls_handle_t tls_conn, void *buffer, size_t size, size_t *received);
void tls_close(pauk_tls_handle_t tls_conn);

errno_t fetch_https_content(const char *url, tcp_conn_t *tcp_conn, char **content, size_t *content_size, int redirect_count);
bool is_https_url(const char *url);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
void debug_handshake_details(pauk_tls_connection_t *conn, int ret);
int load_ca_cert_file(mbedtls_x509_crt *crt, const char *filename);
int load_ca_cert_der(mbedtls_x509_crt *crt, const char *filename); 
void srbinos_delay_1(unsigned int ms);
mbedtls_time_t mbedtls_platform_time(mbedtls_time_t *t);
size_t decode_chunked_body(char *buffer, size_t total_size, size_t body_start);


errno_t fetch_css_file(const char *url, char **content, size_t *size);
void parse_css_from_string(const char *css) ;
errno_t fetch_css_resource(const char *url, char **content, size_t *size);
void parse_css_and_add_rules(const char *css);

compression_type_t detect_compression(const unsigned char *data, size_t size, const char *content_encoding);

tcp_conn_t* get_main_conn(void);
void* get_main_tls(void);
void store_main_connection(void *tls, tcp_conn_t *conn);
errno_t fetch_https_content_keep_alive(const char *url, char **content, size_t *content_size);
errno_t fetch_http_content_keep_alive(const char *url, char **content, size_t *content_size) ;
void close_keepalive_connection(void);

void init_connection_pool(void);
errno_t get_pooled_connection(const char *hostname, int port, 
    tcp_conn_t **out_tcp, void **out_tls);
errno_t fetch_https_content_pooled(const char *url, char **content, size_t *content_size);
void release_pooled_connection(tcp_conn_t *tcp_conn);
void close_connection_pool(void);

errno_t fetch_https_media(const char *url, char **content, size_t *content_size);
errno_t fetch_image_on_connection(void *tls_conn, tcp_conn_t *tcp_conn, 
    const char *url, char **content, size_t *content_size);
    void process_single_selector_rule(const char *clean_selector, char *declarations);
    
#endif // PAUK_TLS_H
