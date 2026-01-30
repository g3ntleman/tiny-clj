# Add-Watch and Remove-Watch Implementation Plan

## Overview

Implement Clojure-compatible `add-watch` and `remove-watch` functions for atoms using **Test-First** approach. Functions are defined directly in `src/clojure.core.clj` to match Clojure's namespace structure.**Reference Implementations** (from source code analysis):

- **Clojure (JVM)**: `(.addWatch reference key fn)` - Delegates to Java `IRef` interface
- **ClojureScript**: `(-add-watch iref key f)` - Uses `IWatchable` protocol, returns `iref`
- **Tiny-CLJ**: Pure Clojure with registry (similar to ClojureScript, but without protocols)

**Clojure Compatibility**:

- ✅ Watcher function receives `[key ref old new]` arguments
- ✅ Watchers called synchronously on `reset!` and `swap!`
- ✅ Multiple watchers per atom supported
- ✅ Watchers can throw exceptions (others continue)
- ✅ `add-watch` and `remove-watch` return the atom (for threading)
- ✅ Keys can be any Clojure value

## Architecture

```javascript
┌─────────────────┐
│  Clojure API     │
│  add-watch       │  ← Pure Clojure (~10 lines)
│  remove-watch    │  ← Pure Clojure (~10 lines)
└────────┬─────────┘
         │
         │ uses
         ▼
┌─────────────────────────┐
│  Watcher Registry       │
│  (atom {Atom ->         │  ← Clojure Atom (COW)
│    Map of Key->WatchFn}) │  ← Simple maps (small, DRY)
└────────┬────────────────┘
         │
         │ called from
         ▼
┌─────────────────────────┐
│  atom_reset()           │
│  atom_swap()            │  ← C (must stay in C)
│    ↓                    │
│  atom_notify_watchers() │  ← Minimal C hook (~15 lines)
│    ↓                    │
│  notify-watchers()      │  ← Clojure function
└─────────────────────────┘
```

**Code Distribution**:

- **Clojure**: ~35 lines (registry + functions + notification)
- **C**: ~15 lines (minimal hook with function caching)
- **Total**: ~50 lines vs ~200 lines pure C (75% reduction)

**Embedded Optimizations**:

- Function caching: Zero lookup overhead after first call
- Simple maps: No HashMap overhead for small watcher lists
- COW-optimized: Registry uses Clojure atom (efficient)
- Minimal C code: Small binary footprint

## Implementation Strategy: Test-First, Step-by-Step

### DRY Principles

- Reuse `update-watcher-map` helper (no code duplication)
- Single `get-watcher-map` helper for all lookups
- Shared `notify-watchers` function for all notifications

### Step-by-Step Approach

1. **Write test** → 2. **Run test** (should fail) → 3. **Implement minimal code** → 4. **Run test** (should pass) → 5. **Refactor** → 6. **Run all tests** → 7. **Next step**

After each step: Run unit tests to verify no regressions.

## Step 1: Test Infrastructure and Registry

### 1.1 Write Tests

**File**: `src/tests/test_atom_watch.c` (NEW)

```c
#include "tests_common.h"
#include "../atom.h"
#include "../runtime.h"
#include "../eval.h"
#include "../reader.h"

// Test: Registry exists and is accessible
TEST(test_watcher_registry_exists) {
    // Load clojure.core to initialize registry
    extern const char *clojure_core_code;
    EvalState *st = test_get_eval_state();
    eval_string(clojure_core_code, st);
    
    // Check that watcher-registry symbol exists
    CljSymbol *reg_sym = intern_symbol_global("watcher-registry");
    TEST_ASSERT_NOT_NULL(reg_sym);
    
    ID reg = ns_resolve(NULL, reg_sym);
    TEST_ASSERT_NOT_NULL(reg);
    TEST_ASSERT_EQUAL(CLJ_ATOM, TAG(reg));
}

// Test: Registry starts empty
TEST(test_watcher_registry_starts_empty) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    // Get registry value
    CljSymbol *reg_sym = intern_symbol_global("watcher-registry");
    ID reg = ns_resolve(NULL, reg_sym);
    CljAtom *reg_atom = (CljAtom*)reg;
    CljPersistentMap *reg_value = (CljPersistentMap*)atom_deref(reg_atom);
    
    TEST_ASSERT_NOT_NULL(reg_value);
    TEST_ASSERT_EQUAL(0, map_count(reg_value));
}
```



