// image_downloader.c - CLEANED VERSION

#include "image_downloader.h"
#include "network.h"
#include "pauk_tls.h"
#include "time_utils.h"
#include "image_cache.h"
#include "download_protocol.h"
#include "cjson.h"
#include "layout_engine.h"
#include <https_client.h>
#include <libhttps_download.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <jpeglib.h>
#include <str_error.h>
#include <fcntl.h>
#include <unistd.h>

#include "stb_image.h"
#include "stb_image_write.h"

static DownloadTask g_ipc_tasks[MAX_DOWNLOAD_TASKS];
static int g_ipc_task_count = 0;
static int g_active_downloads = 0;
static fibril_mutex_t g_ipc_mutex;

// Store both URL and local path
static struct {
    char *url;
    char *local_path;
} g_completed_images[512];

static int g_completed_count = 0;
static fibril_mutex_t g_completed_mutex;

extern cJSON *global_computed_layout;
extern int queue_count;

// Forward declarations
static int is_image_already_downloaded(const char *original_url);
static void mark_image_downloaded(const char *original_url, const char *local_path);
static const char* get_local_path_for_url(const char *original_url);
static const char* get_image_format_from_data(const unsigned char *data, size_t size);
static void update_element_url_recursive(cJSON *element, const char *original_url, const char *local_path);
static void update_all_elements_by_url(const char *original_url, const char *local_path);
unsigned char* convert_svg_to_rgb(const char *svg_text, int w, int h);
// ============================================================================
// INITIALIZATION
// ============================================================================

void init_image_downloader(void) {
    fibril_mutex_initialize(&g_completed_mutex);
    fibril_mutex_initialize(&g_ipc_mutex);
    g_completed_count = 0;
    g_ipc_task_count = 0;
    g_active_downloads = 0;
    memset(g_completed_images, 0, sizeof(g_completed_images));
    memset(g_ipc_tasks, 0, sizeof(g_ipc_tasks));
   // printf("📥 Image downloader initialized\n");
}

void cleanup_image_downloader(void) {
    fibril_mutex_lock(&g_completed_mutex);
    for (int i = 0; i < g_completed_count; i++) {
        if (g_completed_images[i].url) {
            free(g_completed_images[i].url);
        }
        if (g_completed_images[i].local_path) {
            free(g_completed_images[i].local_path);
        }
    }
    g_completed_count = 0;
    fibril_mutex_unlock(&g_completed_mutex);
  //  printf("📥 Image downloader cleaned up\n");
}


void init_download_queue(void) {
    fibril_mutex_lock(&g_ipc_mutex);
    g_ipc_task_count = 0;
    g_active_downloads = 0;
    memset(g_ipc_tasks, 0, sizeof(g_ipc_tasks));
    fibril_mutex_unlock(&g_ipc_mutex);
   // printf("📥 Download queue initialized (max parallel: %d)\n", MAX_PARALLEL);
}

void add_images_to_queue(ImageList *images) {
    if (!images || images->count == 0) {
     //   printf("📥 No images to add to queue\n");
        return;
    }
    
  //  printf("📥 Adding %d images to queue\n", images->count);
    
    fibril_mutex_lock(&g_ipc_mutex);
    
    int added = 0;
    int available_slots = MAX_DOWNLOAD_TASKS - g_ipc_task_count;
    
    for (int i = 0; i < images->count && i < available_slots; i++) {
        if (!images->urls[i] || images->urls[i][0] == '\0') {
            continue;
        }
        
        g_ipc_tasks[g_ipc_task_count].url = strdup(images->urls[i]);
        g_ipc_tasks[g_ipc_task_count].element = images->elements[i];
        g_ipc_tasks[g_ipc_task_count].status = 0;  // Pending
        g_ipc_tasks[g_ipc_task_count].completed = 0;
        g_ipc_tasks[g_ipc_task_count].success = 0;
        g_ipc_tasks[g_ipc_task_count].task_id = 0;
        g_ipc_tasks[g_ipc_task_count].local_path = NULL;
        g_ipc_tasks[g_ipc_task_count].retry_count = 0;
        g_ipc_task_count++;
        added++;
    }
    
    fibril_mutex_unlock(&g_ipc_mutex);
  //  printf("📥 Added %d images to queue (total: %d)\n", added, g_ipc_task_count);
}


