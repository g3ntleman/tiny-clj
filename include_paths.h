/*
 * Common Include Paths Configuration
 * 
 * This file defines common include paths and compiler definitions
 * to ensure consistent compilation across different tools (CMake, Linter, IDE).
 */

#ifndef INCLUDE_PATHS_H
#define INCLUDE_PATHS_H

// Common compiler definitions
#ifndef ENABLE_MEMORY_PROFILING
#define ENABLE_MEMORY_PROFILING
#endif

#ifndef DEBUG
#define DEBUG
#endif

#ifndef ENABLE_META
#define ENABLE_META
#endif

// Include path hints for IDEs and linters
// These are not actual includes, just documentation for tools

// Unity Framework
// #include "unity.h"  // Located in external/unity/src/unity.h

// Core Tiny-CLJ headers
// #include "object.h"     // Located in src/object.h
// #include "memory.h"     // Located in src/memory.h
// #include "value.h"      // Located in src/value.h
// #include "types.h"      // Located in src/types.h
// #include "memory_profiler.h"  // Located in src/memory_profiler.h

#endif // INCLUDE_PATHS_H