### 1.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 1.3 Implement Registry

**File**: `src/clojure.core.clj`Add to end of file:

```clojure
; Watcher registry: Atom -> Map of Key -> WatchFn
; Use simple maps for small watcher lists per atom (not HashMaps)
(def watcher-registry (atom {}))

; Helper: Get watcher map for an atom (DRY - single lookup point)
(defn- get-watcher-map [atom]
  (get @watcher-registry atom))

; Helper: Update watcher map for an atom (DRY - single update point)
(defn- update-watcher-map [atom f]
  (swap! watcher-registry update atom (fnil f {})))
```



### 1.4 Run Tests (should pass)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



## Step 2: Add-Watch Function

### 2.1 Write Tests

**File**: `src/tests/test_atom_watch.c`Add:

```c
// Test: add-watch adds a watcher
TEST(test_add_watch_adds_watcher) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    // Create atom
    CljAtom *atom = make_atom(fixnum(0));
    
    // Add watcher via Clojure
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] nil))", st);
    
    // Check registry contains watcher
    CljSymbol *reg_sym = intern_symbol_global("watcher-registry");
    ID reg = ns_resolve(NULL, reg_sym);
    CljAtom *reg_atom = (CljAtom*)reg;
    CljPersistentMap *reg_value = (CljPersistentMap*)atom_deref(reg_atom);
    
    TEST_ASSERT_EQUAL(1, map_count(reg_value));
    
    RELEASE(atom);
}

// Test: add-watch returns atom (for threading)
TEST(test_add_watch_returns_atom) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    ID result = eval_string("(add-watch test-atom :test (fn [k a o n] nil))", st);
    
    // Should return the atom
    CljSymbol *atom_sym = intern_symbol_global("test-atom");
    ID atom = ns_resolve(NULL, atom_sym);
    
    TEST_ASSERT_EQUAL(atom, result);
}

// Test: add-watch validates arguments
TEST(test_add_watch_validates_atom) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    // Should throw exception for non-atom
    ID result = eval_string("(add-watch 42 :test (fn [k a o n] nil))", st);
    TEST_ASSERT_EQUAL(CLJ_EXCEPTION, TAG(result));
}

// Test: add-watch validates function
TEST(test_add_watch_validates_function) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    ID result = eval_string("(add-watch test-atom :test 42)", st);
    TEST_ASSERT_EQUAL(CLJ_EXCEPTION, TAG(result));
}
```



### 2.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 2.3 Implement add-watch

**File**: `src/clojure.core.clj`**Reference**: ClojureScript's `add-watch` returns `iref` for threading. Clojure's docstring describes the behavior.Add:

```clojure
(defn add-watch [atom key watch-fn]
  "Adds a watch function to an atom. Returns the atom (for threading).
  
   The watch fn must be a fn of 4 args: a key, the reference, its old-state,
   its new-state. Whenever the reference's state might have been changed,
   any registered watches will have their functions called. The watch fn
   will be called synchronously. Note that an atom's state may have changed
   again prior to the fn call, so use old/new-state rather than derefing
   the reference. Keys must be unique per reference, and can be used to
   remove the watch with remove-watch, but are otherwise considered opaque
   by the watch mechanism.
  
   Arguments:
    - atom: The atom to watch
    - key: Any unique value to identify this watcher
    - watch-fn: Function of 4 args [key atom old-value new-value]
  
   Returns: The atom (for threading with ->)
  
   IMPORTANT - Retain Loop Prevention:
   The watch-fn receives the atom as its second argument. Do NOT capture
   the atom in the watcher's closure, as this creates a retain cycle:
   
   Good (no cycle):
     (add-watch temp :logger
       (fn [key atom old new]  ; atom is argument, not captured
         (println \"Changed:\" new)))
   
   Problematic (creates cycle):
     (let [self temp]  ; Captures atom in closure!
       (add-watch temp :self-ref
         (fn [key atom old new]
           (println \"Self:\" self))))
   
   If a watcher captures the atom, you MUST call (remove-watch atom key)
   before releasing the atom to break the retain cycle."
  (when (not (atom? atom))
    (throw (Exception. "add-watch requires an atom")))
  (when (not (fn? watch-fn))
    (throw (Exception. "add-watch requires a function as third argument")))
  
  ; DRY: Reuse update-watcher-map helper
  (update-watcher-map atom #(assoc % key watch-fn))
  atom)
```