static errno_t download_worker_fibril(void *arg) {
    int slot = (int)(uintptr_t)arg;
    
    fibril_mutex_lock(&g_ipc_mutex);
    DownloadTask *task = &g_ipc_tasks[slot];
    char *url = strdup(task->url);
    char *output_path = strdup(task->output_path); 
    fibril_mutex_unlock(&g_ipc_mutex);
    
    char *content = NULL;
    size_t content_size = 0;
    
    // Fetch using our dual-protocol core download function
    errno_t rc = https_get_with_progress(url, &content, &content_size, NULL, NULL);
    
    fibril_mutex_lock(&g_ipc_mutex);
    task = &g_ipc_tasks[slot];  // Refresh tracking nodes inside thread
    
    if (rc == EOK && content && content_size > 0) {
        
        // SAFE DETECTION: Check URL pattern OR check if raw payload is explicitly XML/SVG
        int is_vector = (strstr(url, ".svg") != NULL) || 
                        (content_size > 4 && strncmp(content, "<svg", 4) == 0) ||
                        (content_size > 60 && strstr(content, "<svg") != NULL);

        if (is_vector) {
            printf("🎨 [Fibril] Safe Vector track detected. Executing NanoSVG conversions...\n");
            
            int target_w = 400;
            int target_h = 360;
            
            // STACK-SAFE FILENAME REARRANGEMENT: Never use strcpy on ext_ptr!
            char safe_png_path[512];
            char *ext_dot = strrchr(output_path, '.');
            if (ext_dot) *ext_dot = '\0'; // Chop off whatever extension was there safely
            
            snprintf(safe_png_path, sizeof(safe_png_path), "%s.png", output_path);
            
            // Force strict null termination for the NanoSVG parser string input
            char *svg_text = malloc(content_size + 1);
            if (svg_text) {
                memcpy(svg_text, content, content_size);
                svg_text[content_size] = '\0';
                
                unsigned char *rgb_pixels = convert_svg_to_rgb(svg_text, target_w, target_h);
                free(svg_text);
                
                if (rgb_pixels) {
                    // Write directly as a raw pixel PNG stream to the target directory
                    int write_ok = stbi_write_png(safe_png_path, target_w, target_h, 3, rgb_pixels, target_w * 3);
                    free(rgb_pixels);
                    
                    if (write_ok) {
                        task->status = 2;
                        task->completed = 1;
                        task->success = 1;
                        task->local_path = strdup(safe_png_path);
                        
                        if (task->element) {
                            set_json_string(task->element, "src", safe_png_path);
                            set_json_bool(task->element, "needs_download", 0);
                        }
                        printf("✅ [Fibril Engine] SVG successfully rendered and saved to /tmp: %s\n", safe_png_path);
                    } else {
                        printf("❌ [Fibril Engine] Failed to compile PNG image map onto local disk\n");
                        rc = EIO;
                    }
                } else {
                    printf("❌ [Fibril Engine] NanoSVG vector evaluation module failed to rasterize paths\n");
                    rc = EIO;
                }
            } else {
                rc = ENOMEM;
            }
        } 
        // === STANDARD FALLBACK FOR NATIVE IMAGES (JPG, PNG) ===
        else {
            FILE *f = fopen(output_path, "wb");
            if (f) {
                size_t written = fwrite(content, 1, content_size, f);
                fclose(f);
                
                if (written == content_size) {
                    task->status = 2;
                    task->completed = 1;
                    task->success = 1;
                    task->local_path = strdup(output_path);
                    
                    if (task->element) {
                        set_json_string(task->element, "src", output_path);
                        set_json_bool(task->element, "needs_download", 0);
                    }
                } else {
                    rc = EIO;
                }
            } else {
                rc = EIO;
            }
        }
        free(content);
    }
    
    if (rc != EOK) {
        task->retry_count++;
        if (task->retry_count < 3) {
            task->status = 0; // Trigger internal state back to pending for retry loops
        } else {
            task->status = 3; // Hard fail state limit reached
            task->completed = 1;
            task->success = 0;
            printf("❌ Download failed after 3 retries: %s\n", url);
            show_status_message(global_pauk_ui, "Nakon 3 pokusaja odustajem od slike!", 2000);
        }
    }
    
    g_active_downloads--;
    fibril_mutex_unlock(&g_ipc_mutex);
    
    free(url);
    free(output_path);
    
    // Instruct queue to dispatch subsequent loops
    start_download_from_queue();
    return EOK;
}



