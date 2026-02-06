/**
 * @file debug_trace.c
 * @brief Unified debug tracing implementation.
 */

#include "debug_trace.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#if defined(__GNUC__) && !defined(ESP32_BUILD) && !defined(ESP_PLATFORM)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#endif

bool debug_trace_enabled(const DebugTraceConfig *cfg) {
    if (!cfg || !cfg->env_var_name) return false;
    
    // Cache result per config (simple linear search, max ~10 categories expected)
    static struct {
        const char *env_var;
        int cached;
    } cache[16];
    static int cache_count = 0;
    
    // Check cache
    for (int i = 0; i < cache_count; i++) {
        if (cache[i].env_var == cfg->env_var_name) {
            return cache[i].cached == 1;
        }
    }
    
    // Not cached: check environment and cache result
    const char *env = getenv(cfg->env_var_name);
    int enabled = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    
    if (cache_count < 16) {
        cache[cache_count].env_var = cfg->env_var_name;
        cache[cache_count].cached = enabled;
        cache_count++;
    }
    
    return enabled == 1;
}

bool debug_trace_backtrace_enabled(const DebugTraceConfig *cfg) {
#ifdef HAVE_BACKTRACE
    if (!cfg || !cfg->env_var_bt_name) return false;
    
    static struct {
        const char *env_var;
        int cached;
    } cache[16];
    static int cache_count = 0;
    
    for (int i = 0; i < cache_count; i++) {
        if (cache[i].env_var == cfg->env_var_bt_name) {
            return cache[i].cached == 1;
        }
    }
    
    const char *env = getenv(cfg->env_var_bt_name);
    int enabled = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    
    if (cache_count < 16) {
        cache[cache_count].env_var = cfg->env_var_bt_name;
        cache[cache_count].cached = enabled;
        cache_count++;
    }
    
    return enabled == 1;
#else
    return false;
#endif
}

void debug_trace_allocation(const DebugTraceConfig *cfg, void *ptr, void *first, void *rest, int *trace_count) {
    if (!cfg || !trace_count) return;
    if (!debug_trace_enabled(cfg)) return;
    
    int max_traces = cfg->max_traces > 0 ? cfg->max_traces : 200;
    if (*trace_count >= max_traces) return;
    
    const char *prefix = cfg->prefix ? cfg->prefix : "[alloc]";
    fprintf(stderr, "%s %p first=%p rest=%p\n", prefix, ptr, first, rest);
    
#ifdef HAVE_BACKTRACE
    if (debug_trace_backtrace_enabled(cfg)) {
        void *bt[16];
        int n = backtrace(bt, 16);
        char **symbols = backtrace_symbols(bt, n);
        if (symbols) {
            for (int i = 0; i < n; i++) {
                fprintf(stderr, "  %s\n", symbols[i]);
            }
            CLJ_FREE(symbols);
        }
    }
#endif
    
    (*trace_count)++;
}

#endif // DEBUG