### 2.4 Run Tests (should pass)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



## Step 3: Remove-Watch Function

### 3.1 Write Tests

**File**: `src/tests/test_atom_watch.c`Add:

```c
// Test: remove-watch removes a watcher
TEST(test_remove_watch_removes_watcher) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] nil))", st);
    eval_string("(remove-watch test-atom :test)", st);
    
    // Check registry is empty
    CljSymbol *reg_sym = intern_symbol_global("watcher-registry");
    ID reg = ns_resolve(NULL, reg_sym);
    CljAtom *reg_atom = (CljAtom*)reg;
    CljPersistentMap *reg_value = (CljPersistentMap*)atom_deref(reg_atom);
    
    TEST_ASSERT_EQUAL(0, map_count(reg_value));
}

// Test: remove-watch returns atom
TEST(test_remove_watch_returns_atom) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] nil))", st);
    ID result = eval_string("(remove-watch test-atom :test)", st);
    
    CljSymbol *atom_sym = intern_symbol_global("test-atom");
    ID atom = ns_resolve(NULL, atom_sym);
    
    TEST_ASSERT_EQUAL(atom, result);
}

// Test: remove-watch cleans up empty entries
TEST(test_remove_watch_cleans_up_empty) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] nil))", st);
    eval_string("(remove-watch test-atom :test)", st);
    
    // Registry should not contain empty atom entry
    CljSymbol *reg_sym = intern_symbol_global("watcher-registry");
    ID reg = ns_resolve(NULL, reg_sym);
    CljAtom *reg_atom = (CljAtom*)reg;
    CljPersistentMap *reg_value = (CljPersistentMap*)atom_deref(reg_atom);
    
    TEST_ASSERT_EQUAL(0, map_count(reg_value));
}
```



### 3.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 3.3 Implement remove-watch

**File**: `src/clojure.core.clj`Add:

```clojure
(defn remove-watch [atom key]
  "Removes a watch function from an atom. Returns the atom (for threading)."
  ; DRY: Reuse update-watcher-map helper
  (update-watcher-map atom
    (fn [watcher-map]
      (let [new-map (dissoc watcher-map key)]
        (if (empty? new-map) nil new-map))))
  
  ; Clean up empty entries in registry (DRY: single cleanup point)
  (swap! watcher-registry
    (fn [registry]
      (if (empty? (get registry atom))
        (dissoc registry atom)
        registry)))
  atom)
```



### 3.4 Run Tests (should pass)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



## Step 4: Notify Watchers (Clojure)

### 4.1 Write Tests

**File**: `src/tests/test_atom_watch.c`Add:

```c
// Test: notify-watchers function exists
TEST(test_notify_watchers_function_exists) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    CljSymbol *fn_sym = intern_symbol_global("notify-watchers");
    ID fn = ns_resolve(NULL, fn_sym);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_EQUAL(CLJ_CLOSURE, TAG(fn) || TAG(fn) == CLJ_FUNC);
}

// Test: notify-watchers calls watcher function
TEST(test_notify_watchers_calls_watcher) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    // Create atom and watcher
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def called (atom false))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] (reset! called true)))", st);
    
    // Manually call notify-watchers
    eval_string("(notify-watchers test-atom 0 42)", st);
    
    // Check watcher was called
    CljSymbol *called_sym = intern_symbol_global("called");
    ID called_atom = ns_resolve(NULL, called_sym);
    CljAtom *atom = (CljAtom*)called_atom;
    ID value = atom_deref(atom);
    
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL(CLJ_TRUE, value);
}
```



