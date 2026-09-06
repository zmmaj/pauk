#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <inet/tcp.h>
#include <stdbool.h>

typedef struct {
    char host[256];
    uint16_t port;
    tcp_conn_t *conn;
    void *tls_conn;
    bool in_use;
} PoolEntry;

// Initialize the pool (call once)
void pool_init(void);

// Get a connection for a host (creates if needed)
tcp_conn_t* pool_get_connection(const char *host, uint16_t port, void **tls_conn);

// Return connection to pool
void pool_return_connection(tcp_conn_t *conn);

// Clean up all connections
void pool_cleanup(void);

#endif
