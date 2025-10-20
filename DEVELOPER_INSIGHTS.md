# Tiny-Clj Developer Insights

## Evaluation System Overview

Tiny-Clj implements a complete Lisp evaluation system with the following key components:

### 1. Core Evaluation Flow

```
Input String → Parser → AST → Evaluator → Result
```

#### 1.1 Parser (`src/clj_parser.c`)
- **Input**: Clojure source code as string
- **Output**: Abstract Syntax Tree (AST) as `CljObject*`
- **Key Functions**:
  - `parse()`: Main entry point, handles string input
  - `parse_form()`: Recursive descent parser for individual forms
  - `parse_list()`: Handles parentheses `(form1 form2 ...)`
  - `parse_vector()`: Handles square brackets `[item1 item2 ...]`
  - `parse_map()`: Handles curly braces `{key1 val1 key2 val2}`
  - `parse_string()`: Handles string literals `"hello"`
  - `parse_number()`: Handles integers and floats
  - `parse_symbol()`: Handles symbols and keywords

#### 1.2 AST Structure
All AST nodes are `CljObject*` with different types:
- `CLJ_LIST`: `(form1 form2 ...)`
- `CLJ_VECTOR`: `[item1 item2 ...]`
- `CLJ_MAP`: `{key1 val1 key2 val2}`
- `CLJ_SYMBOL`: `symbol-name`
- `CLJ_STRING`: `"string"`
- `CLJ_INT`: `42`
- `CLJ_FLOAT`: `3.14`
- `CLJ_FUNC`: Function objects (native or Clojure)

### 2. Evaluation Engine

#### 2.1 Main Evaluator (`src/function_call.c`)

**Entry Point**: `eval_list(CljObject *list, CljObject *env, EvalState *st)`

The evaluator follows this strategy:

1. **Special Forms First**: Built-in operations like `if`, `def`, `fn`, `+`, `-`, etc.
2. **Function Calls**: Resolve symbol to function, evaluate arguments, call function
3. **Fallback**: Return the first element

```c
CljObject* eval_list(CljObject *list, CljObject *env, EvalState *st) {
    // 1. Extract operator (first element)
    CljObject *op = list_data->head;
    
    // 2. Check special forms
    if (sym_is(op, "if")) return eval_if(list, env, st);
    if (sym_is(op, "def")) return eval_def(list, env, st);
    if (sym_is(op, "fn")) return eval_fn(list, env);
    if (sym_is(op, "+")) return eval_add(list, env);
    // ... more special forms
    
    // 3. Function call fallback
    if (is_type(op, CLJ_SYMBOL)) {
        CljObject *fn = eval_symbol(op, st);  // Resolve symbol
        if (is_type(fn, CLJ_FUNC)) {
            // Evaluate arguments
            CljObject **args = alloc_obj_array(argc, args_stack);
            for (int i = 0; i < argc; i++) {
                args[i] = eval_arg(list, i + 1, env);
            }
            // Call function
            return eval_function_call(fn, args, argc, env);
        }
    }
}
```

#### 2.2 Function Call System

**Two Types of Functions**:

1. **Native Functions** (`CljFunc`): C functions with function pointer
2. **Clojure Functions** (`CljFunction`): User-defined functions with parameters and body

```c
CljObject* eval_function_call(CljObject *fn, CljObject **args, int argc, CljObject *env) {
    // Check if it's a native function (CljFunc)
    CljFunc *native_func = (CljFunc*)fn;
    if (native_func && native_func->fn) {
        return native_func->fn(args, argc);  // Direct C call
    }
    
    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    // Parameter binding and body evaluation
    return eval_body_with_params(func->body, func->params, args, argc, func->closure_env);
}
```

#### 2.3 Parameter Binding

For Clojure functions, parameters are bound to argument values:

```c
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    if (is_type(body, CLJ_SYMBOL)) {
        // Check if symbol matches a parameter
        for (int i = 0; i < param_count; i++) {
            if (params[i] && is_type(params[i], CLJ_SYMBOL) && is_type(body, CLJ_SYMBOL)) {
                CljSymbol *param_sym = as_symbol(params[i]);
                CljSymbol *body_sym = as_symbol(body);
                if (strcmp(param_sym->name, body_sym->name) == 0) {
                    return values[i];  // Return bound value
                }
            }
        }
        // Try to resolve from closure environment
        if (closure_env && is_type(closure_env, CLJ_MAP)) {
            return map_get(closure_env, body);
        }
    }
    // ... handle other body types
}
```

### 3. Namespace System

#### 3.1 Namespace Structure (`src/namespace.c`)

```c
typedef struct CljNamespace {
    CljObject *name;          // e.g., 'user', 'clojure.core'
    CljObject *mappings;      // Map: Symbol → CljObject (definitions)
    const char *filename;     // optional: associated file
    struct CljNamespace *next;
} CljNamespace;
```

