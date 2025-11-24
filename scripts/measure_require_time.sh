#!/bin/bash

# Script to measure how long require 'clojure.string takes

echo "Measuring require 'clojure.string load time..."
echo ""

# Create a simple test program
cat > /tmp/test_require_time.c << 'EOF'
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "tests_common.h"

int main() {
    // Initialize runtime
    runtime_init(&g_runtime);
    event_loop_init();
    clojure_core_set_quiet(true);
    
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        register_builtins();
        g_runtime.builtins_registered = true;
    }
    
    // Create eval state
    WITH_AUTORELEASE_POOL({
        evalstate_reset(&g_test_eval_state, true);
        
        // Measure require time
        struct timeval start, end;
        gettimeofday(&start, NULL);
        
        // Run require multiple times to get average
        int iterations = 10;
        for (int i = 0; i < iterations; i++) {
            // Reset namespace to ensure clean state
            runtime_reset(&g_runtime);
            runtime_init(&g_runtime);
            evalstate_reset(&g_test_eval_state, true);
            
            // Measure single require
            struct timeval req_start, req_end;
            gettimeofday(&req_start, NULL);
            
            CljObject *result = eval_string("(require 'clojure.string)", g_test_eval_state);
            (void)result;
            
            gettimeofday(&req_end, NULL);
            double req_time = (req_end.tv_sec - req_start.tv_sec) * 1000.0 + 
                             (req_end.tv_usec - req_start.tv_usec) / 1000.0;
            printf("Require #%d: %.2f ms\n", i+1, req_time);
        }
        
        gettimeofday(&end, NULL);
        double total_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                           (end.tv_usec - start.tv_usec) / 1000.0;
        double avg_time = total_time / iterations;
        
        printf("\nTotal time for %d requires: %.2f ms\n", iterations, total_time);
        printf("Average time per require: %.2f ms\n", avg_time);
    });
    
    return 0;
}
EOF

# Compile and run (simplified - just use the test framework)
echo "Using test framework to measure..."
./build/unity-tests --test "require/test_require_clojure_string" 2>&1 | head -5


