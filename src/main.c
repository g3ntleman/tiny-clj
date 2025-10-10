#include "platform.h"
#include "object.h"
#include "exception.h"
#include "clj_parser.h"
#include "namespace.h"
#include "clj_symbols.h"
#include "runtime.h"
#include "tests/test_api.h"
#include <stdio.h>
#include <stdlib.h>

static int run_all_tests_main(void) {
    platform_print("[main] Running unit tests...");
    if (run_unit_tests() != 0) return -1;
    platform_print("[main] Running integration tests...");
    if (run_integration_tests() != 0) return -1;
    platform_print("[main] Running parser tests...");
    if (run_parser_tests() != 0) return -1;
    platform_print("[main] All tests passed.");
    return 0;
}

int main() {
    platform_init();
    // 1) Run all tests first; abort if any fail
    if (run_all_tests_main() != 0) {
        return 1;
    }
    
    const char *name = platform_name();
    char message[128];
    snprintf(message, sizeof(message), "Hello from %s! (Final Optimized Version)", name);
    platform_print(message);
    
    // Initialize global structures
    meta_registry_init(); // Enable meta functionality
    // Singletons are automatically initialized on first call
    
    // Demo output removed; everything should be covered by unit tests
    platform_print("=== Tiny-Clj started (tests passed) ===");
    
    // No demo content here – rely on tests
    
    // Cleanup
    meta_registry_cleanup(); // Cleanup meta functionality
    autorelease_pool_cleanup_all(); // Cleanup alle Autorelease-Pools
    
    return 0;
}
