#include <string.h>
#include <stdlib.h>
#include "runtime.h"

#define MAX_BUILTINS 64

typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinEntry;

static BuiltinEntry g_builtins[MAX_BUILTINS];
static int g_builtin_count = 0;

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
