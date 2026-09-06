// resource_filter.h
#ifndef RESOURCE_FILTER_H
#define RESOURCE_FILTER_H

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Blocked URL patterns (exact strings to search for)
typedef struct {
    const char *pattern;
    const char *reason;  // For debugging
} BlockedPattern;

// Check if a URL should be blocked
bool is_resource_blocked(const char *url);

// Get block reason (for logging)
const char* get_block_reason(const char *url);

// prikazi blokirano
void print_blocked_patterns(void) ;
#endif