#### 3.2 Symbol Resolution

```c
CljObject* ns_resolve(EvalState *st, CljObject *sym) {
    // 1. Search current namespace
    CljObject *v = map_get(st->current_ns->mappings, sym);
    if (v) return v;
    
    // 2. Search global namespaces (e.g., clojure.core)
    CljNamespace *cur = ns_registry;
    while (cur) {
        v = map_get(cur->mappings, sym);
        if (v) return v;
        cur = cur->next;
    }
    return NULL;
}
```

#### 3.3 Symbol Definition

```c
void ns_define(EvalState *st, CljObject *symbol, CljObject *value) {
    // Get current namespace
    CljNamespace *ns = st->current_ns;
    if (!ns) {
        ns = ns_get_or_create("user", NULL);
        st->current_ns = ns;
    }
    
    // Store symbol-value binding
    map_assoc(ns->mappings, symbol, value);
}
```

### 4. Memory Management

#### 4.1 Reference Counting

All `CljObject*` use reference counting:
- `RETAIN(obj)`: Increment reference count
- `RELEASE(obj)`: Decrement reference count, free when count reaches 0
- `AUTORELEASE(obj)`: Add to autorelease pool for automatic cleanup

#### 4.2 Autorelease Pools

```c
CLJVALUE_POOL_SCOPE(pool) {
    // Objects created here are automatically released
    CljObject *result = eval_list(ast, env, st);
    return result;  // Pool is popped, objects released
}
```

### 5. Exception Handling

#### 5.1 TRY/CATCH System

```c
TRY {
    CljObject *result = eval_list(ast, env, st);
    return result;
} CATCH(ex) {
    // Handle exception
    print_exception(ex);
    return NULL;
} END_TRY
```

#### 5.2 Exception Propagation

Exceptions are thrown with `throw_exception_formatted()` and caught at the REPL level:

```c
// In REPL
TRY {
    CljObject *res = eval_list(ast, env, st);
    if (res) print_result(res);
} CATCH(ex) {
    print_exception(ex);
} END_TRY
```

### 6. Special Forms

#### 6.1 Control Flow

- **`if`**: `(if condition then else?)`
- **`def`**: `(def symbol value)` - Define symbol in current namespace
- **`fn`**: `(fn [params] body)` - Create function
- **`let`**: `(let [bindings] body)` - Local bindings

#### 6.2 Arithmetic

- **`+`**: `(+ a b ...)` - Addition
- **`-`**: `(- a b ...)` - Subtraction  
- **`*`**: `(* a b ...)` - Multiplication
- **`/`**: `(/ a b ...)` - Division

#### 6.3 Comparison

- **`=`**: `(= a b)` - Equality
- **`<`**: `(< a b)` - Less than
- **`>`**: `(> a b)` - Greater than

### 7. REPL (Read-Eval-Print Loop)

#### 7.1 Main Loop (`src/repl.c`)

```c
int main(int argc, char **argv) {
    // Initialize platform and evaluation state
    platform_init();
    EvalState *st = evalstate_new();
    load_clojure_core(st);
    register_builtins();
    
    // Handle command line arguments
    if (file_arg) {
        // Load and evaluate file
    }
    
    if (eval_args) {
        // Evaluate -e expressions
        for (int i = 0; i < eval_count; i++) {
            eval_string_repl(eval_args[i], st);
        }
    }
    
    // Interactive REPL
    for (;;) {
        char *input = readline();
        if (is_balanced_form(input)) {
            eval_string_repl(input, st);
        }
    }
}
```

#### 7.2 Evaluation with Exception Handling

```c
static int eval_string_repl(const char *code, EvalState *st) {
    CljObject *ast = parse(code, st);
    if (!ast) return 0;
    
    TRY {
        CljObject *res = NULL;
        if (is_type(ast, CLJ_LIST)) {
            CljObject *env = st->current_ns->mappings;
            res = eval_list(ast, env, st);
        } else {
            res = eval_expr_simple(ast, st);
        }
        if (res) print_result(res);
        return 1;
    } CATCH(ex) {
        print_exception(ex);
        return 0;
    } END_TRY
}
```

### 8. Clojure Core Functions

#### 8.1 Core Library (`src/clojure.core.clj`)

Defines essential Clojure functions:

```clojure
; Arithmetic
(def add (fn [a b] (+ a b)))
(def mod (fn [a b] (- a (* b (/ a b)))))

; Predicates  
(def even? (fn [x] (= (mod x 2) 0)))
(def odd? (fn [x] (not (even? x))))

; Collections
(def map (fn [f coll]
  (if (empty? coll)
    '()
    (cons (f (first coll)) (map f (rest coll))))))
```