void start_download_from_queue(void) {
    fibril_mutex_lock(&g_ipc_mutex);
    
    if (g_active_downloads >= MAX_PARALLEL) {
        fibril_mutex_unlock(&g_ipc_mutex);
        return;
    }
    
    int slot = -1;
    for (int i = 0; i < g_ipc_task_count; i++) {
        if (g_ipc_tasks[i].status == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        fibril_mutex_unlock(&g_ipc_mutex);
        return;
    }
    
    DownloadTask *task = &g_ipc_tasks[slot];
    
    // === 1. SANITIZACIJA ORIGINALNOG URL-A (Čišćenje od Next.js parametara) ===
    char *clean_url = strdup(task->url);
    if (clean_url) {
        char *query_ptr = strchr(clean_url, '?');
        if (query_ptr) {
            *query_ptr = '\0'; // Odsecamo ?v=1.4.2 direktno iz stringa zadatka
        }
        
        // Osvežavamo URL u zadatku sa očišćenom verzijom koja je bezbedna za mrežni drajver
        free(task->url);
        task->url = clean_url;
    }

    // === 2. BEZBEDNO IZDVAJANJE EKSTENZIJE ZA STRUKTURU PUTANJE ===
    static int counter = 0;
    const char *ext = ".jpg"; 
    
    const char *clean_ext = strrchr(task->url, '.');
    if (clean_ext && strlen(clean_ext) <= 5) {
        if (strcasecmp(clean_ext, ".svg") == 0) ext = ".svg";
        else if (strcasecmp(clean_ext, ".png") == 0) ext = ".png";
        else if (strcasecmp(clean_ext, ".jpg") == 0 || strcasecmp(clean_ext, ".jpeg") == 0) ext = ".jpg";
        else if (strcasecmp(clean_ext, ".webp") == 0) ext = ".webp";
        else if (strcasecmp(clean_ext, ".gif") == 0) ext = ".gif";
    }
    
    // Sklapanje finalne putanje na fajl sistemu
    snprintf(task->output_path, sizeof(task->output_path),
             "/tmp/img_%d_%ld%s",
             counter++, (long)time(NULL), ext);
    
    task->status = 1;
    g_active_downloads++;
    
    printf("🚀 [Queue] Pokrecem cist mrezni fibril za URL: %s -> Izlaz: %s\n", task->url, task->output_path);

    // ===== START DOWNLOAD IN A FIBRIL =====
    fid_t fid = fibril_create(download_worker_fibril, (void*)(uintptr_t)slot);
    if (fid) {
        fibril_add_ready(fid);
    } else {
        printf("❌ Failed to create fibril\n");
        task->status = 3;
        g_active_downloads--;
    }
    // =====================================
    
    fibril_mutex_unlock(&g_ipc_mutex);
}



void process_download_queue(pauk_ui_t *pauk_ui) {
    if (g_ipc_task_count == 0) {
      //  printf("📥 No images in queue\n");
        return;
    }
    
  //  printf("📥 Starting download queue (total: %d, max parallel: %d)\n",
     //      g_ipc_task_count, MAX_PARALLEL);
    
    g_active_downloads = 0;
    
    // Start the first batch
    for (int i = 0; i < MAX_PARALLEL; i++) {
        start_download_from_queue();
    }
    
    // Wait for all downloads to complete
    while (1) {
        int all_done = 1;
        
        fibril_mutex_lock(&g_ipc_mutex);
        for (int i = 0; i < g_ipc_task_count; i++) {
            if (g_ipc_tasks[i].status == 0 || g_ipc_tasks[i].status == 1) {
                all_done = 0;
                break;
            }
        }
        fibril_mutex_unlock(&g_ipc_mutex);
        
        if (all_done) {
            break;
        }
        
        fibril_usleep(100 * 1000);
    }
    
    printf("✅ Preuzimanje slika zavrseno!\n");
    show_status_message(pauk_ui, "Preuzimanje slika zavrseno!", 2000);
    // ===== CLOSE CONNECTION AFTER LAST IMAGE =====
    https_close_connection();
    // ============================================
    
    if (pauk_ui) {
        refresh_page_after_js(pauk_ui);
    }
}


void cleanup_download_queue(void) {
    fibril_mutex_lock(&g_ipc_mutex);
    
  //  printf("🧹 Cleaning up download queue...\n");
    
    int killed = 0;
    for (int i = 0; i < g_ipc_task_count; i++) {
        if (g_ipc_tasks[i].status == 1) {
            printf("🛑 Killing task %d (task: %llu, URL: %s)\n",
                   i, (unsigned long long)g_ipc_tasks[i].task_id,
                   g_ipc_tasks[i].url);
            task_kill(g_ipc_tasks[i].task_id);
            killed++;
        }
        if (g_ipc_tasks[i].url) {
            free(g_ipc_tasks[i].url);
            g_ipc_tasks[i].url = NULL;
        }
        if (g_ipc_tasks[i].local_path) {
            free(g_ipc_tasks[i].local_path);
            g_ipc_tasks[i].local_path = NULL;
        }
        g_ipc_tasks[i].status = 0;
        g_ipc_tasks[i].completed = 0;
        g_ipc_tasks[i].success = 0;
        g_ipc_tasks[i].task_id = 0;
        g_ipc_tasks[i].retry_count = 0;
    }
    
    g_ipc_task_count = 0;
    g_active_downloads = 0;
    
    fibril_mutex_unlock(&g_ipc_mutex);
    
    if (killed > 0) {
        printf("✅ Killed %d running tasks\n", killed);
    }
   // printf("🧹 Download queue cleaned up\n");
}

// ============================================================================
// DOWNLOAD WORKER FIBRIL - Uses the library directly
// ============================================================================


// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static int is_image_already_downloaded(const char *original_url) {
    if (!original_url) return 0;
    
    fibril_mutex_lock(&g_completed_mutex);
    for (int i = 0; i < g_completed_count; i++) {
        if (g_completed_images[i].url && strcmp(g_completed_images[i].url, original_url) == 0) {
            fibril_mutex_unlock(&g_completed_mutex);
            return 1;
        }
    }
    fibril_mutex_unlock(&g_completed_mutex);
    return 0;
}

