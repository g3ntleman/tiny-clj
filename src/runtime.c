#include <string.h>
#include <stdlib.h>
#include "runtime.h"
#include "namespace.h"
#include "symbol.h"
#include "meta.h"
#include "vector.h"
#include "value.h"

#define MAX_BUILTINS 64

typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinEntry;

static BuiltinEntry g_builtins[MAX_BUILTINS];
static int g_builtin_count = 0;

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

void register_builtin(const char *name, BuiltinFn fn) {
    if (!name || !fn) return;
    if (g_builtin_count >= MAX_BUILTINS) return;
    g_builtins[g_builtin_count].name = name;
    g_builtins[g_builtin_count].fn = fn;
    g_builtin_count++;
}

BuiltinFn find_builtin(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_builtin_count; ++i) {
        if (strcmp(g_builtins[i].name, name) == 0) {
            return g_builtins[i].fn;
        }
    }
    return NULL;
}
