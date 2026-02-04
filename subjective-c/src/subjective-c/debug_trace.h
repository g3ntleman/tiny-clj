/**
 * @file debug_trace.h
 * @brief Unified debug tracing for allocations with environment variable control.
 */

#ifndef DEBUG_TRACE_H
#define DEBUG_TRACE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef DEBUG

/**
 * @brief Configuration for a debug trace category.
 */
typedef struct {
    const char *env_var_name;      ///< Environment variable to enable tracing (e.g., "TINYCLJ_TRACE_LIST_ALLOC")
    const char *env_var_bt_name;   ///< Environment variable to enable backtraces (e.g., "TINYCLJ_TRACE_LIST_ALLOC_BT")
    const char *prefix;            ///< Log prefix (e.g., "[list-alloc]")
    int max_traces;                ///< Maximum number of traces to output (default: 200)
} DebugTraceConfig;

/**
 * @brief Check if tracing is enabled for this category.
 * @param cfg Trace configuration
 * @return true if enabled, false otherwise (cached per category)
 */
bool debug_trace_enabled(const DebugTraceConfig *cfg);

/**
 * @brief Check if backtraces are enabled for this category.
 * @param cfg Trace configuration
 * @return true if enabled, false otherwise (cached per category)
 */
bool debug_trace_backtrace_enabled(const DebugTraceConfig *cfg);

/**
 * @brief Log an allocation with optional backtrace.
 * @param cfg Trace configuration
 * @param ptr Allocated pointer
 * @param first First field pointer (or NULL)
 * @param rest Rest field pointer (or NULL)
 * @param trace_count Pointer to trace counter (incremented if logged)
 */
void debug_trace_allocation(const DebugTraceConfig *cfg, void *ptr, void *first, void *rest, int *trace_count);

#else
// Release builds: all tracing disabled at compile-time
#define debug_trace_enabled(cfg) false
#define debug_trace_backtrace_enabled(cfg) false
#define debug_trace_allocation(cfg, ptr, first, rest, trace_count) ((void)0)
#endif

#endif // DEBUG_TRACE_H