static void mark_image_downloaded(const char *original_url, const char *local_path) {
    if (!original_url || !local_path) return;
    
    fibril_mutex_lock(&g_completed_mutex);
    if (g_completed_count < 512) {
        g_completed_images[g_completed_count].url = strdup(original_url);
        g_completed_images[g_completed_count].local_path = strdup(local_path);
        g_completed_count++;
        printf("📝 Marked as downloaded: %s -> %s (total: %d)\n",
               original_url, local_path, g_completed_count);
    }
    fibril_mutex_unlock(&g_completed_mutex);
}

static const char* get_local_path_for_url(const char *original_url) {
    if (!original_url) return NULL;
    
    fibril_mutex_lock(&g_completed_mutex);
    for (int i = 0; i < g_completed_count; i++) {
        if (g_completed_images[i].url && strcmp(g_completed_images[i].url, original_url) == 0) {
            fibril_mutex_unlock(&g_completed_mutex);
            return g_completed_images[i].local_path;
        }
    }
    fibril_mutex_unlock(&g_completed_mutex);
    return NULL;
}

static const char* get_image_format_from_data(const unsigned char *data, size_t size) {
    if (size < 12) return NULL;
    
    // PNG: \x89PNG\r\n\x1a\n
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
        return ".png";
    // JPEG: \xFF\xD8\xFF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return ".jpg";
    // WebP: RIFF....WEBP
    if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
        size > 12 && data[8] == 0x57 && data[9] == 0x49 && data[10] == 0x42 && data[11] == 0x50)
        return ".webp";
    // GIF: GIF8
    if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x38)
        return ".gif";
    // BMP: BM
    if (data[0] == 0x42 && data[1] == 0x4D)
        return ".bmp";
    
    return NULL;
}

