
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <src/webp/decode.h>
#include "image_cache.h"
#include "image_downloader.h"
#include "main.h"
#include "gui.h"
#include "layout_engine.h"
#include "render_func.h"
#include "time_utils.h"
#include "pauk_tls.h"
#include "stb_image.h"

#define MAX_CACHE_SIZE 50
#define MAX_QUEUE 256

 MediaDownloadTask queue[MAX_QUEUE];
 int queue_count = 0;

 CachedImage *cache = NULL;
static int cache_count = 0;
static int cache_capacity = 0;
static RawImageEntry raw_cache[32];
static int raw_cache_count = 0;
//static int is_processing = 0;
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo) {
    struct my_error_mgr *myerr = (struct my_error_mgr*)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

static unsigned char* decode_jpeg_with_libjpeg(const unsigned char *jpeg_data, size_t jpeg_size,
                                                int *width, int *height) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    unsigned char *rgb_data = NULL;
    
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    
    *width = cinfo.image_width;
    *height = cinfo.image_height;
    
    jpeg_start_decompress(&cinfo);
    
    rgb_data = malloc((*width) * (*height) * 3);
    if (!rgb_data) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    
    unsigned long row_stride = (*width) * 3;
    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, 
                                                    JPOOL_IMAGE, row_stride, 1);
    
    unsigned char *row_ptr = rgb_data;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(row_ptr, buffer[0], row_stride);
        row_ptr += row_stride;
    }
    
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    
    return rgb_data;
}
void init_image_cache(void) {
    cache_capacity = MAX_CACHE_SIZE;
    cache = calloc(cache_capacity, sizeof(CachedImage));
    cache_count = 0;
    
  //  printf("🖼️ Image cache initialized (capacity: %d)\n", cache_capacity);
}


void init_image_catalog(pauk_ui_t *pauk_ui) {
    if (!pauk_ui) return;
    
    // Make sure you're zeroing the ENTIRE structure
    memset(&pauk_ui->image_catalog, 0, sizeof(pauk_ui->image_catalog));
    
    // Double-check count is 0
    pauk_ui->image_catalog.count = 0;
    pauk_ui->image_catalog.is_downloading = 0;
    pauk_ui->image_catalog.current_index = 0;
    
    //printf("📚 Image catalog initialized (count=%d)\n", pauk_ui->image_catalog.count);
}

void free_image_cache(void) {
    for (int i = 0; i < cache_count; i++) {
        if (cache[i].key) free(cache[i].key);
        if (cache[i].data) free(cache[i].data);
    }
    free(cache);
    cache = NULL;
    cache_count = 0;
    cache_capacity = 0;
}

static char* make_key(const char *src_path, int target_width, int target_height) {
    char *key = malloc(512);
    snprintf(key, 512, "%s_%dx%d", src_path, target_width, target_height);
    return key;
}


