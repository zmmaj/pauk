#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#include "gui.h"
#include <stdint.h>
#include <stdlib.h>


#define MAX_QUEUE 256

typedef struct {
    char *key;
    unsigned char *data;
    int width;
    int height;
    int channels;
} CachedImage;


// For storing raw network images
typedef struct {
    char *url;
    unsigned char *raw_data;
    size_t raw_size;
    uint64_t last_access; 
    int width;      // Will be filled when decoded
    int height;     // Will be filled when decoded
    int decoded;    // Whether we've decoded it
} RawImageEntry;

typedef struct {
    char *url;
    char *original_url;
    cJSON *element;
    int status;
    char *local_path;
} MediaDownloadTask;

extern MediaDownloadTask queue[MAX_QUEUE];
extern int queue_count;


void init_image_catalog(pauk_ui_t *pauk_ui);

void init_image_cache(void);
void free_image_cache(void);
unsigned char* get_cached_image(const char *src_path, int target_width, int target_height, 
                                 int *out_width, int *out_height);
void clear_image_cache(void);

void store_raw_image_data(const char *url, unsigned char *data, size_t size);
unsigned char* get_cached_image(const char *src_path, int target_width, int target_height,
    int *out_width, int *out_height);

void store_raw_image_data(const char *url, unsigned char *data, size_t size);
void free_raw_image_cache(void);

int is_webp(const unsigned char *data, size_t size);

void catalog_add_image(pauk_ui_t *pauk_ui, const char *url, cJSON *element);
void catalog_start_download(pauk_ui_t *pauk_ui);
void queue_media_download(const char *url, cJSON *element);
void process_download_queue(pauk_ui_t *pauk_ui);
int get_queue_count(void);
char* url_encode(const char *str);
void process_download_queue_parallel(pauk_ui_t *pauk_ui);
#endif
