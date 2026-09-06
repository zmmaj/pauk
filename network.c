#include <ui/ui.h>
#include <ui/image.h>
#include <inet/addr.h>  
#include <fibril_synch.h>
#include <async.h>
#include <str.h>
#include <miniz.h>
#include "gui.h"
#include "network.h"
#include "image_cache.h"
#include "img_parser.h"
#include "pauk_tls.h"  // This should be your main TLS header

#define CONNECTION_TIMEOUT_MS 30000  // 10 second timeout
#define RECV_TIMEOUT_MS 30000  


#ifndef NO_HTTPS_SUPPORT

#else
// Simple stubs for when HTTPS is disabled - MUST MATCH pauk_tls.h signatures!

// Use the same types as defined in pauk_tls.h
static inline errno_t create_https_connection(const char* hostname, tcp_conn_t* tcp_conn, void** tls_conn) { 
    return ENOTSUP; 
}

static inline errno_t tls_perform_handshake(void* tls_conn) { 
    return ENOTSUP; 
}


errno_t tls_send(pauk_tls_handle_t tls_conn, const void *data, size_t len) {
    if (!tls_conn || !data || len == 0) return EINVAL;
    
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    
    // Use the TCP library's send function
    return tcp_conn_send(conn->tcp_conn, data, len);
}

errno_t tls_receive(pauk_tls_handle_t tls_conn, void *buffer, size_t size, size_t *received) {
    if (!tls_conn || !buffer || !received) return EINVAL;
    
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    
    // Use the TCP library's receive function
    // This properly yields and handles blocking
    errno_t rc = tcp_conn_recv_wait(conn->tcp_conn, buffer, size, received);
    
    if (rc == EOK) {
        return EOK;
    } else if (rc == EAGAIN) {
        return EAGAIN;
    } else {
        return EIO;
    }
}

void tls_close(void *tls_conn) {
    if (!tls_conn) return;
    
    pauk_tls_connection_t *conn = (pauk_tls_connection_t *)tls_conn;
    
    // 1. Close the TLS connection (send close_notify)
    if (conn->ssl.context) {  // Or however your mbedtls is structured
        mbedtls_ssl_close_notify(&conn->ssl);
        mbedtls_ssl_free(&conn->ssl);
        mbedtls_ssl_config_free(&conn->conf);
        mbedtls_ctr_drbg_free(&conn->ctr_drbg);
        mbedtls_entropy_free(&conn->entropy);
        mbedtls_x509_crt_free(&conn->ca_cert);
    }
    
    // 2. Close the TCP connection
    if (conn->tcp_conn) {
        tcp_conn_destroy(conn->tcp_conn);
        conn->tcp_conn = NULL;
    }
    
    // 3. Free the connection structure
    free(conn);
}

#endif
// Helper function to extract hostname from URL
 errno_t extract_hostname(const char *url, char *hostname, size_t hostname_size)
{
    const char *host_start = url;
    const char *host_end;
    
    // Skip protocol if present
    if (str_ncmp(url, "http://", 7) == 0) {
        host_start = url + 7;
    } else if (str_ncmp(url, "https://", 8) == 0) {
        host_start = url + 8;
    }
    
    // Find the end of the hostname (before port or path)
    host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/' && *host_end != '?') {
        host_end++;
    }
    
    // Calculate length and copy
    size_t host_len = host_end - host_start;
    if (host_len == 0) {
        return EINVAL; // Empty hostname
    }
    
    if (host_len >= hostname_size) {
        return EOVERFLOW;
    }
    
    // Copy the hostname
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = '\0'; // Null-terminate
    
    return EOK;
}

errno_t resolve_host(const char *url, inet_addr_t *addr)
{
    char hostname[256];
    errno_t rc;
    
    // Extract hostname from URL
    rc = extract_hostname(url, hostname, sizeof(hostname));
    if (rc != EOK) {
        return rc;
    }
    
 //   printf("Resolving hostname: %s\n", hostname);
    return inet_host_plookup_one(hostname, ip_v4, addr, NULL, 0);
}


