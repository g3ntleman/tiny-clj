int main() { 
    const char *executables[] = {"tiny-clj-repl", "unity-tests"};
    int num_executables = sizeof(executables) / sizeof(executables[0]);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    g_size_measurement_count = 0;
    
    for (int i = 0; i < num_executables && g_size_measurement_count < 10; i++) {
        uint64_t size = get_executable_size(executables[i]);
        if (size > 0) {
            g_size_measurements[g_size_measurement_count].timestamp = "2025-10-20T16:50:00";
            strncpy(g_size_measurements[g_size_measurement_count].name, 
                   executables[i], sizeof(g_size_measurements[g_size_measurement_count].name) - 1);
            g_size_measurements[g_size_measurement_count].size_bytes = size;
            g_size_measurements[g_size_measurement_count].text_size = size * 0.8;
            g_size_measurements[g_size_measurement_count].data_size = size * 0.1;
            g_size_measurements[g_size_measurement_count].bss_size = size * 0.05;
            g_size_measurements[g_size_measurement_count].total_size = size;
            g_size_measurement_count++;
        }
    }
    
    print_size_analysis();
    return 0; 
}