// Helper function to decode and scale from raw network data
static unsigned char* decode_and_scale_from_raw(RawImageEntry *raw, int target_width, int target_height,
    int *out_width, int *out_height) {
if (!raw || !raw->raw_data || !out_width || !out_height) {
return NULL;
}

// Check if already scaled to this size in regular cache
char *key = make_key(raw->url, target_width, target_height);
if (!key) {
return NULL;
}

for (int i = 0; i < cache_count; i++) {
if (strcmp(cache[i].key, key) == 0) {
free(key);
*out_width = cache[i].width;
*out_height = cache[i].height;
return cache[i].data;
}
}
free(key);

// Variables for cleanup
unsigned char *img_data = NULL;
unsigned char *scaled_data = NULL;
uint8_t *rgba = NULL;
int img_w = 0, img_h = 0, channels = 0;
bool is_webp = false;

// ========== WEBP DETECTION ==========
is_webp = (raw->raw_size >= 12 &&
raw->raw_data[0] == 'R' && raw->raw_data[1] == 'I' && 
raw->raw_data[2] == 'F' && raw->raw_data[3] == 'F' &&
raw->raw_data[8] == 'W' && raw->raw_data[9] == 'E' && 
raw->raw_data[10] == 'B' && raw->raw_data[11] == 'P');

// ========== DECODE IMAGE ==========
if (is_webp) {
printf("📷 Decoding WebP image: %s\n", raw->url);

rgba = WebPDecodeRGBA(raw->raw_data, raw->raw_size, &img_w, &img_h);
if (!rgba) {
printf("❌ WebP decode failed for: %s\n", raw->url);
goto cleanup;
}

// Convert RGBA to RGB (3 channels)
img_data = malloc(img_w * img_h * 3);
if (!img_data) {
printf("❌ Out of memory for WebP conversion\n");
goto cleanup;
}

for (int i = 0; i < img_w * img_h; i++) {
img_data[i * 3]     = rgba[i * 4];     // R
img_data[i * 3 + 1] = rgba[i * 4 + 1]; // G
img_data[i * 3 + 2] = rgba[i * 4 + 2]; // B
}
channels = 3;

} else {
// Use stb_image for other formats (PNG, JPEG, etc.)
img_data = stbi_load_from_memory(raw->raw_data, raw->raw_size,
&img_w, &img_h, &channels, 3);
if (!img_data) {
printf("📓 Failed to decode network image: %s - %s\n", 
raw->url, stbi_failure_reason());
goto cleanup;
}
}

if (img_w <= 0 || img_h <= 0 || channels <= 0) {
printf("❌ Invalid image dimensions: %dx%d, channels=%d\n", img_w, img_h, channels);
goto cleanup;
}

printf("📓 Decoded network image: %s (%dx%d, %d channels)\n", 
raw->url, img_w, img_h, channels);

// ========== SCALE IMAGE ==========
if (target_width <= 0 || target_height <= 0) {
printf("❌ Invalid target dimensions: %dx%d\n", target_width, target_height);
goto cleanup;
}

scaled_data = malloc(target_width * target_height * 3);
if (!scaled_data) {
printf("❌ Out of memory for scaled image (%dx%d)\n", target_width, target_height);
goto cleanup;
}

// Nearest neighbor scaling
for (int y = 0; y < target_height; y++) {
int src_y = (y * img_h) / target_height;  // Fixed: use img_h for Y scaling
if (src_y >= img_h) src_y = img_h - 1;

for (int x = 0; x < target_width; x++) {
int src_x = (x * img_w) / target_width;
if (src_x >= img_w) src_x = img_w - 1;

int src_idx = (src_y * img_w + src_x) * 3;
int dst_idx = (y * target_width + x) * 3;

scaled_data[dst_idx]     = img_data[src_idx];
scaled_data[dst_idx + 1] = img_data[src_idx + 1];
scaled_data[dst_idx + 2] = img_data[src_idx + 2];
}
}

// ========== CACHE SCALED VERSION ==========
char *new_key = make_key(raw->url, target_width, target_height);
if (!new_key) {
printf("❌ Failed to create cache key\n");
goto cleanup;
}

// Evict oldest if cache is full
if (cache_count >= cache_capacity) {
if (cache[0].key) {
free(cache[0].key);
cache[0].key = NULL;
}
if (cache[0].data) {
free(cache[0].data);
cache[0].data = NULL;
}

// Shift remaining entries
for (int i = 1; i < cache_count; i++) {
cache[i-1] = cache[i];
}
cache_count--;
}

// Add to cache
cache[cache_count].key = new_key;
cache[cache_count].data = scaled_data;
cache[cache_count].width = target_width;
cache[cache_count].height = target_height;
cache[cache_count].channels = 3;
cache_count++;

*out_width = target_width;
*out_height = target_height;

// ========== CLEANUP - Free decoded data ==========
if (is_webp) {
if (img_data) free(img_data);
if (rgba) free(rgba);
} else {
if (img_data) stbi_image_free(img_data);
}

return scaled_data;

cleanup:
// ========== ERROR CLEANUP PATH ==========
if (is_webp) {
if (img_data) free(img_data);
if (rgba) free(rgba);
} else {
if (img_data) stbi_image_free(img_data);
}
if (scaled_data) {
free(scaled_data);
scaled_data = NULL;
}

*out_width = 0;
*out_height = 0;
return NULL;
}

