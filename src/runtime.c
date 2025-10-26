#include <string.h>
#include <stdlib.h>
#include "runtime.h"
#include "namespace.h"
#include "symbol.h"
#include "meta.h"
#include "vector.h"
#include "value.h"

// Statisch alloziertes globales Runtime-Struct
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .clojure_core_cache = NULL,
    .symbol_table = NULL,
    .meta_registry = NULL,
    .pool_stack = {NULL},
    .pool_stack_top = -1,
    .builtins_registered = false
};

void runtime_init(void) {
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));
    g_runtime.pool_stack_top = -1;
    g_runtime.builtins_registered = false;
}

void runtime_free(void) {
    // Cleanup in korrekter Reihenfolge
    // Pools werden automatisch beim nächsten Test geleert
    symbol_table_cleanup();
    meta_registry_cleanup();
    ns_cleanup();
    
    // Reset Runtime (statisch alloziert, bleibt bestehen)
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));
    g_runtime.pool_stack_top = -1;
}

// Legacy functions removed - all builtins now use namespace registration
