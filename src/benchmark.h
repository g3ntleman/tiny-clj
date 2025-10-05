/*
 * Benchmark Extension for Unity
 * 
 * Provides simple benchmarking capabilities on top of Unity test framework:
 * - Time measurement for functions
 * - Memory usage tracking
 * - Performance regression detection
 * - CSV export for historical data
 */

#ifndef TINY_CLJ_BENCHMARK_H
#define TINY_CLJ_BENCHMARK_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Benchmark result structure
typedef struct {
    char name[64];
    double time_ms;
    size_t memory_bytes;
    int iterations;
    double ops_per_sec;
} BenchmarkResult;

// Global benchmark storage
#define MAX_BENCHMARKS 100
extern BenchmarkResult g_benchmarks[MAX_BENCHMARKS];
extern int g_benchmark_count;

// Benchmark macros
#define BENCHMARK_START(name) \
    struct timespec _start, _end; \
    clock_gettime(CLOCK_MONOTONIC, &_start); \
    const char* _bench_name = name;

#define BENCHMARK_END() \
    clock_gettime(CLOCK_MONOTONIC, &_end); \
    double _time_ms = (_end.tv_sec - _start.tv_sec) * 1000.0 + \
                     (_end.tv_nsec - _start.tv_nsec) / 1000000.0; \
    if (g_benchmark_count < MAX_BENCHMARKS) { \
        strncpy(g_benchmarks[g_benchmark_count].name, _bench_name, sizeof(g_benchmarks[g_benchmark_count].name) - 1); \
        g_benchmarks[g_benchmark_count].time_ms = _time_ms; \
        g_benchmarks[g_benchmark_count].iterations = 1; \
        g_benchmark_count++; \
    }

#define BENCHMARK_ITERATIONS(name, iter_count) \
    struct timespec _start, _end; \
    clock_gettime(CLOCK_MONOTONIC, &_start); \
    const char* _bench_name = name; \
    int _iterations = iter_count;

#define BENCHMARK_ITERATIONS_END() \
    clock_gettime(CLOCK_MONOTONIC, &_end); \
    double _total_time_ms = (_end.tv_sec - _start.tv_sec) * 1000.0 + \
                           (_end.tv_nsec - _start.tv_nsec) / 1000000.0; \
    double _time_ms = _total_time_ms / _iterations; \
    double _ops_per_sec = (_iterations * 1000.0) / _total_time_ms; \
    if (g_benchmark_count < MAX_BENCHMARKS) { \
        strncpy(g_benchmarks[g_benchmark_count].name, _bench_name, sizeof(g_benchmarks[g_benchmark_count].name) - 1); \
        g_benchmarks[g_benchmark_count].name[sizeof(g_benchmarks[g_benchmark_count].name) - 1] = '\0'; \
        g_benchmarks[g_benchmark_count].time_ms = _time_ms; \
        g_benchmarks[g_benchmark_count].iterations = _iterations; \
        g_benchmarks[g_benchmark_count].ops_per_sec = _ops_per_sec; \
        g_benchmarks[g_benchmark_count].memory_bytes = 0; \
        g_benchmark_count++; \
    }

// Memory tracking (simplified)
#define BENCHMARK_MEMORY_START() size_t _mem_start = 0; // Placeholder
#define BENCHMARK_MEMORY_END() size_t _mem_end = 0; // Placeholder

// Utility functions
void benchmark_print_results(void);
void benchmark_export_csv(const char* filename);
void benchmark_compare_with_baseline(const char* baseline_file);
void benchmark_clear_results(void);

// Simple initialization/cleanup and compatibility wrappers
void benchmark_init(void);
void benchmark_cleanup(void);
void benchmark_generate_report(const char* filename);
void benchmark_compare_with_previous(const char* report_file, const char* previous_file);

#endif // TINY_CLJ_BENCHMARK_H
