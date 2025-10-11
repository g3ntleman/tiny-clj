# Development Ideas for Tiny-CLJ

## AST Serialization for STM32 Deployment

### Problem
Clojure source code is too large for STM32 flash memory. Traditional approaches like source code compression have significant overhead.

### Solution: AST Serialization with String Pooling

#### Core Concept
- **Pre-compile Clojure to AST** at build time
- **Serialize AST to binary format** for embedded storage
- **Use string pooling** to eliminate string duplication
- **Store strings in flash** with AST references to string indices

#### Benefits
- **60-80% memory savings** compared to source code
- **20x faster execution** (no parsing overhead)
- **Type-safe execution** with pre-validated AST
- **No runtime compression** (avoids RAM overhead)

### Implementation Strategy

#### 1. String Pool in Flash
```c
// String pool in code segment
__attribute__((section(".clojure_strings")))
const char string_pool[] = 
    "defn\0factorial\0n\0if\0<=\0*\0dec\0+\0-\0=\0";

// String offset table
const uint16_t string_offsets[] = {
    0,   // "defn"
    5,   // "factorial" 
    15,  // "n"
    17,  // "if"
    20,  // "<="
    23,  // "*"
    25,  // "dec"
    29,  // "+"
    31,  // "-"
    34   // "="
};
```

#### 2. Compact AST Representation
```c
// Minimal AST nodes with string references
typedef struct {
    uint8_t type : 4;        // 4-bit CljObject type
    uint8_t string_id : 4;   // 4-bit string index
    uint16_t children[];     // Child node indices
} CompactASTNode;

// Serialized AST in flash
__attribute__((section(".clojure_ast")))
const CompactASTNode ast_nodes[] = {
    {CLJ_LIST, 0, {1, 2, 3}},     // defn -> [factorial, [n], body]
    {CLJ_SYMBOL, 1, {}},          // factorial
    {CLJ_LIST, 2, {4}},           // [n]
    {CLJ_SYMBOL, 2, {}},          // n
    // ...
};
```

#### 3. Runtime String Access
```c
// String lookup at runtime
const char* get_string_by_index(uint8_t index) {
    if (index >= sizeof(string_offsets)/sizeof(string_offsets[0])) {
        return NULL;
    }
    return &string_pool[string_offsets[index]];
}

// Create CljSymbol from string index
CljSymbol* create_symbol_from_index(uint8_t index) {
    const char* str = get_string_by_index(index);
    return create_symbol(str);
}
```

### Build-Time Integration

#### 1. Clojure-to-AST Compiler
```python
# clojure-to-ast.py
def compile_clojure_to_ast(clojure_file):
    # 1. Parse Clojure source
    ast = parse_clojure_file(clojure_file)
    
    # 2. Extract all strings
    strings = extract_all_strings(ast)
    
    # 3. Create string pool
    string_pool = create_string_pool(strings)
    
    # 4. Serialize AST with string references
    binary_ast = serialize_ast_to_binary(ast, string_pool)
    
    # 5. Generate C code
    return generate_c_ast_code(binary_ast, string_pool)
```

#### 2. CMake Integration
```cmake
# CMakeLists.txt
add_custom_command(
    OUTPUT embedded_ast.c
    COMMAND python3 clojure-to-ast.py ${CMAKE_SOURCE_DIR}/app.clj > embedded_ast.c
    DEPENDS app.clj
)

add_executable(tiny-clj-stm32-main 
    src/main_stm32.c
    embedded_ast.c
    # ... other sources
)
```

### Memory Savings Analysis

#### Before (Source Code)
```
Original Clojure:       200 bytes
String duplicates:       100 bytes
Total:                  300 bytes
```

#### After (AST + String Pool)
```
String pool:            35 bytes
String offsets:         20 bytes
AST nodes:              30 bytes
Total:                  85 bytes (72% savings)
```

### Trade-offs

#### Benefits
- ✅ **Massive memory savings** (60-80%)
- ✅ **Faster execution** (no parsing overhead)
- ✅ **Type-safe execution** (pre-validated AST)
- ✅ **No RAM overhead** (no decompression)

#### Drawbacks
- ❌ **Loss of source code references** in exceptions
- ❌ **Build-time complexity** (AST compilation)
- ❌ **Debugging difficulty** (AST nodes vs source lines)

### Exception Handling Strategy

#### Debug Build (with source mapping)
```c
typedef struct {
    uint8_t type;
    uint8_t string_id;
    uint16_t source_line;    // Original line number
    uint16_t source_col;     // Original column
    uint16_t children[];
} MappedASTNode;
```

#### Release Build (compact)
```c
typedef struct {
    uint8_t type;
    uint8_t string_id;
    uint16_t children[];
} CompactASTNode;
```

### Implementation Phases

#### Phase 1: Basic AST Serialization
- [ ] Implement AST-to-binary serializer
- [ ] Create string pool extraction
- [ ] Add CMake integration
- [ ] Test with simple Clojure programs

#### Phase 2: String Pool Optimization
- [ ] Implement string deduplication
- [ ] Add string offset table
- [ ] Optimize string access patterns
- [ ] Measure memory savings

#### Phase 3: Advanced Features
- [ ] Add source mapping for debug builds
- [ ] Implement multi-module AST support
- [ ] Add hot-swapping capabilities
- [ ] Create development tools

### Future Enhancements

#### 1. Multi-Module Support
```c
// Separate ASTs for different modules
const CompactASTNode* module_asts[] = {
    core_ast,      // Clojure core functions
    app_ast,       // Application code
    utils_ast      // Utility functions
};
```

#### 2. Hot-Swapping
```c
// Runtime AST updates
void update_embedded_ast(const uint8_t* new_ast) {
    // Flash update for new AST
    flash_write(0x08010000, new_ast, ast_size);
}
```

#### 3. Development Tools
- AST visualizer for debugging
- Source-to-AST mapping tools
- Memory usage analyzer
- Performance profiler

### Conclusion

AST serialization with string pooling is the optimal approach for STM32 deployment:

- **Massive memory savings** without compression overhead
- **Faster execution** with pre-compiled AST
- **Type-safe execution** with validated structures
- **Scalable** for larger Clojure programs

This approach makes tiny-clj viable for embedded development while maintaining Clojure's expressiveness and power.