#### 8.2 Loading Process

```c
void load_clojure_core(EvalState *st) {
    const char *core_source = clojure_core_source();
    eval_core_source(core_source, st);
}
```

### 9. Debugging and Development

#### 9.1 Debug Prints

Use `DEBUG_PRINTF` for conditional debug output:

```c
#ifdef DEBUG
    #define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINTF(fmt, ...) ((void)0)
#endif
```

#### 9.2 Memory Profiling

```c
WITH_MEMORY_PROFILING {
    // Code to profile
    CljObject *result = eval_list(ast, env, st);
}
```

### 10. Common Issues and Solutions

#### 10.1 Symbol Resolution

**Problem**: Symbol not found
**Solution**: Check namespace mappings and symbol interning

#### 10.2 Memory Leaks

**Problem**: Objects not released
**Solution**: Use `AUTORELEASE` and proper `RELEASE` calls

#### 10.3 Function Calls

**Problem**: Clojure functions crash with parameters
**Solution**: Check `eval_body_with_params` and parameter binding

#### 10.4 Exception Handling

**Problem**: Exceptions not caught
**Solution**: Ensure `TRY/CATCH` blocks are properly nested

### 11. Performance Considerations

#### 11.1 Symbol Interning

Symbols are interned to avoid duplicate string comparisons:
```c
CljObject* intern_symbol(const char *ns, const char *name);
```

#### 11.2 Map Operations

Maps use structural equality for keys:
```c
bool clj_equal(CljObject *a, CljObject *b);
```

#### 11.3 Memory Optimization

- Use `AUTORELEASE` for temporary objects
- Implement proper reference counting
- Clean up autorelease pools

### 12. Testing

#### 12.1 Unit Tests

```c
static char *test_function_call(void) {
    CljObject *func = make_named_func(native_if, NULL, "if");
    CljObject *args[3] = {make_int(1), make_int(42), make_int(0)};
    CljObject *result = eval_function_call(func, args, 3, NULL);
    
    mu_assert("Function call should work", result != NULL);
    mu_assert("Should return 42", is_type(result, CLJ_INT) && result->as.i == 42);
    
    RELEASE(func);
    RELEASE(result);
    return 0;
}
```

#### 12.2 Integration Tests

```bash
./tiny-clj-repl -e "(def test-func (fn [x] (+ x 1)))" -e "(test-func 5)"
```

### 13. Testing with Clojure Command Line Tool

#### 13.1 Using `clj` for Behavior Testing

Das `clj` Commandline-Tool kann verwendet werden, um Clojure-Verhalten zu testen und als Referenz für die Implementierung zu dienen:

```bash
# Starte eine REPL
clj

# Führe spezifische Ausdrücke aus
clj -e "(+ 1 2 3)"
clj -e "(def test-fn (fn [x] (* x 2)))" -e "(test-fn 5)"

# Lade eine Datei
clj -i script.clj

# Kombiniere mehrere Optionen
clj -e "(def x 42)" -e "(println x)" -e "(+ x 1)"
```

#### 13.2 Wichtige `clj` Optionen

- `-e, --eval string`: Führe Ausdrücke aus
- `-i, --init path`: Lade eine Datei oder Resource
- `-m, --main ns-name`: Rufe die -main Funktion auf
- `-r, --repl`: Starte eine REPL
- `-Jopt`: JVM-Optionen übergeben (z.B. `-J-Xmx512m`)

#### 13.3 Verwendung für tiny-clj Entwicklung

Das `clj` Tool ist nützlich für:
- **Verhaltenstests**: Vergleiche tiny-clj Ausgabe mit Standard Clojure
- **Syntax-Validierung**: Teste Clojure-Syntax vor der Implementierung
- **Performance-Vergleiche**: Benchmark gegen Standard Clojure
- **Feature-Referenz**: Verstehe erwartetes Verhalten von Clojure-Funktionen

Beispiel für Verhaltenstest:
```bash
# Standard Clojure
clj -e "(map inc [1 2 3 4])"
# Erwartete Ausgabe: (2 3 4 5)

# tiny-clj sollte dasselbe Verhalten zeigen
./tiny-clj-repl -e "(map inc [1 2 3 4])"
```

### 14. Error Handling and Validation Patterns

#### 14.1 Assert vs NULL Checks - Best Practices

Tiny-Clj follows a strict pattern for error handling that distinguishes between **API misuse** (programming errors) and **legitimate NULL values** (runtime conditions):

##### **Use `assert()` for API Misuse (Programming Errors)**

`assert()` should be used when a condition should **never** be true in correct code:

```c
// Environment should never be NULL when expected
assert(env != NULL);

// Function pointer should never be NULL for valid native functions
assert(native_func != NULL);
assert(native_func->fn != NULL);

// Type assertions - these should never fail if types are checked first
CljSymbol *sym = as_symbol(obj);  // as_symbol() is a type assertion
// No assert() needed - as_symbol() handles type validation internally
```