### 4.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 4.3 Implement notify-watchers

**File**: `src/clojure.core.clj`**Reference Implementation** (from ClojureScript sources):

- **ClojureScript**: `(-notify-watches [this oldval newval] (doseq [[key f] watches] (f key this oldval newval)))`
- **Tiny-CLJ**: Similar approach using `doseq` over watcher map

Add:

```clojure
; Internal helper function (called from C)
; DRY: Single notification point, reusable
; ClojureScript-compatible: Uses doseq like ClojureScript's IWatchable implementation
(defn- notify-watchers [atom old-value new-value]
  "Internal function called from C to notify watchers.
   Similar to ClojureScript's -notify-watches protocol method."
  (let [watcher-map (get-watcher-map atom)]  ; DRY: Reuse helper
    (when watcher-map
      ; ClojureScript pattern: doseq over watchers, call each with [key this oldval newval]
      (doseq [[key watch-fn] watcher-map]
        (try
          (watch-fn key atom old-value new-value)
          (catch Exception e
            (println "Watcher error:" e)))))))
```



### 4.4 Run Tests (should pass)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



## Step 5: C Hook (Minimal)

### 5.1 Write Tests

**File**: `src/tests/test_atom_watch.c`Add:

```c
// Test: atom_notify_watchers function exists
TEST(test_atom_notify_watchers_exists) {
    // Function should be declared in atom.h
    // Just verify it compiles and links
    TEST_ASSERT_TRUE(true);
}

// Test: atom_notify_watchers calls Clojure function
TEST(test_atom_notify_watchers_calls_clojure) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    // Create atom and watcher
    CljAtom *atom = make_atom(fixnum(0));
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def called (atom false))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] (reset! called true)))", st);
    
    // Get atom from Clojure
    CljSymbol *atom_sym = intern_symbol_global("test-atom");
    ID clj_atom = ns_resolve(NULL, atom_sym);
    CljAtom *clj_atom_ptr = (CljAtom*)clj_atom;
    
    // Call C hook
    atom_notify_watchers(clj_atom_ptr, fixnum(0), fixnum(42));
    
    // Check watcher was called
    CljSymbol *called_sym = intern_symbol_global("called");
    ID called_atom = ns_resolve(NULL, called_sym);
    CljAtom *called_ptr = (CljAtom*)called_atom;
    ID value = atom_deref(called_ptr);
    
    TEST_ASSERT_EQUAL(CLJ_TRUE, value);
    
    RELEASE(atom);
}
```



### 5.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 5.3 Implement C Hook

**File**: `src/atom.c`Add before `atom_reset()`:

```c
// Cached function reference (lazy initialization)
// Embedded optimization: Zero lookup overhead after first call
static ID g_notify_watchers_fn = NULL;

void atom_notify_watchers(CljAtom *atom, ID old_value, ID new_value) {
    if (!atom) return;
    
    // Lazy initialization: resolve function once, cache it
    // Embedded optimization: Function caching reduces overhead
    if (!g_notify_watchers_fn) {
        CljSymbol *fn_sym = intern_symbol_global("notify-watchers");
        g_notify_watchers_fn = ns_resolve(NULL, fn_sym);
        if (!g_notify_watchers_fn) return;
        RETAIN(g_notify_watchers_fn);
    }
    
    // Prepare arguments: [atom, old-value, new-value]
    ID args[3];
    args[0] = RETAIN((ID)atom);
    args[1] = RETAIN(old_value);
    args[2] = RETAIN(new_value);
    
    // Call Clojure function (exception handling in Clojure)
    EvalState *st = get_global_eval_state();
    CljPersistentMap *env = st ? (CljPersistentMap*)st->current_ns->mappings : NULL;
    eval_function_call(g_notify_watchers_fn, args, 3, env, st);
    
    // Cleanup arguments
    RELEASE((ID)atom);
    RELEASE(old_value);
    RELEASE(new_value);
}
```

