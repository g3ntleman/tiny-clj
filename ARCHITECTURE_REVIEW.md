# Architecture Review: Environment and Context for Lexical Scoping

## Current Architecture Analysis

### Current Components

1. **EvalContext** (`eval.h`):
   - `env`: Current environment map
   - `env_stack`: Environment stack (list of maps) for closures
   - `frame`: Stack-based call frame for parameters
   - `st`: Evaluation state

2. **CljFunction** (`function.h`):
   - `env_stack`: Environment stack captured at function creation time (defining environment)
   - `body`: Function body
   - `params`: Function parameters

3. **CallFrame** (`environment.h`):
   - Stack-based parameter bindings
   - Parent frame chain for nested calls

### Current Problem

The current implementation tries to combine:
- `func->env_stack` (defining environment - captured at function creation)
- `outer_ctx->env_stack` (calling environment - current evaluation context)

This is **incorrect** for lexical scoping!

## Lexical Scoping Principles (from Research)

### Key Concepts

1. **Defining Environment vs Calling Environment**:
   - **Defining Environment**: The environment in which a function was **defined** (captured in closure)
   - **Calling Environment**: The environment in which a function is **called** (current context)
   
2. **Lexical Scoping Rule**: 
   - Functions should resolve free variables from their **defining environment**, not the calling environment
   - Example: `(let [x 42] (def f (fn [] x)))` → `f` should always return 42, regardless of where it's called

3. **Closure Capture**:
   - When a function is created, it captures the current environment chain
   - This captured environment is used for variable resolution, not the calling environment

### Correct Architecture Pattern

Based on Scheme/Clojure implementations:

```
Function Creation:
  - Capture current env_stack → func->env_stack (defining environment)
  
Function Call:
  - Use func->env_stack for free variable resolution (lexical scope)
  - Create new CallFrame for parameters (calling context)
  - Combine: [CallFrame] → func->env_stack (NOT outer_ctx->env_stack!)
```

## Proposed Architecture

### Principle: Separate Defining and Calling Environments

1. **Defining Environment** (`func->env_stack`):
   - Captured at function creation time
   - Immutable (doesn't change after creation)
   - Used for resolving free variables (lexical scope)

2. **Calling Environment** (`CallFrame`):
   - Created at function call time
   - Contains parameter bindings
   - Stack-based (no heap allocation)

3. **Resolution Order**:
   ```
   1. CallFrame (parameters) - most recent
   2. func->env_stack (defining environment) - lexical scope
   3. Namespace (global scope)
   ```

### Current Bug

In `eval_function_call_with_context`, we're doing:
```c
// WRONG: Combining calling environment with defining environment
outer_stack = frame_chain_to_env_stack(outer_ctx->frame, outer_ctx->env_stack);
call_env_stack = combine(outer_stack, func->env_stack);
```

This is wrong because:
- `outer_ctx` is the **calling** environment
- `func->env_stack` is the **defining** environment
- We should NOT combine them - we should only use `func->env_stack` for free variables

### Correct Implementation

```c
// CORRECT: Use defining environment for free variables
call_env_stack = func->env_stack;  // Use defining environment

// Parameters come from CallFrame (created at call time)
CallFrame call_frame;
frame_set_bindings(&call_frame, NULL, params, args, argc);

// Resolution: CallFrame → func->env_stack → namespace
```

## Implementation Plan

### Phase 1: Remove Incorrect Combination
- Remove `outer_ctx->env_stack` combination in `eval_function_call_with_context`
- Use only `func->env_stack` for free variable resolution
- Parameters come from `CallFrame` (already correct)

### Phase 2: Fix Function Creation
- Ensure `eval_fn_with_context` correctly captures defining environment
- Verify `frame_chain_to_env_stack` captures the full lexical scope chain

### Phase 3: Fix Symbol Resolution
- Ensure `resolve_symbol_in_env_with_frame` uses:
  1. CallFrame (parameters)
  2. func->env_stack (defining environment)
  3. Namespace (global)

### Phase 4: Remove Ad-Hoc Solutions
- Remove any thread-local storage hacks
- Ensure `ctx` is passed through all call chains properly

## References

- Scheme interpreters: Use defining environment for closures
- Clojure: Lexical scoping with captured environment
- Common Lisp: Lexical closures capture defining environment



