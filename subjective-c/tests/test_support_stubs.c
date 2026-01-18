#include <stdbool.h>
#include "runtime.h"
#include "strings.h"

// Provide a zero-initialized runtime instance so Subjective-C can link
TinyClJRuntime g_runtime = {0};
