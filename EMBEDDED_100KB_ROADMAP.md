# Embedded 100KB Target Roadmap

## Current Status
- **Current Size**: 198KB (tiny-clj-stm32)
- **Target**: <100KB for embedded deployment
- **Gap**: ~98KB reduction needed

## Size Analysis
The current 198KB includes:
- Full Clojure runtime
- All built-in functions
- Complete parser
- Memory management
- Error handling
- Debug symbols

## 100KB Target Strategy

### Phase 1: Compiler Optimizations (Expected: -20KB)
- [x] Aggressive LTO (-flto)
- [x] Function/data sections (-ffunction-sections -fdata-sections)
- [x] Dead code elimination (-Wl,--gc-sections)
- [x] Strip symbols (-Wl,--strip-all)
- [x] Remove unwind tables (-fno-unwind-tables)

### Phase 2: Feature Reduction (Expected: -40KB)
- [ ] **Minimal Parser**: Remove complex parsing features
- [ ] **Core Functions Only**: Keep only essential built-ins
- [ ] **No Error Messages**: Remove error_messages.c (~5KB)
- [ ] **No Memory Profiler**: Remove memory_profiler.c (~10KB)
- [ ] **Minimal String Operations**: Basic string handling only
- [ ] **No Debug Support**: Remove all debug functionality

### Phase 3: Architecture Optimization (Expected: -30KB)
- [ ] **32-bit Xtensa**: Use ESP32 Xtensa toolchain
- [ ] **Long Calls**: -mlongcalls for ESP32
- [ ] **Optimized Data Types**: Use smaller integer types where possible
- [ ] **Static Allocation**: Pre-allocate fixed-size buffers

### Phase 4: Runtime Reduction (Expected: -8KB)
- [ ] **Minimal REPL**: Remove interactive features
- [ ] **No Line Editing**: Remove readline support
- [ ] **Basic I/O**: Minimal printf/scanf only
- [ ] **Fixed Stack**: Pre-allocated evaluation stack

## Implementation Plan

### Step 1: Create Minimal Embedded Target
```cmake
# New target: tiny-clj-minimal
add_executable(tiny-clj-minimal
    src/main_stm32.c
    src/platform_stm32_embedded.c
    src/clojure_core.c
    src/types.c
    src/object.c
    # Only essential sources
)
```

### Step 2: Conditional Compilation
```c
#ifdef EMBEDDED_MINIMAL
    // Minimal feature set
    #define DISABLE_ERROR_MESSAGES
    #define DISABLE_MEMORY_PROFILER
    #define DISABLE_DEBUG_SYMBOLS
    #define DISABLE_STRING_FORMATTING
    #define DISABLE_COMPLEX_PARSING
#endif
```

### Step 3: ESP32 Xtensa Toolchain
- Use ESP32 GCC toolchain for 32-bit embedded
- Target: Xtensa LX6 (ESP32)
- Optimize for ESP32 instruction set

## Expected Results
- **Phase 1**: 198KB → 178KB (-20KB)
- **Phase 2**: 178KB → 138KB (-40KB)  
- **Phase 3**: 138KB → 108KB (-30KB)
- **Phase 4**: 108KB → 100KB (-8KB)

## Success Criteria
- [ ] Binary size < 100KB
- [ ] Core Clojure functionality preserved
- [ ] Tail call optimization (recur) working
- [ ] Basic arithmetic operations
- [ ] Function definitions and calls
- [ ] List/vector operations

## Notes
- The 100KB target is aggressive but achievable
- Some Clojure features may need to be removed
- Focus on embedded use cases (no interactive REPL)
- Prioritize core language features over convenience functions