// The rest of the functions remain the same...
errno_t create_tcp_connection(inet_addr_t addr, uint16_t port, tcp_t **tcp, tcp_conn_t **conn)
{
    errno_t rc;
    int retry_count = 0;
    
  //  printf("Creating TCP connection to ");
  //  uint8_t *bytes = (uint8_t *)&addr;
  //  printf("%d.%d.%d.%d:%d\n", bytes[0], bytes[1], bytes[2], bytes[3], port);
    
    while (retry_count < MAX_RETRIES) {
     //   printf("Attempt %d/%d...\n", retry_count + 1, MAX_RETRIES);
        
        rc = tcp_create(tcp);
        if (rc != EOK) {
            printf("tcp_create failed: %s\n", str_error(rc));
            return rc;
        }

        inet_ep2_t ep2;
        inet_ep2_init(&ep2);
        ep2.remote.addr = addr;
        ep2.remote.port = port;

        rc = tcp_conn_create(*tcp, &ep2, NULL, NULL, conn);
     //   printf("tcp_conn_create returned: %s\n", str_error(rc));
        
        if (rc == EBUSY) {
            printf("EBUSY, retrying...\n");
            tcp_destroy(*tcp);
            retry_count++;
            srbinos_delay_1(RETRY_DELAY_MS);
            continue;
        } else if (rc != EOK) {
            printf("Connection failed: %s\n", str_error(rc));
            tcp_destroy(*tcp);
            return rc;
        }
        break;
    }
    
    if (rc != EOK) {
        printf("All connection attempts failed\n");
        return rc;
    }
    
    printf("Waiting for connection to be established...\n");
    rc = tcp_conn_wait_connected(*conn);
 //   printf("Connection result: %s\n", str_error(rc));
    
    return rc;
}

