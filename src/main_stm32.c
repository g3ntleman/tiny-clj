#include "platform.h"
#include "object.h"
#include "exception.h"
#include "clj_parser.h"
#include "namespace.h"
#include "clj_symbols.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

// STM32-optimized main - no test code, minimal size
int main() {
    platform_init();
    
    char message[64];  // Smaller buffer for STM32
    snprintf(message, sizeof(message), "Tiny-Clj STM32 v1.0");
    platform_print(message);
    
    // Initialize only essential structures
    meta_registry_init();
    
    // Minimal startup message
    platform_print("Ready");
    
    // Cleanup
    meta_registry_cleanup();
    autorelease_pool_cleanup_all();
    
    return 0;
}
