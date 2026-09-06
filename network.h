#ifndef NETWORK_H
#define NETWORK_H

#include <inet/addr.h>
#include <inet/tcp.h>
#include <stddef.h>
#include <stdbool.h>



// Function to resolve hostname
errno_t resolve_host(const char *url, inet_addr_t *addr);

// Function to create TCP connection
errno_t create_tcp_connection(inet_addr_t addr, uint16_t port, tcp_t **tcp, tcp_conn_t **conn);

// Function to send HTTP request and receive response
errno_t fetch_http_content(const char *host, tcp_conn_t *conn, char **content, size_t *content_size , int redirect_count);

// Function to find the end of HTTP headers
int find_end_of_headers(const char *data, size_t length);

extern errno_t extract_hostname(const char *url, char *hostname, size_t hostname_size);
extern errno_t network_http_get(const char *url, char **content, size_t *content_size);
extern char *extract_header_value(const char *headers, const char *header_name);
extern int parse_http_status(const char *response);
extern char *extract_header_value(const char *headers, const char *header_name);
extern const char *extract_path(const char *url);
extern errno_t test_http_connectivity(void);
extern errno_t create_tcp_connection_async(inet_addr_t addr, tcp_t **tcp, tcp_conn_t **conn);
extern errno_t create_tcp_connection_simple(inet_addr_t addr, tcp_t **tcp, tcp_conn_t **conn);
extern errno_t fetch_http_content(const char *url, tcp_conn_t *conn, char **content, size_t *content_size, int redirect_count);

extern errno_t extract_hostname_and_port(const char *url, char *hostname, size_t hostname_size, uint16_t *port);
//extern char* resolve_url(const char* base_url, const char* relative_url);
extern char* resolve_relative_url(const char* base_url, const char* relative_url);


char* fetch_url_content(const char *url);
char* decode_chunked_data(const char *src, size_t src_len, size_t *out_len) ;

#endif // NETWORK_H
