// image_downloader.h 

#ifndef IMAGE_DOWNLOADER_H
#define IMAGE_DOWNLOADER_H

#include <stddef.h>
#include <errno.h>
#include <task.h>
#include <async.h>
#include <fibril.h>
#include "cjson.h"
#include "image_cache.h"

typedef struct pauk_ui pauk_ui_t;

#define MAX_PARALLEL 1
#define MAX_DOWNLOAD_TASKS 50

typedef struct {
    char **urls;
    cJSON **elements;  // DOM elements for each image
    int count;
} ImageList;

typedef struct {
    char *url;
    cJSON *element;
    int status;          // 0=pending, 1=downloading, 2=complete, 3=failed
    char *local_path;
    int retry_count;
    task_id_t task_id;
    int completed;
    int success;
    char output_path[256];
} DownloadTask;

// Core functions
void init_image_downloader(void);
void cleanup_image_downloader(void);
char* download_image_to_cache(const char *encoded_url, const char *original_url);
char* save_raw_image_to_cache(const char *original_url, unsigned char *image_data, size_t image_size);

// Queue management
void init_download_queue(void);
void add_images_to_queue(ImageList *images);
void start_download_from_queue(void);
void process_download_queue(pauk_ui_t *pauk_ui);
void cleanup_download_queue(void);

#endif