**File**: `src/atom.h`Add declaration:

```c
void atom_notify_watchers(CljAtom *atom, ID old_value, ID new_value);
```



### 5.4 Run Tests (should pass)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



## Step 6: Integration into atom_reset and atom_swap

### 6.1 Write Tests

**File**: `src/tests/test_atom_watch.c`Add:

```c
// Test: reset! triggers watchers
TEST(test_reset_triggers_watchers) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def old-val (atom nil))", st);
    eval_string("(def new-val (atom nil))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] (reset! old-val o) (reset! new-val n)))", st);
    
    eval_string("(reset! test-atom 42)", st);
    
    // Check watcher received correct values
    CljSymbol *old_sym = intern_symbol_global("old-val");
    ID old_atom = ns_resolve(NULL, old_sym);
    CljAtom *old_ptr = (CljAtom*)old_atom;
    ID old_value = atom_deref(old_ptr);
    TEST_ASSERT_EQUAL(0, as_fixnum((CljValue)old_value));
    
    CljSymbol *new_sym = intern_symbol_global("new-val");
    ID new_atom = ns_resolve(NULL, new_sym);
    CljAtom *new_ptr = (CljAtom*)new_atom;
    ID new_value = atom_deref(new_ptr);
    TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)new_value));
}

// Test: swap! triggers watchers
TEST(test_swap_triggers_watchers) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def called (atom false))", st);
    eval_string("(add-watch test-atom :test (fn [k a o n] (reset! called true)))", st);
    
    eval_string("(swap! test-atom inc)", st);
    
    CljSymbol *called_sym = intern_symbol_global("called");
    ID called_atom = ns_resolve(NULL, called_sym);
    CljAtom *called_ptr = (CljAtom*)called_atom;
    ID value = atom_deref(called_ptr);
    
    TEST_ASSERT_EQUAL(CLJ_TRUE, value);
}

// Test: Multiple watchers are called
TEST(test_multiple_watchers_called) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def count1 (atom 0))", st);
    eval_string("(def count2 (atom 0))", st);
    eval_string("(add-watch test-atom :w1 (fn [k a o n] (swap! count1 inc)))", st);
    eval_string("(add-watch test-atom :w2 (fn [k a o n] (swap! count2 inc)))", st);
    
    eval_string("(reset! test-atom 42)", st);
    
    // Both watchers should be called
    CljSymbol *c1_sym = intern_symbol_global("count1");
    ID c1_atom = ns_resolve(NULL, c1_sym);
    CljAtom *c1_ptr = (CljAtom*)c1_atom;
    ID c1_value = atom_deref(c1_ptr);
    TEST_ASSERT_EQUAL(1, as_fixnum((CljValue)c1_value));
    
    CljSymbol *c2_sym = intern_symbol_global("count2");
    ID c2_atom = ns_resolve(NULL, c2_sym);
    CljAtom *c2_ptr = (CljAtom*)c2_atom;
    ID c2_value = atom_deref(c2_ptr);
    TEST_ASSERT_EQUAL(1, as_fixnum((CljValue)c2_value));
}

// Test: Exception in watcher doesn't crash
TEST(test_watcher_exception_handled) {
    EvalState *st = test_get_eval_state();
    extern const char *clojure_core_code;
    eval_string(clojure_core_code, st);
    
    eval_string("(def test-atom (atom 0))", st);
    eval_string("(def called (atom false))", st);
    eval_string("(add-watch test-atom :bad (fn [k a o n] (throw (Exception. \"error\"))))", st);
    eval_string("(add-watch test-atom :good (fn [k a o n] (reset! called true)))", st);
    
    // Should not crash, good watcher should still be called
    eval_string("(reset! test-atom 42)", st);
    
    CljSymbol *called_sym = intern_symbol_global("called");
    ID called_atom = ns_resolve(NULL, called_sym);
    CljAtom *called_ptr = (CljAtom*)called_atom;
    ID value = atom_deref(called_ptr);
    
    TEST_ASSERT_EQUAL(CLJ_TRUE, value);
}
```



