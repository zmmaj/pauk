/*
 * Time utilities for Pauk Browser
 * HelenOS native time functions implementation
 */

 #include "time_utils.h"
 #include <stdio.h>
 #include <string.h>
 #include <inttypes.h>
 
 /* ========== Basic Time Functions ========== */
 
 uint64_t get_uptime_ms(void) {
     struct timespec ts;
     getuptime(&ts);
     return (uint64_t)SEC2MSEC(ts.tv_sec) + (uint64_t)NSEC2MSEC(ts.tv_nsec);
 }
 
 uint64_t get_uptime_us(void) {
     struct timespec ts;
     getuptime(&ts);
     return (uint64_t)SEC2USEC(ts.tv_sec) + (uint64_t)NSEC2USEC(ts.tv_nsec);
 }
 
 uint64_t get_uptime_ns(void) {
     struct timespec ts;
     getuptime(&ts);
     return (uint64_t)SEC2NSEC(ts.tv_sec) + (uint64_t)ts.tv_nsec;
 }
 
 uint64_t get_realtime_ms(void) {
     struct timespec ts;
     getrealtime(&ts);
     return (uint64_t)SEC2MSEC(ts.tv_sec) + (uint64_t)NSEC2MSEC(ts.tv_nsec);
 }
 
 uint64_t get_realtime_us(void) {
     struct timespec ts;
     getrealtime(&ts);
     return (uint64_t)SEC2USEC(ts.tv_sec) + (uint64_t)NSEC2USEC(ts.tv_nsec);
 }
 
 /* ========== Sleep/Delay Functions ========== */
 
 void sleep_ms(uint32_t ms) {
     udelay(MSEC2USEC(ms));
 }
 
 void sleep_us(uint32_t us) {
     udelay(us);
 }
 
 /* ========== Stopwatch Functions ========== */
 
 void stopwatch_start(stopwatch_t *sw) {
     if (sw) {
         sw->start_time = get_uptime_ms();
     }
 }
 
 uint64_t stopwatch_elapsed_ms(stopwatch_t *sw) {
     if (!sw) return 0;
     return get_uptime_ms() - sw->start_time;
 }
 
 uint64_t stopwatch_elapsed_us(stopwatch_t *sw) {
     if (!sw) return 0;
     return get_uptime_us() - (sw->start_time * 1000ULL);
 }
 
 uint64_t stopwatch_elapsed_ns(stopwatch_t *sw) {
     if (!sw) return 0;
     return get_uptime_ns() - (sw->start_time * 1000000ULL);
 }
 
 void stopwatch_reset(stopwatch_t *sw) {
     if (sw) {
         sw->start_time = get_uptime_ms();
     }
 }
 
 /* ========== Timeout Functions ========== */
 
 void timeout_init(timeout_t *to, uint64_t timeout_ms) {
     if (to) {
         to->start_time = get_uptime_ms();
         to->timeout_ms = timeout_ms;
     }
 }
 
 bool timeout_expired(timeout_t *to) {
     if (!to) return true;
     return has_timeout_expired(to->start_time, to->timeout_ms);
 }
 
 uint64_t timeout_remaining_ms(timeout_t *to) {
     if (!to) return 0;
     uint64_t elapsed = get_uptime_ms() - to->start_time;
     if (elapsed >= to->timeout_ms) return 0;
     return to->timeout_ms - elapsed;
 }
 
 bool has_timeout_expired(uint64_t start_time_ms, uint64_t timeout_ms) {
     return (get_uptime_ms() - start_time_ms) >= timeout_ms;
 }
 
 /* ========== Rate Limiter Functions ========== */
 
 void rate_limiter_init(rate_limiter_t *rl, uint32_t min_interval_ms) {
     if (rl) {
         rl->last_call_time = 0;
         rl->min_interval_ms = min_interval_ms;
     }
 }
 
 bool rate_limiter_allow(rate_limiter_t *rl) {
     if (!rl) return true;
     
     uint64_t now = get_uptime_ms();
     if (now - rl->last_call_time >= rl->min_interval_ms) {
         rl->last_call_time = now;
         return true;
     }
     return false;
 }
 
 uint32_t rate_limiter_wait_ms(rate_limiter_t *rl) {
     if (!rl) return 0;
     
     uint64_t now = get_uptime_ms();
     uint64_t elapsed = now - rl->last_call_time;
     
     if (elapsed >= rl->min_interval_ms) {
         return 0;
     }
     
     return (uint32_t)(rl->min_interval_ms - elapsed);
 }
 
 /* ========== Performance Monitoring ========== */
 
 void perf_timing_init(perf_timing_t *pt) {
     if (pt) {
         memset(pt, 0, sizeof(perf_timing_t));
     }
 }
 
 void perf_timing_navigation_start(perf_timing_t *pt) {
     if (pt) {
         pt->navigation_start = get_uptime_ms();
     }
 }
 
 void perf_timing_dom_loaded(perf_timing_t *pt) {
     if (pt) {
         pt->dom_loaded = get_uptime_ms();
     }
 }
 
 void perf_timing_page_loaded(perf_timing_t *pt) {
     if (pt) {
         pt->page_loaded = get_uptime_ms();
     }
 }
 
 void perf_timing_first_paint(perf_timing_t *pt) {
     if (pt) {
         pt->first_paint = get_uptime_ms();
     }
 }
 
 uint64_t perf_timing_elapsed_ms(perf_timing_t *pt, uint64_t mark_time) {
     if (!pt || mark_time == 0) return 0;
     return mark_time - pt->navigation_start;
 }
 
 void perf_timing_print(perf_timing_t *pt) {
     if (!pt) return;
     
     printf("\n========== Performance Timing ==========\n");
     
     if (pt->dom_loaded > 0) {
         printf("DOM Loading:      %" PRIu64 " ms\n", 
                perf_timing_elapsed_ms(pt, pt->dom_loaded));
     }
     
     if (pt->first_paint > 0) {
         printf("First Paint:      %" PRIu64 " ms\n", 
                perf_timing_elapsed_ms(pt, pt->first_paint));
     }
     
     if (pt->page_loaded > 0) {
         printf("Page Loaded:      %" PRIu64 " ms\n", 
                perf_timing_elapsed_ms(pt, pt->page_loaded));
     }
     
     printf("========================================\n");
 }
 
 /* ========== Formatted Time Strings ========== */
 
 void time_format_ms(char *buffer, size_t buffer_size, uint64_t ms) {
     if (!buffer || buffer_size == 0) return;
     
     uint64_t hours = ms / 3600000ULL;
     uint64_t minutes = (ms % 3600000ULL) / 60000ULL;
     uint64_t seconds = (ms % 60000ULL) / 1000ULL;
     uint64_t milliseconds = ms % 1000ULL;
     
     snprintf(buffer, buffer_size, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
              hours, minutes, seconds, milliseconds);
 }
 
 void time_uptime_str(char *buffer, size_t buffer_size) {
     if (!buffer || buffer_size == 0) return;
     time_format_ms(buffer, buffer_size, get_uptime_ms());
 }