errno_t fetch_http_content(const char *url, tcp_conn_t *tcp_conn, char **content, size_t *content_size, int redirect_count) {
    if (redirect_count > MAX_REDIRECTS) {
        printf("Too many redirects (%d)\n", redirect_count);
        return ELOOP;
    }

    // Note: We assume tcp_conn is already established and passed in.
    // If tcp_conn is NULL, you would need to create one here.
    if (!tcp_conn) {
        printf("❌ fetch_http_content: tcp_conn is NULL\n");
        return EINVAL;
    }

    int is_image = 0;
    char *content_type = NULL;
    errno_t rc;
    char hostname[256];

    rc = extract_hostname(url, hostname, sizeof(hostname));
    if (rc != EOK) return rc;

    const char *path = extract_path(url);
    if (!path || path[0] == '\0') {
        path = "/";
    }

    // Use a standard User-Agent
    const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    if (strstr(url, "://google.com") != NULL) {
        ua = "Lynx/2.8.9rel.1 libwww-FM/2.14 SSL-MM/1.4.1";
    }

    char request[2048];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.9,sr;q=0.5\r\n"
        "Accept-Encoding: identity\r\n"
        "Referer: https://google.com\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, hostname, ua);

    printf("📤 Sending HTTP request (%zu bytes):\n", strlen(request));
    printf("--- START REQUEST ---\n%s", request);
    printf("--- END REQUEST ---\n");

    // Send the request over the plain TCP connection
    rc = tcp_conn_send(tcp_conn, request, strlen(request));
    if (rc != EOK) {
        printf("HTTP send failed: %s\n", str_error(rc));
        return rc;
    }

    printf("Request sent, waiting for HTTP response...\n");

    size_t total_size = 0;
    size_t buffer_size = 65536;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
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
        // Receive data over the plain TCP connection
        rc = tcp_conn_recv_wait(tcp_conn, read_buffer, sizeof(read_buffer), &nread);

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

                        char *cl_header = extract_header_value(buffer, "Content-Length");
                        if (cl_header) {
                            body_bytes_expected = strtoul(cl_header, NULL, 10);
                            has_content_length = 1;
                            free(cl_header);
                        }

                        char *te_header = extract_header_value(buffer, "Transfer-Encoding");
                        if (te_header && strstr(te_header, "chunked")) {
                            is_chunked = 1;
                        }
                        if (te_header) free(te_header);

                        http_status = parse_http_status(buffer);
                        printf("HTTP Status: %d\n", http_status);

                        if (http_status == 0) {
                            redirect_location = extract_header_value(buffer, "Location");
                            if (redirect_location) {
                                printf("⚠️ Detected hidden redirect via Location header!\n");
                                http_status = 302;
                            } else {
                                printf("⚠️ Detected status 0, forcing fallback to 200 OK for stability!\n");
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

                // === SMART CHECKS FOR END OF TRANSMISSION ===
                if (headers_processed) {
                    if (has_content_length) {
                        size_t current_body_bytes = total_size - body_start;
                        if (current_body_bytes >= body_bytes_expected) {
                            printf("✅ Finished via Content-Length.\n");
                            break;
                        }
                    } else if (is_chunked && total_size >= 5) {
                        if (memcmp(buffer + total_size - 5, "0\r\n\r\n", 5) == 0) {
                            printf("✅ Finished Chunked stream (found final chunk 0\\r\\n\\r\\n).\n");
                            break;
                        } else if (memcmp(buffer + total_size - 3, "0\n\n", 3) == 0) {
                            printf("✅ Finished Chunked stream (found 0\\n\\n).\n");
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
    /* 2. FAZA: Obrada nakon petlje (Kada su svi podaci u baferu)               */
    /* ========================================================================= */

    if (final_rc != EOK) {
        if (buffer) free(buffer);
        return final_rc;
    }

    // ===== DEBUG: Check received content =====
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
        printf("⚠️ Server returned empty response (total_size=%zu, buffer=%p)\n",
               total_size, (void*)buffer);
        if (buffer) free(buffer);
        *content = NULL;
        *content_size = 0;
        return EIO;
    }

    // ===== CHECK FOR DIRECT HTML (NO HTTP HEADERS) =====
    if (!headers_processed && total_size > 0 &&
        (strncasecmp(buffer, "<!doctype", 9) == 0 ||
         strncasecmp(buffer, "<html", 5) == 0 ||
         strncasecmp(buffer, "<HTML", 5) == 0 ||
         strstr(buffer, "<head") != NULL)) {

        printf("📄 Direct HTML response\n");
        *content = malloc(total_size + 1);
        if (*content) {
            memcpy(*content, buffer, total_size);
            (*content)[total_size] = '\0';
            *content_size = total_size;
            free(buffer);
            return EOK;
        }
    }

    if (http_status == 0) {
        printf("⚠️ Status 0!\n");
        free(buffer);
        return EIO;
    }

    // === REDIRECTS ===
    if (http_status == 301 || http_status == 302 ||
        http_status == 307 || http_status == 308) {
        if (redirect_location) {
            printf("🔄 Redirect to: %s\n", redirect_location);
            free(buffer);
            tcp_conn_t local_tcp_conn;
            char *new_content = NULL;
            size_t new_content_size = 0;
            errno_t rc = fetch_http_content(redirect_location, &local_tcp_conn,
                                            &new_content, &new_content_size,
                                            redirect_count + 1);
            free(redirect_location);
            if (rc == EOK) {
                *content = new_content;
                *content_size = new_content_size;
            }
            return rc;
        }
    }

    // === CHUNKED DECODING ===
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

    // === DEKOMPRESIJA SADRZAJA (GZIP / ZLIB) ===
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

        if (is_gzip) {
            printf("🔄 GZIP detected, using tinfl_decompress...\n");
            // ... (logika za gzip heder i dekompresiju)
            // Kompletan kod za dekompresiju se nalazi u referentnom dokumentu
        }
        else if (is_zlib) {
            printf("🔄 ZLIB detected, using uncompress...\n");
            // ... (logika za zlib dekompresiju)
            // Kompletan kod za zlib se nalazi u referentnom dokumentu
        }

        if (decompressed_ok && decomp_result) {
            free(buffer);
            buffer = decomp_result;
            total_size = decomp_size;
            body_start = 0;
            printf("✅ HTTP Decompression successful: %zu bytes\n", total_size);
        }
    }

    // === IMAGE HANDLING & FINALIZACIJA ===
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
        return EOK;
    }

    // === SUCCESSFUL END FOR PLAIN HTML ===
    if (body_start > 0 && body_start < total_size) {
        size_t html_size = total_size - body_start;
        memmove(buffer, buffer + body_start, html_size);
        buffer[html_size] = '\0';
        *content = buffer;
        *content_size = html_size;
    } else {
        *content = buffer;
        *content_size = total_size;
    }

    return EOK;
}


errno_t network_http_get(const char *url, char **content, size_t *content_size)
{
    tcp_t *tcp = NULL;
    tcp_conn_t *conn = NULL;
    inet_addr_t addr;
    
    char hostname[256];
    uint16_t port;
    
    // USE THE NEW FUNCTION: Extract hostname AND port from URL
    errno_t rc = extract_hostname_and_port(url, hostname, sizeof(hostname), &port);
    if (rc != EOK) {
        printf("URL parsing failed: %s\n", str_error(rc));
        return rc;
    }
    
  //  printf("Connecting to: %s:%d\n", hostname, port);
    
    // USE THE EXISTING FUNCTION: Resolve hostname to IP address
    rc = resolve_host(url, &addr);  // This still works with full URL
    if (rc != EOK) {
        printf("Resolution failed: %s\n", str_error(rc));
        return rc;
    }
    
    uint8_t *addr_bytes = (uint8_t *)&addr;
    printf("Resolved to IP: %d.%d.%d.%d\n", 
           addr_bytes[0], addr_bytes[1], addr_bytes[2], addr_bytes[3]);

    // USE THE EXTRACTED PORT (not hardcoded 80/443)
    rc = create_tcp_connection(addr, port, &tcp, &conn);
    if (rc != EOK) {
        printf("create_tcp_connection failed: %s\n", str_error(rc));
        return rc;
    }

    bool is_https = (str_ncmp(url, "https://", 8) == 0);
   // printf("Fetching %s content...\n", is_https ? "HTTPS" : "HTTP");
    
    if (is_https) {
        rc = fetch_https_content(url, conn, content, content_size, 0);
    } else {
        rc = fetch_http_content(url, conn, content, content_size, 0);
    }
    
    if (rc != EOK) {
        printf("fetch_%s_content failed: %s\n", is_https ? "https" : "http", str_error(rc));
    } else {
      //  printf("Successfully fetched %zu bytes\n", *content_size);
    }

    if (conn) tcp_conn_destroy(conn);
    if (tcp) tcp_destroy(tcp);
    return rc;
}


// Extract path from URL (e.g., "http://example.com/path" -> "/path")
const char *extract_path(const char *url)
{
    // Find the protocol part
    const char *protocol_end = str_str(url, "://");
    if (protocol_end) {
        // Skip the protocol
        const char *host_start = protocol_end + 3;
        
        // Find the first slash after the host
        const char *path_start = str_chr(host_start, '/');
        if (path_start) {
            return path_start; // Return the path including the slash
        }
    }
    
    // If no path found, return root path
    return "/";
}

// Parse HTTP status code from response headers
int parse_http_status(const char *response) {
    if (!response) return 0;
    
    // 💡 PAMETAN FALLBACK: Ako u baferu nema HTTP/, ali vidimo početak HTML-a,
    // to znači da su zaglavlja skinuta ili pomerena. Vraćamo 200 OK da browser ne odustane.
    if (strncasecmp(response, "<!", 2) == 0 || 
        strncasecmp(response, "<html", 5) == 0 || 
        strstr(response, "<head") != NULL) {
        return 200;
    }
    
    // 💡 POPRAVKA: Tražimo "HTTP/" bilo gde unutar primljenih podataka,
    // umesto da forsiramo da mora biti strogo na nultom bajtu stringa.
    const char *http_line = strstr(response, "HTTP/");
    if (!http_line) {
        return 0; // Tek ovde odustajemo ako nema ni protokola ni HTML-a
    }
    
    // ===== FIND FIRST SPACE STARTING FROM "HTTP/" =====
    const char *space = strchr(http_line, ' ');
    if (!space) {
        // No space found - malformed response
        return 0;
    }
    // ============================
    
    // ===== SKIP SPACE AND READ STATUS CODE =====
    const char *status_start = space + 1;
    
    // Status code must be 3 digits
    if (!(*status_start >= '0' && *status_start <= '9') ||
        !(*(status_start + 1) >= '0' && *(status_start + 1) <= '9') ||
        !(*(status_start + 2) >= '0' && *(status_start + 2) <= '9')) {
        return 0;
    }
    // ===========================================
    
    // ===== CONVERT TO INTEGER =====
    int status = 0;
    for (int i = 0; i < 3; i++) {
        status = status * 10 + (status_start[i] - '0');
    }
    // ===============================
    
    return status;
}


// Extract value from HTTP header
char *extract_header_value(const char *headers, const char *header_name)
{
    // First, try to find the header at the beginning of headers (after status line)
    char search_str[256];
    
    // Try with \r\n prefix (for headers after first)
    snprintf(search_str, sizeof(search_str), "\r\n%s: ", header_name);
    const char *header_start = str_str(headers, search_str);
    
    if (!header_start) {
        // Try with \n prefix (some servers use \n only)
        snprintf(search_str, sizeof(search_str), "\n%s: ", header_name);
        header_start = str_str(headers, search_str);
    }
    
    if (!header_start) {
        // Try case-insensitive
        snprintf(search_str, sizeof(search_str), "\r\n%s: ", header_name);
        header_start = str_casestr(headers, search_str);
    }
    
    if (header_start) {
        header_start += str_length(search_str);
        const char *header_end = str_str(header_start, "\r\n");
        if (!header_end) {
            header_end = str_str(header_start, "\n");
        }
        if (header_end) {
            size_t value_len = header_end - header_start;
            char *value = malloc(value_len + 1);
            if (value) {
                memcpy(value, header_start, value_len);
                value[value_len] = '\0';
                return value;
            }
        }
    }
    
    // Also check for header that might be right after status line
    // Status line ends with \r\n, so first header starts without \r\n prefix
    const char *first_header = str_str(headers, "\r\n");
    if (first_header) {
        first_header += 2;  // Skip \r\n
        char first_search[256];
        snprintf(first_search, sizeof(first_search), "%s: ", header_name);
        if (str_ncmp(first_header, first_search, str_length(first_search)) == 0) {
            const char *value_start = first_header + str_length(first_search);
            const char *value_end = str_str(value_start, "\r\n");
            if (value_end) {
                size_t value_len = value_end - value_start;
                char *value = malloc(value_len + 1);
                if (value) {
                    memcpy(value, value_start, value_len);
                    value[value_len] = '\0';
                    return value;
                }
            }
        }
    }
    
    return NULL;
}


int find_end_of_headers(const char *data, size_t length) {
    if (!data || length < 4) return -1;
    
    // Prvo probaj standardni \r\n\r\n
    for (size_t i = 0; i < length - 3; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' && 
            data[i+2] == '\r' && data[i+3] == '\n') {
            return i + 4;
        }
    }
    
    // Ako nema \r\n\r\n, probaj \n\n (samo LF)
    for (size_t i = 0; i < length - 1; i++) {
        if (data[i] == '\n' && data[i+1] == '\n') {
            return i + 2;
        }
    }
    
    return -1;
}





errno_t test_http_connectivity()
{
    printf("Testing HTTP connectivity...\n");
    
    // Test with a known working site
    const char *test_url = "http://example.com";
    inet_addr_t addr;
    //bool is_https;
    
    printf("Resolving: %s\n", test_url);
    errno_t rc = resolve_host(test_url, &addr);
    if (rc != EOK) {
        printf("Resolution failed: %s\n", str_error(rc));
        return rc;
    }
    
    uint8_t *bytes = (uint8_t *)&addr;
    printf("Resolved to: %d.%d.%d.%d\n", bytes[0], bytes[1], bytes[2], bytes[3]);
    
    tcp_t *tcp = NULL;
    tcp_conn_t *conn = NULL;
    
    printf("Testing TCP connection...\n");
    rc = create_tcp_connection(addr, 80, &tcp, &conn);
    
    if (rc == EOK) {
        printf("HTTP connectivity test passed!\n");
        tcp_conn_destroy(conn);
        tcp_destroy(tcp);
        return EOK;
    } else {
        printf("HTTP connectivity test failed: %s\n", str_error(rc));
        if (conn) tcp_conn_destroy(conn);
        if (tcp) tcp_destroy(tcp);
        return rc;
    }
}


errno_t extract_hostname_and_port(const char *url, char *hostname, size_t hostname_size, uint16_t *port)
{
    const char *host_start = url;
    const char *host_end;
    
    // Default ports
    bool is_https = (str_ncmp(url, "https://", 8) == 0);
    *port = is_https ? 443 : 80;
    
    // Skip protocol
    if (str_ncmp(url, "http://", 7) == 0) {
        host_start = url + 7;
    } else if (is_https) {
        host_start = url + 8;
    }
    
    // Find hostname end (before port or path)
    host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/' && *host_end != '?') {
        host_end++;
    }
    
    // Extract hostname
    size_t host_len = host_end - host_start;
    if (host_len == 0 || host_len >= hostname_size) {
        return EINVAL;
    }
    
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = '\0';
    
    // Check for custom port
    if (*host_end == ':') {
        const char *port_start = host_end + 1;
        char *port_end;
        long port_num = strtol(port_start, &port_end, 10);
        
        if (port_num > 0 && port_num <= 65535) {
            *port = (uint16_t)port_num;
            printf("Using custom port: %d\n", *port);
        }
    }
    
    return EOK;
}