unsigned char* get_cached_image(const char *src_path, int target_width, int target_height,
    int *out_width, int *out_height) {
    
    if (!src_path || !out_width || !out_height) return NULL;

        // === GLOBALNI ŠTIT: Presrećemo ugrađene inline Base64/SVG slike ===
        if (strncmp(src_path, "data:", 5) == 0) {
            printf("⏭️ [Cache Pipeline] Bezbedno preskacem inline SVG/Base64 podatak.\n");
            return NULL; // Prekidamo odmah, sprečavamo prelivanje steka
        }

    // ========== STEP 1: Check if this is a network URL ==========
    int is_network_url = (strstr(src_path, "://") != NULL);
  //  printf("🔍 Looking for image in cache: '%s'\n", src_path);
    
    if (is_network_url) {
        for (int i = 0; i < raw_cache_count; i++) {
            if (strcmp(raw_cache[i].url, src_path) == 0) {
                return decode_and_scale_from_raw(&raw_cache[i], target_width, target_height, 
                                                  out_width, out_height);
            }
        }
   //     printf("📓 Network image not in raw cache: %s\n", src_path);
        return NULL;
    }

    // ========== STEP 2: Not network - handle as file path ==========
    
    // Detect file type by extension (case-insensitive)
    const char *ext = strrchr(src_path, '.');
    int is_png = (ext && strcasecmp(ext, ".png") == 0);
    int is_jpeg = (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0));
    int is_webp = (ext && strcasecmp(ext, ".webp") == 0);
    
    char actual_path[512];
    strcpy(actual_path, src_path);
    
    // ===== CONVERSION POLICY =====
    // Only convert NON-PNG, NON-JPEG, and NON-WEBP images to PNG
    // JPEGs: libjpeg-turbo
    // WebP: libwebp (you have this)
    // PNGs: stb_image
    // Others (GIF, BMP, PSD, TGA, HDR, PNM, ICO): convert to PNG then stb_image
    if (!is_png && !is_jpeg && !is_webp) {
     //   printf("🔄 Converting %s to PNG for reliable rendering\n", src_path);
        const char *png_path = get_or_create_png(src_path, target_width, target_height);
        if (png_path) {
         //   printf("✅ Using converted PNG: %s\n", png_path);
            strcpy(actual_path, png_path);
        } else {
            printf("⚠️ PNG conversion failed, trying original\n");
        }
    }
    // =============================
    
    char *key = make_key(actual_path, target_width, target_height);

    // Check scaled cache
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].key, key) == 0) {
            free(key);
            *out_width = cache[i].width;
            *out_height = cache[i].height;
            return cache[i].data;
        }
    }
    free(key);

    // ========== STEP 3: Load and decode the image ==========
    int img_width, img_height, channels;
    unsigned char *img_data = NULL;
    
    // Check file type again (actual_path may have changed if converted)
    ext = strrchr(actual_path, '.');
    int is_jpeg_file = (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0));
    int is_webp_file = (ext && strcasecmp(ext, ".webp") == 0);
    int is_png_file = (ext && strcasecmp(ext, ".png") == 0);
    
    // ===== JPEG: Use libjpeg-turbo =====
    if (is_jpeg_file) {
      //  printf("📓 Using libjpeg-turbo for JPEG: %s\n", actual_path);
        FILE *f = fopen(actual_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            unsigned char *jpeg_data = (unsigned char*)malloc(file_size);
            if (jpeg_data) {
                fread(jpeg_data, 1, file_size, f);
                fclose(f);
                
                // Decode with libjpeg-turbo
                img_data = decode_jpeg_with_libjpeg(jpeg_data, file_size, &img_width, &img_height);
                free(jpeg_data);
                
                if (img_data) {
                    channels = 3;
                 //   printf("✅ libjpeg-turbo decoded: %dx%d\n", img_width, img_height);
                } else {
                    printf("❌ libjpeg-turbo decode failed\n");
                }
            } else {
                fclose(f);
            }
        }
    }
    // =================================
    
    // ===== WebP: Use libwebp (you already have this) =====
    else if (is_webp_file) {
      //  printf("📓 Using libwebp for WebP: %s\n", actual_path);
        // Your existing WebP decoding code here
        // img_data = decode_webp(...);
        // For now, fallback to stb_image
        img_data = stbi_load(actual_path, &img_width, &img_height, &channels, 3);
        if (img_data) {
            printf("✅ stb_image loaded WebP: %dx%d\n", img_width, img_height);
        }
    }
    // =====================================================
    
    // ===== PNG: Use stb_image (or libpng if you have it) =====
    else if (is_png_file) {
        printf("📓 Loading PNG: %s\n", actual_path);
        img_data = stbi_load(actual_path, &img_width, &img_height, &channels, 3);
        if (img_data) {
            printf("✅ stb_image loaded PNG: %dx%d\n", img_width, img_height);
        }
    }
    // ========================================================
    
    // ===== ALL OTHER FORMATS: Use stb_image =====
    if (!img_data) {
     //   printf("📓 Using stb_image for: %s\n", actual_path);
        img_data = stbi_load(actual_path, &img_width, &img_height, &channels, 3);
        
        if (!img_data) {
            printf("❌ stb_image failed: %s\n", stbi_failure_reason());
            
            // If conversion failed, try original as fallback (for non-PNG)
            if (!is_png && strcmp(actual_path, src_path) != 0) {
                printf("🔄 Fallback: Trying original %s\n", src_path);
                img_data = stbi_load(src_path, &img_width, &img_height, &channels, 3);
                if (img_data) {
                    printf("✅ Original loaded successfully\n");
                }
            }
        } else {
            printf("✅ stb_image loaded: %dx%d\n", img_width, img_height);
        }
        
        if (!img_data) {
            return NULL;
        }
    }
    // ===============================================

    // ========== STEP 4: Scale the image to target size ==========
    unsigned char *scaled_data = (unsigned char*)malloc(target_width * target_height * 3);
    if (!scaled_data) {
        if (is_jpeg_file) free(img_data);
        else stbi_image_free(img_data);
        return NULL;
    }

    // Nearest neighbor scaling
    for (int y = 0; y < target_height; y++) {
        for (int x = 0; x < target_width; x++) {
            int src_x = (x * img_width) / target_width;
            int src_y = (y * img_height) / target_height;
            if (src_x >= img_width) src_x = img_width - 1;
            if (src_y >= img_height) src_y = img_height - 1;

            int src_idx = (src_y * img_width + src_x) * 3;
            int dst_idx = (y * target_width + x) * 3;

            scaled_data[dst_idx] = img_data[src_idx];
            scaled_data[dst_idx + 1] = img_data[src_idx + 1];
            scaled_data[dst_idx + 2] = img_data[src_idx + 2];
        }
    }

    // Free original image data
    if (is_jpeg_file) free(img_data);
    else stbi_image_free(img_data);

    // ========== STEP 5: Cache the scaled image ==========
    char *new_key = make_key(actual_path, target_width, target_height);
    if (cache_count >= cache_capacity) {
        free(cache[0].key);
        free(cache[0].data);
        for (int i = 1; i < cache_count; i++) {
            cache[i-1] = cache[i];
        }
        cache_count--;
    }

    cache[cache_count].key = new_key;
    cache[cache_count].data = scaled_data;
    cache[cache_count].width = target_width;
    cache[cache_count].height = target_height;
    cache[cache_count].channels = 3;
    cache_count++;

    *out_width = target_width;
    *out_height = target_height;
    return scaled_data;
}