static void update_element_url_recursive(cJSON *element, const char *original_url, const char *local_path) {
    if (!element) return;
    
    const char *elem_url = get_json_string(element, "original_url", "");
    if (elem_url && strlen(elem_url) > 0 && strcmp(elem_url, original_url) == 0) {
        int elem_id = get_json_number(element, "element_id", -1);
        printf(" ✅ Updating element (id=%d): %s\n", elem_id, original_url);
        set_json_string(element, "src", local_path);
        set_json_bool(element, "needs_download", 0);
        set_json_bool(element, "needs_placeholder", 0);
        set_json_bool(element, "download_queued", 0);
    }
    
    cJSON *children = cJSON_GetObjectItem(element, "children");
    if (children && cJSON_IsArray(children)) {
        cJSON *child;
        cJSON_ArrayForEach(child, children) {
            update_element_url_recursive(child, original_url, local_path);
        }
    }
}

static void update_all_elements_by_url(const char *original_url, const char *local_path) {
    if (!original_url || !local_path || !global_computed_layout) return;
    
   // printf("🔄 Updating all elements with URL: %s -> %s\n", original_url, local_path);
    cJSON *body = cJSON_GetArrayItem(global_computed_layout, 0);
    if (body) {
        update_element_url_recursive(body, original_url, local_path);
    }
}