/*
char* resolve_url(const char* base_url, const char* relative_url) {
    if (!base_url || !relative_url) return NULL;
    
    // If relative_url is already absolute, return a copy
    if (str_ncmp(relative_url, "http://", 7) == 0 || 
        str_ncmp(relative_url, "https://", 8) == 0) {
        return str_dup(relative_url);
    }
    
    // Find the last slash in base_url
    const char* last_slash = str_rchr(base_url, '/');
    if (!last_slash) {
        // No path in base URL, just append
        char* result = malloc(str_length(base_url) + 1 + str_length(relative_url) + 1);
        if (result) {
            str_cpy(result, str_length(base_url) + 1, base_url);
            str_cat(result, "/");
            str_cat(result, relative_url);
        }
        return result;
    }
    
    // Replace everything after the last slash with relative_url
    size_t base_len = last_slash - base_url + 1; // Include the slash
    char* result = malloc(base_len + str_length(relative_url) + 1);
    if (result) {
        memcpy(result, base_url, base_len);
        result[base_len] = '\0';
        str_cat(result, relative_url);
    }
    return result;
}
*/


char* resolve_relative_url(const char* base_url, const char* relative_url) {
    if (!base_url || !relative_url) return NULL;
    
 //   printf("Resolving: base='%s', relative='%s'\n", base_url, relative_url);
    
    // ========== ADD FILE:// HANDLING ==========
if (str_ncmp(base_url, "file://", 7) == 0) {
    char *result = malloc(512);
    if (!result) return NULL;
    
    // Get directory from base_url
    char *last_slash = str_rchr(base_url, '/');
    if (last_slash) {
        size_t dir_len = last_slash - base_url + 1;
        str_ncpy(result, 512, base_url, dir_len);
        result[dir_len] = '\0';
        str_cat(result, relative_url);  // str_cat takes only 2 args
    } else {
        snprintf(result, 512, "file:///%s", relative_url);
    }
    
 //   printf("Resolved URL: %s\n", result);
    return result;
}
// ========== END FILE:// HANDLING ==========

    // If relative_url is already absolute (starts with http:// or https://)
    if (str_ncmp(relative_url, "http://", 7) == 0 || 
        str_ncmp(relative_url, "https://", 8) == 0) {
        return str_dup(relative_url);
    }
    
    // Extract protocol, host, and port from base_url
    char protocol[16] = {0};
    char host[256] = {0};
    char port_str[16] = {0};
    uint16_t port = 80; // default HTTP port
    
    const char* protocol_end = str_str(base_url, "://");
    if (protocol_end) {
        size_t protocol_len = protocol_end - base_url;
        if (protocol_len < sizeof(protocol)) {
            memcpy(protocol, base_url, protocol_len);
            protocol[protocol_len] = '\0';
        }
        protocol_end += 3; // Skip "://"
    } else {
        protocol_end = base_url;
    }
    
    // Extract host and port
    const char* host_start = protocol_end;
    const char* host_end = host_start;
    
    while (*host_end && *host_end != ':' && *host_end != '/' && *host_end != '?') {
        host_end++;
    }
    
    size_t host_len = host_end - host_start;
    if (host_len > 0 && host_len < sizeof(host)) {
        memcpy(host, host_start, host_len);
        host[host_len] = '\0';
    }
    
    // Extract port if present
    if (*host_end == ':') {
        const char* port_start = host_end + 1;
        const char* port_end = port_start;
        while (*port_end && *port_end >= '0' && *port_end <= '9') {
            port_end++;
        }
        size_t port_len = port_end - port_start;
        if (port_len > 0 && port_len < sizeof(port_str)) {
            memcpy(port_str, port_start, port_len);
            port_str[port_len] = '\0';
            port = atoi(port_str);
        }
    }
    
    // Extract path from base_url to find directory
    const char* path_start = host_end;
    while (*path_start && *path_start != '/') {
        path_start++;
    }
    
    char base_path[512] = {0};
    if (*path_start == '/') {
        // Find the last slash in the path
        const char* last_slash = str_rchr(path_start, '/');
        if (last_slash) {
            size_t path_len = last_slash - path_start + 1; // Include the slash
            if (path_len < sizeof(base_path)) {
                memcpy(base_path, path_start, path_len);
                base_path[path_len] = '\0';
            }
        } else {
            // No path, just root
            str_cpy(base_path, sizeof(base_path), "/");
        }
    } else {
        // No path in base URL
        str_cpy(base_path, sizeof(base_path), "/");
    }
    
    // Handle different relative URL types
    char resolved_url[1024] = {0};
    
    if (relative_url[0] == '/') {
        // Absolute path relative to root
        snprintf(resolved_url, sizeof(resolved_url), "%s://%s:%d%s", 
                 protocol, host, port, relative_url);
    } else {
        // Relative path
        snprintf(resolved_url, sizeof(resolved_url), "%s://%s:%d%s%s", 
                 protocol, host, port, base_path, relative_url);
    }
    
   // printf("Resolved URL: %s\n", resolved_url);
    return str_dup(resolved_url);
}