void clear_image_cache(void) {
    for (int i = 0; i < cache_count; i++) {
        if (cache[i].key) free(cache[i].key);
        if (cache[i].data) free(cache[i].data);
    }
    cache_count = 0;
    memset(cache, 0, cache_capacity * sizeof(CachedImage));
}


void store_raw_image_data(const char *url, unsigned char *data, size_t size) {
    if (!url || !data) return;
    printf("💾 Storing RAW image data with key: '%s'\n", url);  
    // Check if already in cache
    for (int i = 0; i < raw_cache_count; i++) {
        if (strcmp(raw_cache[i].url, url) == 0) {
            // Already exists - replace data
            free(raw_cache[i].raw_data);
            raw_cache[i].raw_data = data;
            raw_cache[i].raw_size = size;
            printf("📦 Updated raw image: %s (%zu bytes)\n", url, size);
            return;
        }
    }
    
    // Add new entry
    if (raw_cache_count >= 32) {
        // Remove oldest (FIFO)
        free(raw_cache[0].url);
        free(raw_cache[0].raw_data);
        for (int i = 1; i < raw_cache_count; i++) {
            raw_cache[i-1] = raw_cache[i];
        }
        raw_cache_count--;
    }
    
    raw_cache[raw_cache_count].url = strdup(url);
    raw_cache[raw_cache_count].raw_data = data;
    raw_cache[raw_cache_count].raw_size = size;
    raw_cache_count++;
    
    printf("📦 Stored raw image: %s (%zu bytes)\n", url, size);
}


void free_raw_image_cache(void) {
    for (int i = 0; i < raw_cache_count; i++) {
        if (raw_cache[i].url) free(raw_cache[i].url);
        if (raw_cache[i].raw_data) free(raw_cache[i].raw_data);
    }
    raw_cache_count = 0;
}

