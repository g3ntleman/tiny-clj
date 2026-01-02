/*
 * Common Include Paths Configuration
 * 
 * This file defines common include paths and compiler definitions
 * to ensure consistent compilation across different tools (CMake, Linter, IDE).
 */

#ifndef INCLUDE_PATHS_H
#define INCLUDE_PATHS_H

// Common compiler definitions
#ifndef MEMORY_PROFILING_ENABLED
#define MEMORY_PROFILING_ENABLED 1
#endif

#ifndef DEBUG
#define DEBUG
#endif

#ifndef META_ENABLED
#define META_ENABLED 1
#endif

// Include path hints for IDEs and linters
// These are not actual includes, just documentation for tools

// Unity Framework
// #include "unity.h"  // Located in external/unity/src/unity.h

// Core Tiny-CLJ headers
// #include <subjective-c/object.h>     // Located in subjective-c/object.h
// #include "memory.h"     // Located in src/memory.h
// #include "value.h"      // Located in src/value.h
// #include "types.h"      // Located in src/types.h
// #include "memory_profiler.h"  // Located in src/memory_profiler.h

#endif // INCLUDE_PATHS_H