char* fetch_url_content(const char *url) {
    char *content = NULL;
    size_t size = 0;
    errno_t rc;
    
 //   printf("Fetching URL: %s\n", url);
    
    // Use your existing network_http_get which handles both HTTP and HTTPS
    rc = network_http_get(url, &content, &size);
    
    if (rc != EOK) {
        printf("Failed to fetch %s: %s\n", url, str_error(rc));
        return NULL;
    }
    
   // printf("Fetched %zu bytes from %s\n", size, url);
    return content;
}


// Decode chunked transfer encoding
char* decode_chunked_data(const char *data, size_t data_len, size_t *out_len) {
    // ===== PROVERA PARAMETARA =====
    if (!data || data_len == 0 || !out_len) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    // ===============================
    
    // Alociraj maksimalnu moguću veličinu (manje od data_len)
    char *decoded = malloc(data_len + 1);
    if (!decoded) {
        *out_len = 0;
        return NULL;
    }
    
    size_t dpos = 0;
    size_t pos = 0;
    
    while (pos < data_len) {
        // ===== PRESKOČI NE-HEX KARAKTERE =====
        while (pos < data_len && 
               !((data[pos] >= '0' && data[pos] <= '9') || 
                 (data[pos] >= 'a' && data[pos] <= 'f') || 
                 (data[pos] >= 'A' && data[pos] <= 'F'))) {
            pos++;
        }
        if (pos >= data_len) break;
        // =====================================
        
        // ===== PARSIRAJ VELIČINU ČANKA =====
        char *endptr;
        unsigned long chunk_size = strtoul(data + pos, &endptr, 16);
        
        // Proveri da li je parsiranje uspelo
        if (endptr == data + pos) {
            break; // Nema validnog broja
        }
        
        if (chunk_size == 0) {
            break; // Kraj chunked podataka
        }
        
        // Proveri da li chunk_size ima smisla
        if (chunk_size > data_len) {
            printf("⚠️ Chunk size %lu je veći od ukupnih podataka %zu\n", 
                   chunk_size, data_len);
            break;
        }
        // ===================================
        
        // ===== PRONAĐI POČETAK PODATAKA =====
        pos = endptr - data;
        
        // Preskoči \r\n posle veličine
        while (pos < data_len && (data[pos] == '\r' || data[pos] == '\n')) {
            pos++;
        }
        if (pos >= data_len) break;
        // ====================================
        
        // ===== KOPIRAJ ČANAK =====
        if (dpos + chunk_size > data_len) {
            printf("⚠️ Prekoračenje bafera: dpos=%zu, chunk_size=%lu, data_len=%zu\n",
                   dpos, chunk_size, data_len);
            break;
        }
        
        memcpy(decoded + dpos, data + pos, chunk_size);
        dpos += chunk_size;
        pos += chunk_size;
        // =========================
    }
    
    decoded[dpos] = '\0';
    *out_len = dpos;
    
    // ===== OPCIONALNO: SMANJI BAFFER =====
    if (dpos == 0) {
        free(decoded);
        return NULL;
    }
    // =====================================
    
    return decoded;
}