int is_webp(const unsigned char *data, size_t size) {
    if (size < 12) return 0;
    // RIFF chunk header + WEBP signature
    return (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P');
}


// Add image to catalog
void catalog_add_image(pauk_ui_t *pauk_ui, const char *url, cJSON *element) {
    if (!pauk_ui || !url || !element) return;
    
    // Safety check - if count is crazy, reset it
    if (pauk_ui->image_catalog.count < 0 || pauk_ui->image_catalog.count > 1000) {
        printf("⚠️ Catalog count corrupted (%d), resetting to 0\n", pauk_ui->image_catalog.count);
        pauk_ui->image_catalog.count = 0;
    }
    
    if (pauk_ui->image_catalog.count >= 256) {
        printf("⚠️ Image catalog full, cannot add: %s\n", url);
        return;
    }
    
    ImageTask *task = &pauk_ui->image_catalog.tasks[pauk_ui->image_catalog.count];
    task->original_url = strdup(url);
    task->element = element;
    task->status = 0;
    task->catalog_index = pauk_ui->image_catalog.count;
    
    // Store catalog index in JSON for reference
    set_json_number(element, "catalog_index", pauk_ui->image_catalog.count);
    
    pauk_ui->image_catalog.count++;
    printf("📚 Added to catalog [%d]: %s\n", task->catalog_index, url);
}

// Start downloading all images in catalog
void catalog_start_download(pauk_ui_t *pauk_ui) {
    if (!pauk_ui) return;
    if (pauk_ui->image_catalog.count == 0) return;
    if (pauk_ui->image_catalog.is_downloading) return;
    
    pauk_ui->image_catalog.is_downloading = 1;
  //  printf("📚 Starting download of %d images\n", pauk_ui->image_catalog.count);
    
    for (int i = 0; i < pauk_ui->image_catalog.count; i++) {
        ImageTask *task = &pauk_ui->image_catalog.tasks[i];
        
        printf("📥 Downloading [%d/%d]: %s\n", i+1, pauk_ui->image_catalog.count, task->original_url);
        
        // Fix: Pass both encoded and original URLs
        // For now, just pass the same URL twice (you may need to encode one)
        char *local_path = download_image_to_cache(task->original_url, task->original_url);
        
        if (local_path) {
            set_json_string(task->element, "src", local_path);
            set_json_bool(task->element, "needs_download", 0);
            task->status = 1;
            free(local_path);
          //  printf("✅ Saved\n");
        } else {
            task->status = 2;
            printf("❌ Failed\n");
        }
        
        fibril_usleep(500000);
    }
    
    pauk_ui->image_catalog.is_downloading = 0;
 //  printf("✅ All images downloaded!\n");
    refresh_page_after_js(pauk_ui);
}


void queue_media_download(const char *url, cJSON *element) {
    if (!url || !element) return;
    if (queue_count >= MAX_QUEUE) return;
    
    // Store original URL
    queue[queue_count].original_url = strdup(url);
    
    // Create encoded URL for download
    char *encoded_url = url_encode(url);
    if (!encoded_url) {
        free(queue[queue_count].original_url);
        return;
    }
    
    queue[queue_count].url = encoded_url;
    queue[queue_count].element = element;
    queue[queue_count].status = 0;
    queue[queue_count].local_path = NULL;
    queue_count++;
    
    //printf("📥 Queued: %s -> %s\n", url, encoded_url);
}

/*
void process_download_queue(pauk_ui_t *pauk_ui) {
    if (queue_count == 0) return;
    
    printf("📥 Downloading %d images...\n", queue_count);
    uint64_t start_time = get_uptime_ms();
    
    int success_count = 0;
    int fail_count = 0;
    
    // First pass - download all images
    for (int i = 0; i < queue_count; i++) {
        if (queue[i].status == 0) {
            queue[i].status = 1;
            
            char *local_path = download_image_to_cache(queue[i].url, queue[i].original_url);
            
            if (local_path) {
                set_json_string(queue[i].element, "src", local_path);
                set_json_bool(queue[i].element, "needs_download", 0);
                set_json_bool(queue[i].element, "needs_placeholder", 0);
                set_json_bool(queue[i].element, "download_queued", 0);
                queue[i].status = 2;
                success_count++;
                free(local_path);
                
                // Refresh every 3 images
                if (pauk_ui && success_count % 3 == 0) {
                    refresh_page_after_js(pauk_ui);
                    fibril_usleep(50000);
                }
            } else {
                printf("❌ Failed: %s\n", queue[i].url);
                queue[i].status = 3;
                fail_count++;
            }
            
            if (i < queue_count - 1) {
                fibril_usleep(50000);
            }
        }
    }
    
    // Second pass - retry failed images
    if (fail_count > 0) {
        printf("🔄 Retrying %d failed images...\n", fail_count);
        
        // Force new connection for retries
        close_keepalive_connection();
        fibril_usleep(500 * 1000);
        
        for (int i = 0; i < queue_count; i++) {
            if (queue[i].status == 3) {  // Failed
                printf("🔄 Retry: %s\n", queue[i].original_url);
                
                char *local_path = download_image_to_cache(queue[i].url, queue[i].original_url);
                
                if (local_path) {
                    set_json_string(queue[i].element, "src", local_path);
                    set_json_bool(queue[i].element, "needs_download", 0);
                    set_json_bool(queue[i].element, "needs_placeholder", 0);
                    set_json_bool(queue[i].element, "download_queued", 0);
                    queue[i].status = 2;
                    success_count++;
                    fail_count--;
                    free(local_path);
                    
                    // Refresh immediately on retry success
                    if (pauk_ui) {
                        refresh_page_after_js(pauk_ui);
                        fibril_usleep(50000);
                    }
                }
                
                fibril_usleep(100 * 1000); // 100ms between retries
            }
        }
    }
    
    uint64_t elapsed = get_uptime_ms() - start_time;
    printf("📥 Downloads complete: %d success, %d failed in %lu ms\n", 
            success_count, fail_count, (unsigned long)elapsed);
    
    // Final refresh
    if (pauk_ui) {
        printf("🖼️ Final refresh\n");
        refresh_page_after_js(pauk_ui);
    }
    
    // Clear queue
    for (int i = 0; i < queue_count; i++) {
        free(queue[i].url);
        free(queue[i].original_url);
    }
    queue_count = 0;
}

*/


int get_queue_count(void) {
    return queue_count;
}



// URL encode function
char* url_encode(const char *str) {
    if (!str) return NULL;
    
    const char *hex = "0123456789ABCDEF";
    size_t len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;
    
    char *p = encoded;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (c == ' ') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '0';
        } else if (c == '!') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '1';
        } else if (c == '#') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '3';
        } else if (c == '$') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '4';
        } else if (c == '%') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '5';
        } else if (c == '&') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '6';
        } else if (c == '(') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '8';
        } else if (c == ')') {
            *p++ = '%';
            *p++ = '2';
            *p++ = '9';
        } else if (c == '+') {
            *p++ = '%';
            *p++ = '2';
            *p++ = 'B';
        } else if (c == ',') {
            *p++ = '%';
            *p++ = '2';
            *p++ = 'C';
        } else if (c == '/') {
            *p++ = '/';
        } else if (c == ':') {
            *p++ = ':';
        } else if (c == ';') {
            *p++ = ';';
        } else if (c == '=') {
            *p++ = '=';
        } else if (c == '?') {
            *p++ = '?';
        } else if (c == '@') {
            *p++ = '@';
        } else if (c == '[') {
            *p++ = '[';
        } else if (c == ']') {
            *p++ = ']';
        } else if ((c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~') {
            *p++ = c;
        } else {
            *p++ = '%';
            *p++ = hex[c >> 4];
            *p++ = hex[c & 15];
        }
    }
    *p = '\0';
    
    return encoded;
}

/*
void process_download_queue_parallel(pauk_ui_t *pauk_ui) {
    if (queue_count == 0) return;
    
    printf("📥 Processing %d images in parallel (max: %d)\n", queue_count, MAX_PARALLEL);
    
    // Build ImageList from your existing queue
    ImageList images;
    images.count = queue_count;
    images.urls = malloc(queue_count * sizeof(char*));
    images.elements = malloc(queue_count * sizeof(cJSON*));
    
    for (int i = 0; i < queue_count; i++) {
        images.urls[i] = strdup(queue[i].original_url);
        images.elements[i] = queue[i].element;
    }
    
    // Call the parallel download
    download_all_images_parallel(&images);
    
    // Clean up
    for (int i = 0; i < queue_count; i++) {
        free(images.urls[i]);
    }
    free(images.urls);
    free(images.elements);
    
    // Clear queue
    for (int i = 0; i < queue_count; i++) {
        free(queue[i].url);
        free(queue[i].original_url);
    }
    queue_count = 0;
    
    if (pauk_ui) {
        refresh_page_after_js(pauk_ui);
    }
}
    */
