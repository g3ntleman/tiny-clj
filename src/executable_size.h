#ifndef EXECUTABLE_SIZE_H
#define EXECUTABLE_SIZE_H

#include <stdint.h>

// Executable size measurement structure
typedef struct {
    char name[64];
    uint64_t size_bytes;
    uint64_t text_size;
    uint64_t data_size;
    uint64_t bss_size;
    uint64_t total_size;
    char timestamp[32];
} ExecutableSizeInfo;

// Function declarations
uint64_t get_executable_size(const char *executable_path);
void measure_executable_sizes(void);
void export_size_history_csv(void);
void print_size_analysis(void);
void detect_size_regressions(void);

// Global size tracking
extern ExecutableSizeInfo g_size_measurements[10];
extern int g_size_measurement_count;

#endif // EXECUTABLE_SIZE_H
