#include "executable_size.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mini_format.h"

static void mini_fprintf(FILE *stream, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    (void)clj_mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stream ? stream : stdout);
}

// Global size tracking
ExecutableSizeInfo g_size_measurements[10];
int g_size_measurement_count = 0;

uint64_t get_executable_size(const char *executable_path) {
    struct stat st;
    if (stat(executable_path, &st) == 0) {
        return (uint64_t)st.st_size;
    }
    return 0;
}

void measure_executable_sizes(void) {
    const char *executables[] = {
        "tiny-clj",
        "test-clojure-core",
        "test-namespace-unity",
        "test-global-singletons-unity",
        "test-alloc-macros-unity",
        "test-benchmark-simple"
    };
    
    int num_executables = sizeof(executables) / sizeof(executables[0]);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    g_size_measurement_count = 0;
    
    for (int i = 0; i < num_executables && g_size_measurement_count < 10; i++) {
        uint64_t size = get_executable_size(executables[i]);
        if (size > 0) {
            strncpy(g_size_measurements[g_size_measurement_count].name, 
                   executables[i], sizeof(g_size_measurements[g_size_measurement_count].name) - 1);
            g_size_measurements[g_size_measurement_count].size_bytes = size;
            g_size_measurements[g_size_measurement_count].total_size = size;
            
            // Simplified size breakdown (would need objdump for accurate breakdown)
            g_size_measurements[g_size_measurement_count].text_size = size * 0.7;  // ~70% code
            g_size_measurements[g_size_measurement_count].data_size = size * 0.2;  // ~20% data
            g_size_measurements[g_size_measurement_count].bss_size = size * 0.1;   // ~10% BSS
            
            strftime(g_size_measurements[g_size_measurement_count].timestamp, 
                    sizeof(g_size_measurements[g_size_measurement_count].timestamp),
                    "%Y-%m-%d %H:%M:%S", tm_info);
            
            g_size_measurement_count++;
        }
    }
}

void export_size_history_csv(void) {
    FILE *file = fopen("executable_size_history.csv", "a");
    if (!file) {
        mini_fprintf(stdout, "Warning: Could not open executable_size_history.csv for writing\n");
        return;
    }
    
    // Write header if file is empty
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        mini_fprintf(file, "timestamp,name,size_bytes,text_size,data_size,bss_size,total_size\n");
    }
    
    for (int i = 0; i < g_size_measurement_count; i++) {
        mini_fprintf(file, "%s,%s,%lu,%lu,%lu,%lu,%lu\n",
                g_size_measurements[i].timestamp,
                g_size_measurements[i].name,
                (unsigned long)g_size_measurements[i].size_bytes,
                (unsigned long)g_size_measurements[i].text_size,
                (unsigned long)g_size_measurements[i].data_size,
                (unsigned long)g_size_measurements[i].bss_size,
                (unsigned long)g_size_measurements[i].total_size);
    }
    
    fclose(file);
    mini_fprintf(stdout, "Executable size history exported to executable_size_history.csv\n");
}

void print_size_analysis(void) {
    mini_fprintf(stdout, "\n=== Executable Size Analysis ===\n");
    mini_fprintf(stdout, "Executable Name              | Size (KB) | Text (KB) | Data (KB) | BSS (KB) | Total (KB)\n");
    mini_fprintf(stdout, "-----------------------------|-----------|-----------|-----------|----------|-----------\n");
    
    for (int i = 0; i < g_size_measurement_count; i++) {
        mini_fprintf(stdout, "%-28s | %8.1f | %8.1f | %8.1f | %7.1f | %8.1f\n",
               g_size_measurements[i].name,
               g_size_measurements[i].size_bytes / 1024.0,
               g_size_measurements[i].text_size / 1024.0,
               g_size_measurements[i].data_size / 1024.0,
               g_size_measurements[i].bss_size / 1024.0,
               g_size_measurements[i].total_size / 1024.0);
    }
    
    // Calculate totals
    uint64_t total_size = 0;
    for (int i = 0; i < g_size_measurement_count; i++) {
        total_size += g_size_measurements[i].size_bytes;
    }
    
    mini_fprintf(stdout, "-----------------------------|-----------|-----------|-----------|----------|-----------\n");
    mini_fprintf(stdout, "%-28s | %8.1f | %8.1f | %8.1f | %7.1f | %8.1f\n",
           "TOTAL",
           total_size / 1024.0,
           total_size * 0.7 / 1024.0,
           total_size * 0.2 / 1024.0,
           total_size * 0.1 / 1024.0,
           total_size / 1024.0);
    
    mini_fprintf(stdout, "\nSize Distribution:\n");
    for (int i = 0; i < g_size_measurement_count; i++) {
        double percentage = (double)g_size_measurements[i].size_bytes / total_size * 100.0;
        mini_fprintf(stdout, "  %-20s: %6.1f%% (%lu bytes)\n",
               g_size_measurements[i].name, percentage, (unsigned long)g_size_measurements[i].size_bytes);
    }
}