**When to use `assert()`:**
- Parameter validation for API misuse
- Type assertions (after type checking)
- Internal consistency checks
- Conditions that indicate programming errors

##### **Use NULL Checks for Legitimate Runtime Conditions**

Normal NULL checks should be used when NULL is a **valid runtime condition**:

```c
// Parameters can legitimately be NULL when param_count == 0
if (param_count > 0) {
    if (!params || !values) return NULL;
}

// Function results can be NULL
CljObject *result = eval_symbol(symbol, st);
if (!result) return NULL;

// Array elements can be NULL
for (int i = 0; i < count; i++) {
    if (args[i]) {
        // Process non-NULL argument
    }
}
```

**When to use NULL checks:**
- Function return values
- Array elements that can be NULL
- Optional parameters
- Runtime conditions that can legitimately be NULL

##### **Anti-Patterns to Avoid**

```c
// WRONG: Using assert() for legitimate NULL values
assert(params != NULL);        // params can be NULL when param_count == 0
assert(values != NULL);       // values can be NULL when param_count == 0
assert(result != NULL);       // result can legitimately be NULL

// WRONG: Using assert() after type assertions
CljSymbol *sym = as_symbol(obj);
assert(sym != NULL);          // as_symbol() handles type validation internally

// CORRECT: Use normal NULL checks for legitimate conditions
if (param_count > 0 && (!params || !values)) return NULL;
CljObject *result = eval_symbol(symbol, st);
if (!result) return NULL;
```

##### **Type Assertion Functions**

Functions like `as_symbol()`, `as_list()`, `as_map()` are **type assertions** and should never return NULL if called with correct types:

```c
// These functions validate types internally and abort on type mismatch
static inline CljSymbol* as_symbol(CljObject *obj) {
    return (CljSymbol*)assert_type(obj, CLJ_SYMBOL);
}

// Usage - no additional NULL checks needed
if (is_type(obj, CLJ_SYMBOL)) {
    CljSymbol *sym = as_symbol(obj);  // Type already verified
    // sym is guaranteed to be non-NULL
    printf("Symbol name: %s\n", sym->name);
}
```

##### **Environment Parameter Pattern**

Environment parameters should never be NULL when expected (API misuse):

```c
CljObject* eval_function(CljObject *list, CljMap *env, EvalState *st) {
    // Environment is required - this is API misuse if NULL
    assert(env != NULL);
    
    // Rest of function implementation
}
```

##### **Parameter Array Pattern**

Parameter arrays can legitimately be NULL when count is 0:

```c
CljObject* eval_body_with_params(CljObject *body, CljObject **params, 
                                 CljObject **values, int param_count, 
                                 CljObject *closure_env) {
    if (!body) return NULL;
    
    // Check arrays only when they should exist
    if (param_count > 0) {
        if (!params || !values) return NULL;
    }
    
    // Process parameters...
}
```

#### 14.2 Memory Management Patterns

##### **Reference Counting**

```c
// Always retain objects that will be stored
CljObject *result = eval_list(ast, env, st);
if (result) {
    RETAIN(result);  // Increment reference count
    // Store result somewhere
}

// Always release objects when done
RELEASE(result);  // Decrement reference count
```

##### **Autorelease Pools**

```c
// Use autorelease for temporary objects
CljObject *temp = AUTORELEASE(make_string("temporary"));
// Object will be automatically released when pool is popped
```

#### 14.3 Exception Handling Patterns

##### **TRY/CATCH Blocks**

```c
TRY {
    CljObject *result = eval_list(ast, env, st);
    if (result) {
        print_result(result);
        RELEASE(result);
    }
} CATCH(ex) {
    print_exception(ex);
    // Exception is automatically cleaned up
} END_TRY
```

##### **Exception Propagation**

```c
// Throw exceptions for error conditions
if (!symbol) {
    throw_exception_formatted("NameError", __FILE__, __LINE__, 0,
                             "Symbol cannot be NULL");
    return NULL;  // Unreachable, but prevents fallthrough
}
```

### 15. Architecture Summary

The evaluation system follows a clean separation of concerns:

1. **Parser**: String → AST
2. **Evaluator**: AST → Value  
3. **Namespace**: Symbol resolution and definition
4. **Memory**: Reference counting and cleanup
5. **REPL**: User interaction and exception handling
6. **Error Handling**: Proper distinction between API misuse and runtime conditions

This design allows for:
- Easy extension with new special forms
- Clean separation between native and Clojure functions
- Proper memory management
- Robust exception handling
- Interactive development experience
- Clear error handling patterns that distinguish programming errors from runtime conditions