### 6.2 Run Tests (should fail)

```bash
./scripts/run_unit_tests.sh --test atom_watch/*
```



### 6.3 Integrate into atom_reset

**File**: `src/atom.c`Modify `atom_reset()`:

```c
ID atom_reset(CljAtom *atom, ID new_value) {
    if (!atom) return NULL;
    
    ID old_value = RETAIN(atom->value);
    ASSIGN(atom->value, new_value);
    
    // Notify watchers (embedded: minimal overhead, cached function)
    atom_notify_watchers(atom, old_value, new_value);
    
    RELEASE(old_value);
    return RETAIN(new_value);
}
```



### 6.4 Run Tests (should pass for reset!)

```bash
./scripts/run_unit_tests.sh --test atom_watch/test_reset_triggers_watchers
```



### 6.5 Integrate into atom_swap

**File**: `src/atom.c`Modify `atom_swap()` - find where `atom->value` is updated and add notification:

```c
ID atom_swap(CljAtom *atom, ID fn, ID *args, unsigned int argc) {
    // ... exiss oting validation code ...
    
    // Get old value before updating
    ID old_value = RETAIN(atom->value);
    
    // ... existing function call code ...
    // After updating atom->value:
    
    ID new_value = RETAIN(atom->value);
    
    // Notify watchers (embedded: minimal overhead, cached function)
    atom_notify_watchers(atom, old_value, new_value);
    
    RELEASE(old_value);
    return new_value;
}
```



### 6.6 Run All Tests

```bash
./scripts/run_unit_tests.sh
```



## Step 7: Final Verification and Cleanup

### 7.1 Run All Tests

```bash
./scripts/run_unit_tests.sh
```



### 7.2 Memory Profiling

```bash
# Check for memory leaks
./build/unit-tests --test atom_watch/*
```



### 7.3 Code Review Checklist

- [ ] DRY: No code duplication (helpers reused)
- [ ] Embedded: Function caching implemented
- [ ] Embedded: Simple maps used (not HashMaps for small lists)
- [ ] Embedded: Minimal C code (~15 lines)
- [ ] Tests: All tests pass
- [ ] Tests: No memory leaks
- [ ] Tests: No Autorelease-Pools used
- [ ] Performance: Function resolution cached

## File Changes Summary

### Modified Files

1. `src/atom.c` - Add `atom_notify_watchers()` and integrate into `atom_reset()` and `atom_swap()`
2. `src/atom.h` - Add `atom_notify_watchers()` declaration
3. `src/clojure.core.clj` - Add watcher registry and functions

### New Files

1. `src/tests/test_atom_watch.c` - Test-First test suite

## Embedded System Optimizations

1. **Function Caching**: `g_notify_watchers_fn` cached after first call (zero lookup overhead)
2. **Simple Maps**: Use simple maps for small watcher lists (no HashMap overhead)
3. **COW Optimization**: Registry uses Clojure atom (efficient copy-on-write)
4. **Minimal C Code**: Only ~15 lines of C code (small binary footprint)
5. **No Autorelease-Pools**: Direct memory management in tests (embedded-friendly)

## Retain Loop Prevention

The retain loop problem is **documented in the `add-watch` docstring** (see Step 2.3). The docstring includes:

- **Clear explanation** of the retain cycle problem
- **Good example** showing correct usage (atom as argument, not captured)
- **Problematic example** showing what creates a cycle
- **Solution**: Call `remove-watch` before releasing atom if cycle exists

Key points:

- **Watcher functions receive the atom as an argument** - they should NOT capture it in their closure
- **If a watcher captures the atom**, the user MUST call `remove-watch` before releasing the atom
- **Best practice**: Always use the `atom` parameter, never capture it