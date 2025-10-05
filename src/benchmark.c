/*
 * Benchmark Implementation
 */

#include "benchmark.h"
#include <sys/time.h>
#include <math.h>

void benchmark_init(void) {
    benchmark_clear_results();
}

void benchmark_cleanup(void) {
    // Currently nothing to cleanup; keep for API symmetry
}

BenchmarkResult g_benchmarks[MAX_BENCHMARKS];
int g_benchmark_count = 0;

void benchmark_print_results(void) {
    printf("\n=== BENCHMARK RESULTS ===\n");
    printf("%-30s %12s %12s %8s %12s\n", 
           "Name", "Time (ms)", "Per Iter (ms)", "Iters", "Ops/sec");
    printf("%-30s %12s %12s %8s %12s\n", 
           "----", "----------", "-------------", "-----", "--------");
    
    for (int i = 0; i < g_benchmark_count; i++) {
        printf("%-30s %12.3f %12.6f %8d %12.0f\n",
               g_benchmarks[i].name,
               g_benchmarks[i].time_ms * g_benchmarks[i].iterations,
               g_benchmarks[i].time_ms,
               g_benchmarks[i].iterations,
               g_benchmarks[i].ops_per_sec);
    }
    printf("\n");
}

void benchmark_generate_report(const char* filename) {
    benchmark_export_csv(filename);
}

void benchmark_compare_with_previous(const char* report_file, const char* previous_file) {
    const char *baseline_path = previous_file && previous_file[0] ? previous_file : "benchmark_baseline.csv";
    const char *report_path = report_file && report_file[0] ? report_file : "benchmark_report.csv";

    // If baseline does not exist, create it from current report (or generate report first)
    FILE *baseline = fopen(baseline_path, "r");
    if (!baseline) {
        FILE *rep = fopen(report_path, "r");
        if (!rep) {
            // No report on disk → export current in-memory results to report_path
            benchmark_export_csv(report_path);
            rep = fopen(report_path, "r");
        }
        if (rep) {
            FILE *out = fopen(baseline_path, "w");
            if (out) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), rep)) > 0) {
                    fwrite(buf, 1, n, out);
                }
                fclose(out);
                printf("Baseline created at %s from %s\n", baseline_path, report_path);
            }
            fclose(rep);
        } else {
            printf("Warning: Could not create baseline; no report available\n");
        }
    } else {
        fclose(baseline);
    }

    benchmark_compare_with_baseline(baseline_path);
}

void benchmark_export_csv(const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open %s for writing\n", filename);
        return;
    }
    
    // Write header
    fprintf(file, "timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes\n");
    
    // Get current timestamp
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Write data
    for (int i = 0; i < g_benchmark_count; i++) {
        fprintf(file, "%s,%s,%.6f,%d,%.0f,%zu\n",
                timestamp,
                g_benchmarks[i].name,
                g_benchmarks[i].time_ms,
                g_benchmarks[i].iterations,
                g_benchmarks[i].ops_per_sec,
                g_benchmarks[i].memory_bytes);
    }
    
    fclose(file);
    printf("Benchmark results exported to %s (%d entries)\n", filename, g_benchmark_count);
}

static void append_history(const char *name, double current_time, int iterations, double ops_per_sec, size_t memory_bytes, double change_percent) {
    FILE* file = fopen("benchmark_history.csv", "a");
    if (!file) return;
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(file, "%s,%s,%.6f,%d,%.0f,%zu,%.2f\n", ts, name, current_time, iterations, ops_per_sec, memory_bytes, change_percent);
    fclose(file);
}

void benchmark_compare_with_baseline(const char* baseline_file) {
    FILE* file = fopen(baseline_file, "r");
    if (!file) {
        printf("Warning: Could not open baseline file %s\n", baseline_file);
        return;
    }
    
    char line[256];
    fgets(line, sizeof(line), file); // Skip header
    
    printf("\n=== PERFORMANCE COMPARISON ===\n");
    printf("%-30s %12s %12s %12s\n", 
           "Name", "Current (ms)", "Baseline (ms)", "Change (%)");
    printf("%-30s %12s %12s %12s\n", 
           "----", "-------------", "-------------", "----------");
    
    int any_significant = 0;
    const double threshold = 2.0; // percent

    while (fgets(line, sizeof(line), file)) {
        char name[64];
        double baseline_time;
        int iterations;
        double ops_per_sec;
        size_t memory_bytes;
        
        if (sscanf(line, "%*[^,],%63[^,],%lf,%d,%lf,%zu", 
                   name, &baseline_time, &iterations, &ops_per_sec, &memory_bytes) == 5) {
            
            // Find matching current benchmark
            for (int i = 0; i < g_benchmark_count; i++) {
                if (strcmp(g_benchmarks[i].name, name) == 0) {
                    double current_time = g_benchmarks[i].time_ms;
                    double change_percent = ((current_time - baseline_time) / baseline_time) * 100.0;
                    
                    printf("%-30s %12.6f %12.6f %+11.2f%%\n",
                           name, current_time, baseline_time, change_percent);
                    if (fabs(change_percent) >= threshold) {
                        any_significant = 1;
                        append_history(name,
                                       current_time,
                                       g_benchmarks[i].iterations,
                                       g_benchmarks[i].ops_per_sec,
                                       g_benchmarks[i].memory_bytes,
                                       change_percent);
                    }
                    break;
                }
            }
        }
    }
    
    fclose(file);
    printf("\n");

    // Update baseline if significant change detected
    if (any_significant) {
        benchmark_export_csv(baseline_file);
        printf("Baseline updated: %s (>= %.2f%% change)\n", baseline_file, threshold);
    }
}

void benchmark_clear_results(void) {
    g_benchmark_count = 0;
    memset(g_benchmarks, 0, sizeof(g_benchmarks));
}
