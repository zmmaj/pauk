/*
 * Time utilities for Pauk Browser
 * HelenOS native time functions
 */

 #ifndef TIME_UTILS_H
 #define TIME_UTILS_H
 
 #include <time.h>
 #include <stdint.h>
 #include <stdbool.h>
 #include <errno.h>
 
 __C_DECLS_BEGIN;
 
 /* ========== Basic Time Functions ========== */
 
 /** Get system uptime in milliseconds (monotonic - best for timing) */
 extern uint64_t get_uptime_ms(void);
 
 /** Get system uptime in microseconds */
 extern uint64_t get_uptime_us(void);
 
 /** Get system uptime in nanoseconds */
 extern uint64_t get_uptime_ns(void);
 
 /** Get realtime (wall clock) in milliseconds */
 extern uint64_t get_realtime_ms(void);
 
 /** Get realtime (wall clock) in microseconds */
 extern uint64_t get_realtime_us(void);
 
 /* ========== Sleep/Delay Functions ========== */
 
 /** Sleep for milliseconds using HelenOS udelay */
 extern void sleep_ms(uint32_t ms);
 
 /** Sleep for microseconds using HelenOS udelay */
 extern void sleep_us(uint32_t us);
 
 
 /* ========== Stopwatch/Timer Structures ========== */
 
 /** Simple stopwatch for measuring elapsed time */
 typedef struct {
     uint64_t start_time;
 } stopwatch_t;
 
 /** Initialize stopwatch */
 extern void stopwatch_start(stopwatch_t *sw);
 
 /** Get elapsed milliseconds since stopwatch_start */
 extern uint64_t stopwatch_elapsed_ms(stopwatch_t *sw);
 
 /** Get elapsed microseconds since stopwatch_start */
 extern uint64_t stopwatch_elapsed_us(stopwatch_t *sw);
 
 /** Get elapsed nanoseconds since stopwatch_start */
 extern uint64_t stopwatch_elapsed_ns(stopwatch_t *sw);
 
 /** Reset stopwatch to current time */
 extern void stopwatch_reset(stopwatch_t *sw);
 
 /* ========== Timeout Functions ========== */
 
 /** Timeout structure for network operations */
 typedef struct {
     uint64_t start_time;
     uint64_t timeout_ms;
 } timeout_t;
 
 /** Initialize timeout with duration in milliseconds */
 extern void timeout_init(timeout_t *to, uint64_t timeout_ms);
 
 /** Check if timeout has expired */
 extern bool timeout_expired(timeout_t *to);
 
 /** Get remaining time in milliseconds (0 if expired) */
 extern uint64_t timeout_remaining_ms(timeout_t *to);
 
 /** Check if a specific start time has exceeded timeout */
 extern bool has_timeout_expired(uint64_t start_time_ms, uint64_t timeout_ms);
 
 /* ========== Rate Limiter ========== */
 
 /** Rate limiter for animations, scrolling, etc. */
 typedef struct {
     uint64_t last_call_time;
     uint32_t min_interval_ms;
 } rate_limiter_t;
 
 /** Initialize rate limiter */
 extern void rate_limiter_init(rate_limiter_t *rl, uint32_t min_interval_ms);
 
 /** Check if enough time has passed to allow action */
 extern bool rate_limiter_allow(rate_limiter_t *rl);
 
 /** Get time until next allowed action (0 if allowed now) */
 extern uint32_t rate_limiter_wait_ms(rate_limiter_t *rl);
 
 /* ========== Performance Monitoring ========== */
 
 /** Performance timing structure for page loads */
 typedef struct {
     uint64_t navigation_start;
     uint64_t dom_loaded;
     uint64_t page_loaded;
     uint64_t first_paint;
 } perf_timing_t;
 
 /** Initialize performance timing */
 extern void perf_timing_init(perf_timing_t *pt);
 
 /** Mark navigation start */
 extern void perf_timing_navigation_start(perf_timing_t *pt);
 
 /** Mark DOM loaded */
 extern void perf_timing_dom_loaded(perf_timing_t *pt);
 
 /** Mark page fully loaded */
 extern void perf_timing_page_loaded(perf_timing_t *pt);
 
 /** Mark first paint */
 extern void perf_timing_first_paint(perf_timing_t *pt);
 
 /** Get elapsed from navigation start in milliseconds */
 extern uint64_t perf_timing_elapsed_ms(perf_timing_t *pt, uint64_t mark_time);
 
 /** Print performance summary */
 extern void perf_timing_print(perf_timing_t *pt);
 
 /* ========== Time Conversion Helpers ========== */
 
 /** Convert seconds to milliseconds */
 static inline uint64_t sec_to_ms(uint64_t sec) {
     return sec * 1000ULL;
 }
 
 /** Convert milliseconds to seconds */
 static inline uint64_t ms_to_sec(uint64_t ms) {
     return ms / 1000ULL;
 }
 
 /** Convert milliseconds to microseconds */
 static inline uint64_t ms_to_us(uint64_t ms) {
     return ms * 1000ULL;
 }
 
 /** Convert microseconds to milliseconds */
 static inline uint64_t us_to_ms(uint64_t us) {
     return us / 1000ULL;
 }
 
 /* ========== Formatted Time Strings ========== */
 
 /** Get formatted time string for logging (HH:MM:SS.mmm) */
 extern void time_format_ms(char *buffer, size_t buffer_size, uint64_t ms);
 
 /** Get uptime as formatted string */
 extern void time_uptime_str(char *buffer, size_t buffer_size);
 
 __C_DECLS_END;
 
 #endif /* TIME_UTILS_H */