char* download_image_to_cache(const char *encoded_url, const char *original_url) {
    if (!encoded_url || !original_url) {
     //   printf("❌ download_image_to_cache: NULL URL\n");
        return NULL;
    }
    
    if (strlen(encoded_url) < 10) {
      //  printf("❌ download_image_to_cache: URL too short: %s\n", encoded_url);
        return NULL;
    }
    
    if (is_image_already_downloaded(original_url)) {
      //  printf("⏭️ Already downloaded: %s, skipping\n", original_url);
        const char *cached_path = get_local_path_for_url(original_url);
        if (cached_path) {
            update_all_elements_by_url(original_url, cached_path);
        }
        return NULL;
    }
    
  // printf("📥 download_image_to_cache: %s\n", encoded_url);
    
    if (strncmp(encoded_url, "http://", 7) != 0 && strncmp(encoded_url, "https://", 8) != 0) {
        printf("❌ Unsupported protocol (only http/https): %s\n", encoded_url);
        return NULL;
    }
    
    char *image_data = NULL;
    size_t image_size = 0;
    errno_t rc = EIO;
    
    if (strncmp(encoded_url, "https://", 8) == 0) {
      //  printf(" Attempting HTTPS download with keep-alive...\n");
        rc = fetch_https_content_keep_alive(encoded_url, &image_data, &image_size);
       // printf(" HTTPS result: %d, size: %zu\n", rc, image_size);
    }
    
    if (rc != EOK || !image_data || image_size == 0) {
        printf("❌ Download failed: rc=%d, data=%p, size=%zu\n", rc, (void*)image_data, image_size);
        if (image_data) free(image_data);
        return NULL;
    }
    
 //  printf("✅ Downloaded %zu bytes\n", image_size);
    
    const char *detected_ext = get_image_format_from_data((unsigned char*)image_data, image_size);
    const char *ext;
    char lower_ext[16];
    
    if (detected_ext) {
        ext = detected_ext;
     //   printf("📊 Detected format from data: %s\n", ext);
    } else {
        const char *url_ext = strrchr(original_url, '.');
        if (url_ext) {
            strncpy(lower_ext, url_ext, sizeof(lower_ext) - 1);
            lower_ext[sizeof(lower_ext) - 1] = '\0';
            for (char *p = lower_ext; *p; p++) {
                *p = tolower(*p);
            }
            ext = lower_ext;
            printf("⚠️ Could not detect format from data, using URL extension: %s\n", ext);
        } else {
            ext = ".jpg";
            printf("⚠️ No extension found, defaulting to .jpg\n");
        }
    }
    
    strcpy(lower_ext, ext);
    for (char *p = lower_ext; *p; p++) {
        *p = tolower(*p);
    }
    
    const char *valid_extensions[] = {
        ".jpg", ".jpeg", ".png", ".gif", ".webp",
        ".bmp", ".ico", NULL
    };
    int valid = 0;
    for (int i = 0; valid_extensions[i] != NULL; i++) {
        if (strcmp(lower_ext, valid_extensions[i]) == 0) {
            valid = 1;
            break;
        }
    }
    if (!valid) {
        printf("⚠️ Unknown extension: %s, defaulting to .jpg\n", ext);
        strcpy(lower_ext, ".jpg");
    }
    
    static int img_counter = 0;
    char cache_path[256];
    snprintf(cache_path, sizeof(cache_path), "/tmp/img_%d_%lu%s",
             img_counter++, (unsigned long)time(NULL), lower_ext);
    printf("💾 Saving to: %s\n", cache_path);
    
    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        printf("❌ Cannot open file for writing: %s\n", cache_path);
        free(image_data);
        return NULL;
    }
    
    size_t written = fwrite(image_data, 1, image_size, f);
    fclose(f);
    free(image_data);
    
    if (written != image_size) {
        printf("❌ Write incomplete: %zu of %zu bytes\n", written, image_size);
        vfs_unlink_path(cache_path);
        return NULL;
    }
    
   // printf("✅ Image saved: %s (%zu bytes)\n", cache_path, image_size);
    mark_image_downloaded(original_url, cache_path);
    update_all_elements_by_url(original_url, cache_path);
    
    return strdup(cache_path);
}

char* save_raw_image_to_cache(const char *original_url,
                              unsigned char *image_data,
                              size_t image_size) {
    if (!original_url || !image_data || image_size == 0) {
        printf("❌ save_raw_image_to_cache: Invalid parameters\n");
        return NULL;
    }
    
    const char *detected_ext = get_image_format_from_data(image_data, image_size);
    if (!detected_ext) {
        const char *url_ext = strrchr(original_url, '.');
        if (url_ext && (strcasecmp(url_ext, ".jpg") == 0 ||
                        strcasecmp(url_ext, ".jpeg") == 0 ||
                        strcasecmp(url_ext, ".png") == 0 ||
                        strcasecmp(url_ext, ".gif") == 0 ||
                        strcasecmp(url_ext, ".webp") == 0)) {
            detected_ext = url_ext;
      //      printf(" Using URL extension: %s\n", detected_ext);
        } else {
        //    printf("❌ Cannot determine format, skipping: %s\n", original_url);
            return NULL;
        }
    }
    
    static int img_counter = 0;
    char cache_path[256];
    snprintf(cache_path, sizeof(cache_path), "/tmp/img_%d_%lu%s",
             img_counter++, (unsigned long)time(NULL), detected_ext);
    
    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        printf("❌ Cannot create file: %s\n", cache_path);
        return NULL;
    }
    
    size_t written = fwrite(image_data, 1, image_size, f);
    fclose(f);
    
    if (written != image_size) {
        printf("❌ Write incomplete: %zu of %zu bytes\n", written, image_size);
        vfs_unlink_path(cache_path);
        return NULL;
    }
    
  //  printf("💾 Saved image: %s (%zu bytes)\n", cache_path, image_size);
    mark_image_downloaded(original_url, cache_path);
    update_all_elements_by_url(original_url, cache_path);
    
    return strdup(cache_path);
}
