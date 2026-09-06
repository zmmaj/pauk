#include "connection_pool.h"
#include "network.h"
#include "pauk_tls.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_POOL 4

static PoolEntry pool[MAX_POOL];
static int pool_initialized = 0;

void pool_init(void) {
    if (pool_initialized) return;
    memset(pool, 0, sizeof(pool));
    pool_initialized = 1;
    printf("🔌 Connection pool initialized\n");
}

static int find_or_create_entry(const char *host, uint16_t port) {
    // First, try to find an existing connection to the same host
    for (int i = 0; i < MAX_POOL; i++) {
        if (pool[i].conn && !pool[i].in_use && 
            strcmp(pool[i].host, host) == 0 && pool[i].port == port) {
            return i;
        }
    }
    
    // Find an empty slot
    for (int i = 0; i < MAX_POOL; i++) {
        if (!pool[i].conn) {
            return i;
        }
    }
    
    // Replace oldest (simplest: replace index 0)
    if (pool[0].conn) {
        if (pool[0].tls_conn) {
            tls_close(pool[0].tls_conn);
        }
        tcp_conn_destroy(pool[0].conn);
        memset(&pool[0], 0, sizeof(PoolEntry));
    }
    return 0;
}

tcp_conn_t* pool_get_connection(const char *host, uint16_t port, void **tls_conn) {
    if (!pool_initialized) pool_init();
    printf("🔍 pool_get_connection: host=%s, port=%d\n", host, port);
    printf("🔌 Getting connection to %s:%d\n", host, port);
    
    int idx = find_or_create_entry(host, port);
    
    if (pool[idx].conn && !pool[idx].in_use && 
        strcmp(pool[idx].host, host) == 0) {
        // Reuse existing connection
        pool[idx].in_use = true;
        printf("🔌 Reusing connection to %s:%d\n", host, port);
        if (tls_conn) *tls_conn = pool[idx].tls_conn;
        return pool[idx].conn;
    }
    
    // Create new connection
    printf("🔌 Creating new connection to %s:%d\n", host, port);
    
    inet_addr_t addr;
    errno_t rc = inet_host_plookup_one(host, ip_v4, &addr, NULL, 0);
    if (rc != EOK) {
        printf("❌ Failed to resolve %s\n", host);
        return NULL;
    }
    
    tcp_t *tcp = NULL;
    tcp_conn_t *conn = NULL;
    rc = create_tcp_connection(addr, port, &tcp, &conn);
    if (rc != EOK) {
        printf("❌ Failed to create TCP connection\n");
        return NULL;
    }
    
    void *tls = NULL;
    if (port == 443) {
        rc = create_https_connection(host, conn, &tls);
        if (rc != EOK) {
            tcp_conn_destroy(conn);
            tcp_destroy(tcp);
            printf("❌ Failed to create TLS connection\n");
            return NULL;
        }
    }
    
    // Store in pool
    strncpy(pool[idx].host, host, sizeof(pool[idx].host) - 1);
    pool[idx].port = port;
    pool[idx].conn = conn;
    pool[idx].tls_conn = tls;
    pool[idx].in_use = true;
    
    if (tls_conn) *tls_conn = tls;
    return conn;
}

void pool_return_connection(tcp_conn_t *conn) {
    for (int i = 0; i < MAX_POOL; i++) {
        if (pool[i].conn == conn) {
            pool[i].in_use = false;
            printf("🔌 Returned connection to pool (%s:%d)\n", pool[i].host, pool[i].port);
            return;
        }
    }
}

void pool_cleanup(void) {
    printf("🔌 Cleaning up connection pool\n");
    for (int i = 0; i < MAX_POOL; i++) {
        if (pool[i].conn) {
            if (pool[i].tls_conn) tls_close(pool[i].tls_conn);
            tcp_conn_destroy(pool[i].conn);
            memset(&pool[i], 0, sizeof(PoolEntry));
        }
    }
}