void detect_size_regressions(void) {
    mini_fprintf(stdout, "\n=== Size Regression Detection ===\n");
    
    // Read previous measurements from CSV
    FILE *file = fopen("executable_size_history.csv", "r");
    if (!file) {
        mini_fprintf(stdout, "No previous size measurements found. Creating baseline.\n");
        return;
    }
    
    char line[256];
    ExecutableSizeInfo previous[10];
    int prev_count = 0;
    
    // Skip header
    if (fgets(line, sizeof(line), file)) {
        while (fgets(line, sizeof(line), file) && prev_count < 10) {
            char *token = strtok(line, ",");
            if (token) {
                strncpy(previous[prev_count].timestamp, token, sizeof(previous[prev_count].timestamp) - 1);
                
                token = strtok(NULL, ",");
                if (token) {
                    strncpy(previous[prev_count].name, token, sizeof(previous[prev_count].name) - 1);
                    
                    token = strtok(NULL, ",");
                    if (token) {
                        previous[prev_count].size_bytes = strtoull(token, NULL, 10);
                        prev_count++;
                    }
                }
            }
        }
    }
    fclose(file);
    
    if (prev_count == 0) {
        mini_fprintf(stdout, "No previous measurements found. Creating baseline.\n");
        return;
    }
    
    // Compare with current measurements
    int regressions = 0;
    int improvements = 0;
    
    for (int i = 0; i < g_size_measurement_count; i++) {
        for (int j = 0; j < prev_count; j++) {
            if (strcmp(g_size_measurements[i].name, previous[j].name) == 0) {
                int64_t size_change = (int64_t)g_size_measurements[i].size_bytes - (int64_t)previous[j].size_bytes;
                double change_percent = (double)size_change / previous[j].size_bytes * 100.0;
                
                if (change_percent > 5.0) {
                    mini_fprintf(stdout, "⚠️  REGRESSION: %s increased by %.1f%% (+%ld bytes)\n",
                           g_size_measurements[i].name, change_percent, size_change);
                    regressions++;
                } else if (change_percent < -5.0) {
                    mini_fprintf(stdout, "✅ IMPROVEMENT: %s decreased by %.1f%% (%ld bytes)\n",
                           g_size_measurements[i].name, -change_percent, -size_change);
                    improvements++;
                } else {
                    mini_fprintf(stdout, "📊 STABLE: %s changed by %.1f%% (%ld bytes)\n",
                           g_size_measurements[i].name, change_percent, size_change);
                }
                break;
            }
        }
    }
    
    mini_fprintf(stdout, "\nSize Change Summary:\n");
    mini_fprintf(stdout, "  Regressions: %d\n", regressions);
    mini_fprintf(stdout, "  Improvements: %d\n", improvements);
    mini_fprintf(stdout, "  Stable: %d\n", g_size_measurement_count - regressions - improvements);
    
    if (regressions > 0) {
        mini_fprintf(stdout, "\n⚠️  WARNING: %d executable size regression(s) detected!\n", regressions);
        mini_fprintf(stdout, "   Consider investigating recent changes that increased binary size.\n");
    }
}
int main() { measure_executable_sizes(); print_size_analysis(); return 0; }
