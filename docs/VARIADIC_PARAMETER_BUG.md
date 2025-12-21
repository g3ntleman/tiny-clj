# Variadic Parameter Bug (`& rest`)

## Problem Summary

Variadic function parameters using `& rest` syntax do not work correctly. The `rest` parameter receives only the last argument instead of a list of all remaining arguments.

## Current Behavior vs Expected

```clojure
;; Test case 1: Basic variadic
((fn [a & rest] rest) 1 2 3)
;; CURRENT:  3           (single value - last arg only)
;; EXPECTED: (2 3)       (list of remaining args)

;; Test case 2: Many arguments
((fn [a & rest] rest) 1 2 3 4 5)
;; CURRENT:  ArityException
;; EXPECTED: (2 3 4 5)

;; Test case 3: Exactly matching required params
((fn [a & rest] rest) 1)
;; CURRENT:  ArityException  
;; EXPECTED: nil or ()

;; Test case 4: Count of rest args
((fn [a & rest] (count rest)) 1 2 3 4 5)
;; CURRENT:  ArityException
;; EXPECTED: 4
```

## Impact

This bug blocks:
1. **Removing C-based `defn`** - The `defn` macro needs `& body` to collect multiple body expressions
2. **Variadic macros** - Any macro using `& args` fails
3. **User-defined variadic functions** - Core Clojure pattern is broken

## Test Cases

Location: `src/tests/test_macros.c`

5 tests are marked as `TEST_IGNORE` that will pass when the bug is fixed:
- `test_variadic_fn_basic` - Basic `& rest` behavior
- `test_variadic_fn_empty_rest` - `& rest` with no extra args
- `test_variadic_fn_many_args` - `& rest` with many args
- `test_variadic_macro_basic` - Variadic macro `[& items]`
- `test_variadic_macro_with_body` - defn-style `[name params & body]`

## Relevant Code Locations

### 1. Function Structure (`src/function.h`)

```c
typedef struct {
    CljObject base;
    CljVector *params;  // Parameter vector - contains symbols and possibly &
    ID body;
    CljList *env_stack;
    const char *name;
    struct CljNamespace *ns;
} CljFunction;
```

The `params` vector stores the parameter symbols. The `&` symbol and rest parameter are stored as regular elements.

### 2. Function Creation (`src/function.c`)

```c
CljFunction* make_function(ID *params, int param_count, ID body, 
                           CljList *env_stack, const char *cname, 
                           struct CljNamespace *ns);
```

Question: Does this function handle `&` specially or just store it as-is?

### 3. Function Call & Parameter Binding (`src/eval.c`)

The main location where parameters are bound to arguments. Search for:
- `eval_function_call` - Main entry point
- Parameter binding logic
- Where `argc` (argument count) is validated

Key questions:
1. Where is arity checking done?
2. Where are parameters bound to values?
3. Is there any special handling for `&`?

### 4. Parser (`src/parser.c`)

The `&` symbol is parsed. Check:
- Is `&` parsed as a regular symbol?
- Is there any special handling during parsing?

### 5. Symbol Definition (`src/symbol.c`)

Check if there's a `SYM_AMP` or similar for the `&` symbol.

## Investigation Steps

1. **Trace parameter binding**: Add debug output to see how params are bound
   ```c
   // In eval_function_call or parameter binding:
   printf("param[%d] = %s, arg[%d] = ...\n", i, param_name, i);
   ```

2. **Check arity calculation**: The arity check fails with >expected args
   - Where is `param_count` calculated?
   - Does it account for `&`?

3. **Search for `&` handling**:
   ```bash
   grep -rn '"&"' src/*.c
   grep -rn "SYM_AMP\|amp" src/*.c
   grep -rn "variadic\|rest_param" src/*.c
   ```

4. **Compare with defn handling**: The C-based `defn` works with multiple body expressions. How does it handle the body list?

## Expected Fix Location

Most likely in one of these areas:

1. **Arity checking** - Must recognize `&` and adjust expected param count
2. **Parameter binding** - Must collect remaining args into a list when `&` is encountered
3. **Function creation** - May need to store variadic info (e.g., `int variadic_index`)

## Clojure Semantics

In Clojure:
- `&` must be followed by exactly one symbol (the rest parameter)
- The rest parameter binds to a seq (possibly nil) of remaining args
- Zero remaining args → nil (not empty list)
- Arity is: `required_params <= argc` (no upper limit when variadic)

## Example Fix Pseudocode

```c
// During function call:
int required_params = count_until_ampersand(func->params);
int has_rest_param = has_ampersand(func->params);

if (has_rest_param) {
    // Arity: at least required_params
    if (argc < required_params) {
        throw_arity_exception();
    }
    // Bind required params normally
    for (int i = 0; i < required_params; i++) {
        bind_param(params[i], args[i]);
    }
    // Bind rest param to list of remaining args
    CljList *rest = collect_remaining_args(args + required_params, 
                                            argc - required_params);
    bind_param(rest_param_symbol, rest);  // May be nil if no remaining
} else {
    // Exact arity required
    if (argc != param_count) {
        throw_arity_exception();
    }
    // Bind all params normally
}
```

## When Fixed

Once variadic parameters work:

1. **Enable the 5 IGNORED tests** in `src/tests/test_macros.c`
2. **Define `defn` as a Clojure macro**:
   ```clojure
   (defmacro defn [name & decl]
     (let [docstring (when (string? (first decl)) (first decl))
           decl (if docstring (rest decl) decl)
           params (first decl)
           body (rest decl)]
       `(def ~name (fn ~name ~params ~@body))))
   ```
3. **Remove C `eval_special_defn`** from `src/eval_special_forms.c`
4. **Remove `SYM_DEFN`** registration as special form

## Related Files

- `src/tests/test_macros.c` - Test cases (5 IGNORED)
- `src/function.h` - CljFunction structure
- `src/function.c` - Function creation
- `src/eval.c` - Function call and parameter binding
- `src/eval_special_forms.c` - C-based defn (to be removed after fix)
- `src/parser.c` - Parameter parsing
- `src/symbol.c` - Symbol definitions

