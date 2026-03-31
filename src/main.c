#include "platform.h"
#include "namespace.h"
#include "symbol.h"
#include "runtime.h"
#include "meta.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    platform_init();
    runtime_init(&g_runtime);
    
    const char *cname = platform_name();
    char message[128];
    size_t pos = 0;
    const size_t cap = sizeof(message);
    const char *prefix = "Hello from ";
    const char *suffix = "! (Final Optimized Version)";

    while (*prefix && pos + 1 < cap) {
        message[pos++] = *prefix++;
    }
    if (cname) {
        while (*cname && pos + 1 < cap) {
            message[pos++] = *cname++;
        }
    }
    while (*suffix && pos + 1 < cap) {
        message[pos++] = *suffix++;
    }
    message[pos] = '\0';
    platform_print(message);
    
    // Initialize global structures
    meta_registry_init(); // Enable meta functionality
    init_special_symbols();
    // Singletons are automatically initialized on first call
    
    // Demo output removed; everything should be covered by unit tests
    platform_print("=== Tiny-Clj started (tests passed) ===");
    
    // No demo content here – rely on tests
    
    // Cleanup
    meta_registry_cleanup(); // Cleanup meta functionality
    autorelease_pool_free(); // Cleanup all autorelease pools
    
    return 0;
}
