# Development Insights: API Design and Memory Management

## Overview
This document captures key learnings from the refactoring of `test_for_loops_comparison.c` and the implementation of the separated parse/eval API in tiny-clj.

## Key Learnings

### 1. API Design and Memory Policy

#### ❌ Common Misconception
- **Assumption**: `parse_string` returns objects that need manual release
- **Reality**: `parse_string` returns **autoreleased** objects
- **Impact**: Unnecessary `RELEASE(parsed)` calls were added

#### ✅ Correct Understanding
- All evaluation functions return **autoreleased** objects consistently
- Doxygen documentation was already correct: `@return (autoreleased)`
- Trust the existing API design and documentation

### 2. Test-First Development Strategy

#### ✅ Correct Test-First Process
```c
// 1. Write tests (they should FAIL initially)
CljObject *result = eval_string("my-var", NULL);
mu_assert_obj_int(result, 42);  // ← This will fail until implementation

// 2. Implement functionality until tests pass
// 3. Refactor while keeping tests green
```

#### ❌ Common Anti-Patterns
```c
// WRONG: Deactivating tests
// // Test: access the defined variable
// CljObject *var_result = eval_string("my-var", NULL);
// mu_assert_obj_int(var_result, 42);

// WRONG: Tests that always pass
mu_assert("This always passes", true);
```

#### ✅ Correct Approach
- **Tests must be ACTIVE** and fail until implementation
- **Tests drive the development** - they define the expected behavior
- **Never deactivate tests** - they are the specification
- **Implementation goal**: Make all tests pass

#### Benefits
- **`__FUNCTION__` macro** for automatic test naming
- **Immediate feedback** on API correctness
- **Memory profiling** validates implementation
- **Tests serve as living documentation** of expected behavior

### 3. Separated Parse/Eval API Architecture

#### Design Decision
```c
// Separate functions for precise performance measurement
CljObject* parse_string(...);     // Parsing only
CljObject* eval_parsed(...);      // Evaluation only
CljObject* eval_string(...);      // Convenience (both)
```

#### Advantages
- **Precise performance measurement** (parsing vs. evaluation separated)
- **Better control** over memory management
- **Easier debugging** of parsing vs. evaluation issues
- **Flexible usage patterns** (parse once, evaluate multiple times)

### 4. Memory Management Insights

#### Singleton Objects
- **Singleton objects are allowed to leak** (performance optimization)
- **Memory profiler** shows these as "leaks" but this is correct behavior
- **Performance benefit** through object reuse

#### Autorelease Pool
- **Autorelease pool** works correctly for test scenarios
- **Memory profiling** shows autorelease calls being tracked
- **Consistent behavior** across all evaluation functions

### 5. Code Quality Improvements

#### String Literals - Raw Strings (C11)
```c
// Before (ugly with escapes):
const char* expr = "(doseq [x [\"Alpha\" \"Beta\" \"Gamma\"]] (println x))";

// After (clean with Raw Strings):
const char* expr = R"((doseq [x ["Alpha" "Beta" "Gamma"]] (println x)))";
```

#### Raw String Benefits
- **No escape sequences** - eliminates `\"` and other escapes
- **Natural Clojure syntax** - copy/paste from Clojure code works directly
- **Better readability** - immediately clear what the string contains
- **Maintainability** - easy to edit without escape confusion
- **Performance** - single string literal, no runtime concatenation

#### When to Use Raw Strings
```c
// ✅ Use Raw Strings when string contains quotes:
const char* clojure_expr = R"((println "Hello World"))";
const char* test_data = R"(["Alpha" "Beta" "Gamma"])";

// ❌ Don't use Raw Strings for simple strings:
const char* simple_expr = "(+ 1 2)";  // No quotes, no need for Raw String
const char* symbol_name = "my-symbol";  // No quotes, no need for Raw String
```

**Rule**: Only use Raw Strings when the string literal contains quotes or other characters that would require escaping.

### **9. Memory-Testing-Integration:**
```c
// Statt:
MEMORY_TEST_START("test_name");
// ... test code ...
MEMORY_TEST_END("test_name");

// Besser:
WITH_MEMORY_PROFILING({
    // ... test code ...
});
```

**Vorteile:**
- **Automatische Test-Namen** - verwendet `__FUNCTION__` 
- **Sauberer Code** - weniger Boilerplate
- **Konsistente Nutzung** - ein Makro für alle Memory-Tests
- **Automatisches Cleanup** - Start/End wird automatisch verwaltet

### **10. Test-Code-Verbesserungen:**
```c
// Statt Magic Numbers:
for (int i = 0; i < 5; i++) { /* ... */ }

// Besser mit Konstanten:
#define TEST_VECTOR_SIZE 5
for (int i = 0; i < TEST_VECTOR_SIZE; i++) { /* ... */ }
```

**Vorteile:**
- **Wartbarkeit** - zentrale Definition, Änderung an einem Ort
- **Lesbarkeit** - selbstdokumentierend, aussagekräftige Namen
- **Konsistenz** - alle Operationen verwenden dieselbe Größe
- **Weniger Fehlerquellen** - keine unterschiedlichen Magic Numbers

### **11. MinUnit-Makro-Optimierung:**
```c
// Statt redundante Checks:
mu_assert_obj_not_null(result);  // ← redundant
mu_assert_obj_type(result, CLJ_INT);
mu_assert_obj_int(result, 3);

// Besser (spezifische Makros enthalten bereits Checks):
mu_assert_obj_int(result, 3);  // ← enthält NULL- und Typ-Check
```

**Vorteile:**
- **Weniger Boilerplate** - keine redundanten Assertions
- **Bessere Lesbarkeit** - fokussiert auf das Wesentliche
- **Konsistenter Stil** - ein Assertion pro Eigenschaft
- **Weniger Fehlerquellen** - spezifische Makros sind sicherer

#### Documentation Standards
- **Doxygen comments** with memory policy information
- **Consistent API documentation** across all functions
- **Clear parameter and return value descriptions**

### 6. Error Handling and Debugging

#### Memory Profiling
- **Essential tool** for correct implementation
- **Immediate feedback** on memory leaks
- **Distinguishes** between expected and unexpected leaks

#### Fail-Fast Principle
```c
// Setup code with fail-fast validation
if (!shared_string_vector || shared_string_vector->type != CLJ_VECTOR) {
    printf("Error: Failed to parse Clojure vector\n");
    exit(1); // Fail fast if parsing fails
}
```

### 7. Project Organization

#### Public API Structure
- **`tiny_clj.h`**: Main public API
- **`clj_parser.h`**: Parser-specific API
- **Consistent memory policy** across all functions

#### Build System Integration
- **CMake integration** for new test executables
- **Conditional compilation** for debug features
- **Memory profiling** only in debug builds

## Best Practices Established

### 1. API Design
- **Consistent memory policy** across all functions
- **Clear documentation** of memory ownership
- **Separate concerns** (parsing vs. evaluation)

### 2. Testing
- **Test-First development** for new features
- **Tests must be ACTIVE** and fail until implementation
- **Never deactivate tests** - they are the specification
- **Memory profiling** for validation
- **Comprehensive test coverage** for different scenarios

### 3. Documentation
- **Doxygen comments** with memory policy
- **Clear parameter descriptions**
- **Return value documentation** including memory ownership

### 4. Code Quality
- **Clean string literals** without ugly escapes
- **Consistent formatting** and naming
- **Fail-fast error handling**

## Lessons Learned

### 1. Trust the API Design
- **Existing API was already correct**
- **Documentation was accurate**
- **Don't make assumptions** about memory management

### 2. Use Tools Effectively
- **Memory profiling** is essential for validation
- **Test-First approach** prevents incorrect implementations
- **Automated testing** catches issues early
- **Raw Strings** eliminate escape sequence confusion for complex literals

### 3. Follow Test-First Discipline
- **Tests are the specification** - they define what the code should do
- **Tests must fail initially** - this proves they are testing something
- **Never deactivate tests** - they are the contract
- **Implementation goal**: Make all tests pass
- **Refactoring goal**: Keep all tests green

### 4. Separate Concerns
- **Parsing and evaluation** are different concerns
- **Performance measurement** benefits from separation
- **Debugging** is easier with separated functions

### 5. Document Memory Policy
- **Clear documentation** prevents confusion
- **Consistent patterns** across the codebase
- **Examples** help developers understand usage

### 6. Use Raw Strings for Complex Literals
- **C11 Raw Strings** eliminate escape sequence confusion
- **Natural Clojure syntax** in C code
- **Better maintainability** for complex string literals
- **Performance benefits** over string concatenation

## Future Improvements

### 1. API Consistency
- **Audit all functions** for consistent memory policy
- **Documentation review** for completeness
- **Example usage** in documentation

### 2. Testing Infrastructure
- **Automated memory leak detection**
- **Performance regression testing**
- **Comprehensive test coverage**

### 3. Developer Experience
- **Clear error messages**
- **Better debugging tools**
- **Usage examples** and tutorials

## 14. Pre-Commit Testing Best Practice

### ✅ Always Run All Tests Before Committing

**Rule**: Run the complete test suite before every commit to catch regressions early.

```bash
# Run all tests
./run-tests

# Or run specific suites if needed
./run-tests --suite core
./run-tests --suite data
./run-tests --suite control
./run-tests --suite api
```

### Benefits

1. **Catch Regressions Early** - Find breaking changes before they enter the repository
2. **Maintain Code Quality** - Ensure all functionality works together
3. **Fast Feedback Loop** - Fix issues while context is fresh
4. **Clean History** - Every commit in history is known to work
5. **Easier Debugging** - Bisect works reliably when all commits pass tests
6. **Team Confidence** - Colleagues can trust that main branch is stable

### Workflow

```bash
# 1. Make changes
vim src/function_call.c

# 2. Run tests
./run-tests

# 3. Fix any failures
# ... repeat until all tests pass ...

# 4. Commit only when all tests pass
git add -A
git commit -m "Your changes"
```

### CI/CD Integration

If tests fail during commit (via pre-commit hook), you have two options:

```bash
# Option 1: Fix the tests (preferred)
# ... fix the failing tests ...
./run-tests  # Verify all pass
git commit

# Option 2: Skip hook if tests are unrelated to your changes
# Use sparingly, only for pre-existing failures
git commit --no-verify
```

### Exception Handling

- **Pre-existing failures**: Document them, but don't let them block unrelated work
- **New failures**: Must be fixed before committing
- **Flaky tests**: Mark or skip them, but don't ignore failures

### Test-Driven Development Integration

```bash
# 1. Write failing test
# 2. Run tests (should fail)
./run-tests --test new_feature

# 3. Implement feature
# 4. Run tests (should pass)
./run-tests --test new_feature

# 5. Run all tests (should pass)
./run-tests

# 6. Commit
git commit -m "Add new_feature"
```

**Key Insight**: Running all tests before commit is a simple practice that prevents most integration issues and maintains a clean, working codebase.

## Conclusion

The refactoring of `test_for_loops_comparison.c` provided valuable insights into API design, memory management, and development practices. The key takeaway is to **trust the existing API design** and use **Test-First development** with **memory profiling** to validate implementations.

The separated parse/eval API provides better control and performance measurement capabilities, while maintaining consistency with the existing memory management policy of tiny-clj.

### 8. MinUnit String Assertion Enhancement

#### ✅ New Macro: mu_assert_string_eq
```c
#define mu_assert_string_eq(actual, expected) \
    mu_assert("strings not equal", strcmp((actual), (expected)) == 0)
```

#### Benefits
- **Cleaner test code** - No more manual strcmp() calls
- **Consistent with other assertions** - Follows same pattern as mu_assert_obj_*
- **Better readability** - Intent is immediately clear
- **Less boilerplate** - Single line instead of strcmp + mu_assert

#### Usage Examples
```c
// Before (verbose):
char *result_str = pr_str(result);
mu_assert("result should be (1 2 3)", strcmp(result_str, "(1 2 3)") == 0);

// After (clean):
char *result_str = pr_str(result);
mu_assert_string_eq(result_str, "(1 2 3)");
```

#### Implementation Pattern
- Added to `minunit.h` alongside other assertion macros
- Uses same `strcmp` logic as existing `mu_assert_obj_string`
- Consistent error message format
- No performance overhead - just syntactic sugar

### 9. DRY Principle and Code Compaction

#### ✅ Core Principle: "Don't Repeat Yourself"
**Kompaktheit im Code ist eins der Kerziele dieses Projektes.**

#### Strategy: Local Helper Functions
Instead of repeating similar code patterns, extract them into well-named local helper functions.

#### Example: Test Code Optimization
```c
// Before (repetitive):
char *result_str = pr_str(eval_list(parse("(list 42)", &st), NULL));
mu_assert("list function should work", result_str != NULL);
mu_assert_string_eq(result_str, "(42)");
free(result_str);

char *multi_str = pr_str(eval_list(parse("(list 1 2 3)", &st), NULL));
mu_assert("multi list function should work", multi_str != NULL);
mu_assert_string_eq(multi_str, "(1 2 3)");
free(multi_str);

// After (DRY with helper):
static char* test_list_eval(EvalState *st, const char *expr, const char *expected) {
  char *result_str = pr_str(eval_list(parse(expr, st), NULL));
  mu_assert("list function should work", result_str != NULL);
  mu_assert_string_eq(result_str, expected);
  free(result_str);
  return 0;
}

test_list_eval(&st, "(list 42)", "(42)");
test_list_eval(&st, "(list 1 2 3)", "(1 2 3)");
```

#### Benefits of DRY Principle
- **Eliminates repetition** - Common patterns extracted once
- **Better readability** - Intent is clearer with descriptive function names
- **Easier maintenance** - Changes in one place affect all usages
- **Reduced code size** - Less boilerplate, more focus on test logic
- **Consistent behavior** - Same error handling and cleanup everywhere

#### Implementation Guidelines
- **Descriptive names** - `test_list_eval` clearly indicates purpose
- **Minimal parameters** - Only pass what's necessary
- **Consistent return patterns** - Follow test function conventions (char*)
- **Single responsibility** - Each helper does one thing well
- **Local scope** - Keep helpers close to where they're used

#### When to Apply DRY
- **3+ similar code blocks** - Extract to helper function
- **Repeated setup/teardown** - Create setup/cleanup helpers
- **Common assertion patterns** - Extract assertion helpers
- **Similar parsing/evaluation** - Create evaluation helpers

#### Code Compaction Goals
- **Reduce line count** - Fewer lines = less maintenance
- **Increase clarity** - Each line should be meaningful
- **Eliminate boilerplate** - Extract repetitive patterns
- **Focus on intent** - Code should read like documentation

### 10. MinUnit Design Pattern: char* as Boolean

#### ✅ Understanding MinUnit's Return Type
MinUnit uses `char*` as a boolean-like return type, which is unconventional but works:

```c
// MinUnit Return Pattern:
char* test_function(void) {
    // Test logic here...
    return NULL;    // Success (like true)
    return "error"; // Failure (like false)
}
```

#### The Pattern Explained
- **`NULL`** = Test passed (equivalent to `true`)
- **`char*`** = Test failed with error message (equivalent to `false`)

#### Helper Function Implementation
```c
// Helper functions should follow the same pattern:
static char* test_list_eval(EvalState *st, const char *expr, const char *expected) {
  char *result_str = pr_str(eval_list(parse(expr, st), NULL));
  if (!result_str) return "list function failed - no result";
  if (strcmp(result_str, expected) != 0) {
    free(result_str);
    return "list function result mismatch";
  }
  free(result_str);
  return NULL; // Success
}
```

#### Benefits of This Pattern
- **Error messages** - Failed tests can provide specific error details
- **Consistent API** - All test functions follow same signature
- **Simple checking** - `if (result) return result;` handles failures
- **No boolean confusion** - Clear distinction between success/failure

#### Usage in Test Functions
```c
static char *test_list_function(void) {
  EvalState st;
  memset(&st, 0, sizeof(EvalState));
  
  char *result = test_list_eval(&st, "(list 42)", "(42)");
  if (result) return result; // Early return on failure
  
  result = test_list_eval(&st, "(list 1 2 3)", "(1 2 3)");
  if (result) return result; // Early return on failure
  
  return NULL; // All tests passed
}
```

#### Key Insight
While `char*` seems like it should be `bool*`, it's actually a clever design that provides both success/failure status AND error messages in a single return value.

## 11. Function Call Evaluation and Symbol Resolution

### Problem: Function Calls Not Working
**Symptom**: `(add 1 2)` returned `"add"` (the symbol) instead of `3` (the result).

### Root Causes and Solutions

#### 1. eval_list Fallback Logic Missing Function Calls
**Problem**: When `eval_list` encountered an unknown operator, it simply returned the first element instead of attempting function resolution.

```c
// ❌ Before: Just return the symbol
if (op->type == CLJ_SYMBOL) {
    return head ? (RETAIN(head), head) : clj_nil();
}

// ✅ After: Resolve and call as function
if (op->type == CLJ_SYMBOL) {
    CljObject *fn = eval_symbol(op, st);
    if (fn && fn->type == CLJ_FUNC) {
        // Evaluate arguments and call function
        int argc = list_count(list) - 1;
        CljObject *args[argc];
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg(list, i + 1, env);
        }
        return eval_function_call(fn, args, argc, env);
    }
    return fn;
}
```

**Lesson**: Fallback logic must handle user-defined functions, not just built-in operators.

#### 2. eval_fn Must Support Vector Parameter Syntax
**Problem**: Clojure allows both `(fn [a b] ...)` and `(fn (a b) ...)`, but code only checked for `CLJ_LIST`.

```c
// ❌ Before: Only accepts lists
if (!params_list || params_list->type != CLJ_LIST) {
    return clj_nil();
}

// ✅ After: Accept both vectors and lists
if (!params_list || 
    (params_list->type != CLJ_LIST && params_list->type != CLJ_VECTOR)) {
    return clj_nil();
}

// Handle both types when extracting parameters
if (params_list->type == CLJ_VECTOR) {
    CljPersistentVector *vec = as_vector(params_list);
    params[i] = vec->data[i];
} else {
    params[i] = list_get_element(params_list, i);
}
```

**Lesson**: Support idiomatic Clojure syntax - vectors are the standard for parameter lists.

#### 3. Critical Bug in list_count
**Problem**: Incorrect iteration logic counted only the first element.

```c
// ❌ Before: Wrong iteration - iterated over head->tail
int count = 0;
CljObject *current = ld2 ? ld2->head : NULL;
while (current) {
    count++;
    CljList *cn = as_list(current);
    current = cn ? cn->tail : NULL;  // WRONG: iterates inside first element
}

// ✅ After: Correct iteration - iterate over list nodes
int count = 0;
CljList *current = as_list(list);
while (current && current->head) {
    count++;
    current = as_list(current->tail);  // CORRECT: iterate through list
}
```

**Impact**: This bug caused `(add 1 2)` to see 0 arguments instead of 2, triggering arity errors.

**Lesson**: List iteration must traverse the tail chain, not the head's internal structure.

#### 4. Symbol Resolution for Non-Interned Symbols
**Problem**: Function parameters compared by pointer equality, but symbols weren't interned.

```c
// ❌ Before: Only pointer comparison
if (params[i] && body == params[i]) {
    return values[i];
}

// ✅ After: Also compare by name
if (params[i] && body == params[i]) {
    return values[i];
}
// Fallback for non-interned symbols
if (params[i]->type == CLJ_SYMBOL && body->type == CLJ_SYMBOL) {
    CljSymbol *param_sym = as_symbol(params[i]);
    CljSymbol *body_sym = as_symbol(body);
    if (strcmp(param_sym->name, body_sym->name) == 0) {
        return values[i];  // Found by name
    }
}
```

**Lesson**: Always handle both interned (pointer equality) and non-interned (name equality) symbols.

#### 5. Multiple -e Arguments in REPL
**Problem**: Command-line parser only kept the last `-e` argument value.

```c
// ❌ Before: Single eval_arg variable (overwritten)
const char *eval_arg = NULL;
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-e") == 0) {
        eval_arg = argv[++i];  // Overwrites previous value
    }
}

// ✅ After: Array of all eval arguments
const char **eval_args = NULL;
int eval_count = 0;
// First pass: count -e arguments
// Second pass: collect all -e arguments into array
// Execute all in order in the same namespace
for (int i = 0; i < eval_count; i++) {
    eval_string_repl(eval_args[i], st);
}
```

**Lesson**: Support multiple invocations of the same flag for composable CLI usage.

#### 6. Reference Counting Consistency in eval_list
**Problem**: Mixed RC conventions - some paths returned AUTORELEASE'd values, others returned owned values.

```c
// ❌ Before: Inconsistent RC
if (sym_is(op, "for")) {
    return eval_for(list, env);  // Returns RC+1
}
if (sym_is(op, "list")) {
    return result ? AUTORELEASE(result) : NULL;  // Returns autoreleased
}
return head ? (RETAIN(head), head) : clj_nil();  // Returns RC+1

// ✅ After: Consistent RC+1 (owned value)
if (sym_is(op, "for")) {
    return eval_for(list, env);  // RC+1
}
if (sym_is(op, "list")) {
    return eval_list_function(list, env);  // RC+1
}
return head ? (RETAIN(head), head) : clj_nil();  // RC+1
```

**Why**: `eval_expr_simple` always calls `AUTORELEASE` on `eval_list`'s result. If `eval_list` returned an already-autoreleased value, it would be double-autoreleased.

**Lesson**: Establish a clear RC convention for function return values and enforce it consistently. In tiny-clj: eval functions return owned values (RC+1), caller manages release/autorelease.

### Implementation Checklist for Function Calls
- [ ] Symbol resolution (eval_symbol)
- [ ] Function type check (CLJ_FUNC)
- [ ] Argument counting (list_count must be correct!)
- [ ] Argument evaluation (eval_arg for each)
- [ ] Arity validation (argc == param_count)
- [ ] Parameter binding (by name for non-interned symbols)
- [ ] Body evaluation (eval_body_with_params)
- [ ] Result handling (proper RC management)

### Testing Strategy
```c
// Test user-defined functions
./tiny-clj-repl --no-core -e '(def add (fn [a b] (+ a b)))' -e '(add 1 2)'
// Expected: 3

// Test multiple parameters
./tiny-clj-repl --no-core -e '(def multiply (fn [x y] (* x y)))' -e '(multiply 3 4)'
// Expected: 12

// Test nested calls
./tiny-clj-repl --no-core -e '(def double (fn [n] (* n 2)))' -e '(double (+ 1 2))'
// Expected: 6
```

### Performance Considerations
- **Stack allocation** for small argument arrays (`args_stack[16]`)
- **Heap allocation** only for > 16 arguments
- **Name comparison** only as fallback after pointer comparison fails
- **Single-pass argument evaluation** without temporary copies

## 12. VSCode/Cursor Configuration Issues

### Problem: Build Task Fails with Exit Code 1
**Symptom**: `The preLaunchTask 'build' terminated with exit code 1.`

### Root Causes

#### 1. Invalid JSON in Configuration Files
**Problem**: Multiple root JSON objects in single file

```json
// ❌ Before: Invalid JSON (two separate objects)
{
  "version": "2.0.0",
  "tasks": [...]
}

{
  "version": "2.0.0", 
  "tasks": [...]
}

// ✅ After: Valid JSON (single merged object)
{
  "version": "2.0.0",
  "tasks": [
    // All tasks in one array
  ]
}
```

**Affected Files:**
- `.vscode/tasks.json`
- `.vscode/launch.json`

#### 2. Incorrect Build Directory Paths
**Problem**: Tasks referenced non-existent `build/` subdirectory

```json
// ❌ Before: Assumes out-of-source build
"command": "cmake --build build -j"
"program": "${workspaceFolder}/build/tiny-clj-repl"

// ✅ After: Use in-source build
"command": "make"
"program": "${workspaceFolder}/tiny-clj-repl"
```

**Reason**: This project uses in-source builds (CMake configured in workspace root), not out-of-source builds in separate `build/` directory.

### Fixed Configuration

#### `.vscode/tasks.json`
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "build",
      "type": "shell",
      "command": "make",
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": ["$gcc"],
      "group": { "kind": "build", "isDefault": true }
    },
    {
      "label": "ctest",
      "type": "shell",
      "command": "ctest --output-on-failure",
      "options": { "cwd": "${workspaceFolder}" }
    },
    {
      "label": "ctest: all",
      "type": "shell",
      "command": "ctest -j 4 --output-on-failure",
      "options": { "cwd": "${workspaceFolder}" },
      "group": { "kind": "test", "isDefault": true }
    }
  ]
}
```

#### `.vscode/launch.json`
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Tiny-CLJ REPL",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/tiny-clj-repl",
      "cwd": "${workspaceFolder}",
      "args": ["--repl"],
      "preLaunchTask": "build"
    },
    {
      "name": "Tiny-CLJ eval (-e)",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/tiny-clj-repl",
      "cwd": "${workspaceFolder}",
      "args": ["--no-core", "-e", "(+ 1 2)"]
    },
    {
      "name": "Tiny-CLJ test function",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/tiny-clj-repl",
      "cwd": "${workspaceFolder}",
      "args": ["--no-core", "-e", "(def add (fn [a b] (+ a b)))", "-e", "(add 1 2)"],
      "preLaunchTask": "build"
    }
  ]
}
```

### Verification
```bash
# Test build task
cd /Users/theisen/Projects/tiny-clj && make

# Test executable path
test -x tiny-clj-repl && echo "OK"

# Test evaluation
./tiny-clj-repl --no-core -e '(+ 1 2)'
# Expected: 3
```

### Best Practices for VSCode Configuration

1. **Always validate JSON** before committing config files
2. **Match build system** - tasks should match actual build commands
3. **Use workspace-relative paths** - `${workspaceFolder}/executable`
4. **Test configurations** - verify tasks run successfully
5. **Document build system** - in-source vs out-of-source builds

### Common Pitfalls

❌ **Don't**: Assume build directory structure without checking  
❌ **Don't**: Copy configurations from other projects without adaptation  
❌ **Don't**: Have multiple root JSON objects in config files  

✅ **Do**: Verify executable paths exist  
✅ **Do**: Test preLaunchTask independently  
✅ **Do**: Use valid JSON (single root object)

## 13. REPL Testing Best Practices

### Problem: Interactive REPL Hangs in Tests
**Symptom**: When testing REPL without `-e` flag, it waits for input and never returns.

### Root Cause
The REPL is designed for interactive use - it continuously reads input until EOF (Ctrl-D) or interrupt signal.

```c
// From repl.c - Interactive REPL loop
for (;;) {
    printf("> ");
    // Waits for input indefinitely
    platform_readline_nb(buf, sizeof(buf));
    // ...
}
```

### Solutions

#### ✅ Option 1: Use -e Flag for Automated Tests (Recommended)
```bash
# Non-interactive evaluation - exits automatically
./tiny-clj-repl --no-core -e '(+ 1 2)'
# Output: 3

# Multiple expressions in same namespace
./tiny-clj-repl --no-core -e '(def x 42)' -e 'x'
# Output: nil
#         42
```

**Advantages:**
- No timeout needed
- Clean exit code
- Composable with multiple `-e` flags
- Same namespace for all expressions

#### ✅ Option 2: Use timeout Command (Linux)
```bash
# Linux systems have timeout command
timeout 1s ./tiny-clj-repl <<< "(+ 1 2)"
```

#### ✅ Option 3: Background Process with Kill (macOS)
```bash
# macOS doesn't have timeout by default - use background process with kill
(./tiny-clj-repl -e '(+ 1 2)' & PID=$!; sleep 2; kill $PID 2>/dev/null; wait $PID 2>/dev/null) 2>&1 | head -20

# For interactive REPL testing on macOS
printf "(+ 1 2)\n" | (./tiny-clj-repl & PID=$!; sleep 2; kill $PID 2>/dev/null)
```

**How it works:**
- `&` runs REPL in background
- `sleep 2` waits 2 seconds
- `kill $PID` terminates the process
- `2>/dev/null` suppresses error messages

#### ✅ Option 4: Install GNU coreutils on macOS
```bash
# Install coreutils via Homebrew (only if timeout is frequently needed)
brew install coreutils

# Use gtimeout (GNU timeout)
echo "(+ 1 2)" | gtimeout 1s ./tiny-clj-repl --no-core
```

#### ✅ Option 5: Use Ctrl-D for Manual Testing
```bash
# Start REPL
./tiny-clj-repl --no-core

# Type expressions
> (+ 1 2)
3
> (def x 42)
nil
> x
42

# Exit with Ctrl-D (EOF)
```

#### ✅ Option 6: Pipe Input for Testing
```bash
# Single expression
echo "(+ 1 2)" | ./tiny-clj-repl --no-core

# Multiple expressions
printf "(+ 1 2)\n(* 3 4)\n" | ./tiny-clj-repl --no-core
```

### Testing Strategy Decision Tree

```
Need to test REPL?
├─ Automated test/script?
│  ├─ YES → Use -e flag (Option 1)
│  └─ NO → Continue
├─ Multiple related expressions?
│  ├─ YES → Use multiple -e flags (Option 1)
│  └─ NO → Continue
├─ On Linux?
│  ├─ YES → Can use timeout command (Option 2)
│  └─ NO → Continue
├─ On macOS?
│  ├─ gtimeout available? → Use gtimeout (Option 3)
│  └─ gtimeout not available → Use piped input (Option 5)
└─ Manual interactive testing → Use Ctrl-D to exit (Option 4)
```

### Anti-Patterns to Avoid

#### ❌ Don't: Start REPL without exit strategy
```bash
# This will hang forever in automated scripts
./tiny-clj-repl --no-core
```

#### ❌ Don't: Use timeout when -e is available
```bash
# Unnecessary complexity
timeout 1s ./tiny-clj-repl --no-core -e '(+ 1 2)'

# Simpler and cleaner
./tiny-clj-repl --no-core -e '(+ 1 2)'
```

#### ❌ Don't: Mix interactive and non-interactive testing
```bash
# Confusing - sometimes hangs, sometimes doesn't
if [ $INTERACTIVE ]; then
    ./tiny-clj-repl --no-core
else
    ./tiny-clj-repl --no-core -e '(+ 1 2)'
fi

# Better - consistent behavior
./tiny-clj-repl --no-core -e '(+ 1 2)'  # Always non-interactive
```

### Best Practices Summary

1. **Default to `-e` flag** for all automated testing
2. **Use multiple `-e` flags** for multi-step tests in same namespace
3. **Reserve interactive REPL** for manual exploration only
4. **Document timeout requirements** if interactive testing is necessary
5. **Provide clear exit instructions** (Ctrl-D) in manual test documentation
6. **Use `-e` for debugging** - Clean exception messages with file/line numbers

### Debugging with -e Flag

#### ✅ Recommended: Debug with -e
```bash
# Clear, immediate error with source location
./tiny-clj-repl -e '(add 1 2)'
# Output: EXCEPTION: RuntimeException: Undefined variable: add 
#         at /Users/.../src/function_call.c:552:0

# Test error conditions explicitly
./tiny-clj-repl --no-core -e '(/ 10 0)'
# Output: EXCEPTION: ArithmeticException: Division by zero: 10 / 0

# Verify function works after definition
./tiny-clj-repl -e '(def add (fn [a b] (+ a b)))' -e '(add 2 3)'
# Output: #<function>
#         5
```

#### ❌ Not Recommended: Debug interactively
```bash
# Hard to reproduce, requires manual input, timeout issues
./tiny-clj-repl
> (add 1 2)
# Exception appears but harder to capture/script
```

#### Benefits of -e for Debugging

1. **Clean Output** - Only errors and results, no prompt noise
2. **Reproducible** - Exact same command every time
3. **Scriptable** - Can be automated in test scripts
4. **No Timeout** - Exits cleanly without hanging
5. **Exit Codes** - Non-zero on error for CI/CD integration
6. **Source Location** - Shows exact file and line number in exception
7. **Fast Iteration** - Quick edit-run-debug cycle

#### Debugging Workflow Pattern

```bash
# 1. Reproduce the error
./tiny-clj-repl -e '(problematic-code)'

# 2. Isolate the issue with simpler expressions
./tiny-clj-repl --no-core -e '(+ 1 2)'  # Works?
./tiny-clj-repl --no-core -e '(def x 42)' -e 'x'  # Variables work?

# 3. Test the fix
./tiny-clj-repl -e '(fixed-code)'

# 4. Verify with multiple steps
./tiny-clj-repl -e '(setup)' -e '(test-code)' -e '(verify)'
```

#### Example: Debugging Undefined Variable

```bash
# Step 1: See the error
$ ./tiny-clj-repl -e '(add 1 2)'
EXCEPTION: RuntimeException: Undefined variable: add at .../function_call.c:552:0

# Step 2: Check if basic arithmetic works
$ ./tiny-clj-repl --no-core -e '(+ 1 2)'
3  # ✅ Works

# Step 3: Define and test
$ ./tiny-clj-repl --no-core -e '(def add (fn [a b] (+ a b)))' -e '(add 1 2)'
#<function>
3  # ✅ Works

# Step 4: Verify with clojure.core loaded
$ ./tiny-clj-repl -e '(def add (fn [a b] (+ a b)))' -e '(add 1 2)'
#<function>
5  # ✅ Works (different result because clojure.core loaded)
```

#### Integration with CI/CD

```bash
#!/bin/bash
# test_repl_errors.sh - Verify error handling

set -e  # Exit on first error

echo "Test 1: Undefined variable error..."
if ./tiny-clj-repl -e '(undefined-var)' 2>&1 | grep -q "Undefined variable"; then
    echo "✅ Correct error message"
else
    echo "❌ Expected undefined variable error"
    exit 1
fi

echo "Test 2: Division by zero..."
if ./tiny-clj-repl --no-core -e '(/ 10 0)' 2>&1 | grep -q "Division by zero"; then
    echo "✅ Correct error message"
else
    echo "❌ Expected division by zero error"
    exit 1
fi

echo "All error handling tests passed!"
```

### Example Test Suite Pattern

```bash
#!/bin/bash
# test_repl_functions.sh

echo "Testing basic arithmetic..."
./tiny-clj-repl --no-core -e '(+ 1 2)' || exit 1

echo "Testing function definition..."
./tiny-clj-repl --no-core -e '(def add (fn [a b] (+ a b)))' -e '(add 1 2)' || exit 1

echo "Testing nested functions..."
./tiny-clj-repl --no-core -e '(def double (fn [n] (* n 2)))' -e '(double (+ 1 2))' || exit 1

echo "All tests passed!"
```

**Key Points:**
- Clean exit codes for CI/CD integration
- No timeout needed
- Easy to debug individual test cases
- Composable and maintainable

## 15. Memory Management Macros haben eingebaute NULL-Checks

### ✅ Wichtige Regel: Explizite NULL-Checks vor Memory-Management-Makros sind unnötig

Die Memory-Management-Makros `RELEASE`, `RETAIN` und `AUTORELEASE` haben bereits eingebaute NULL-Checks. Explizite `if`-Statements vor diesen Makros sind unnötig und sollten entfernt werden.

#### ❌ Unnötige NULL-Checks
```c
// Unnötig - RELEASE hat bereits NULL-Check
if (obj) RELEASE(obj);

// Unnötig - RETAIN hat bereits NULL-Check  
if (result) RETAIN(result);

// Unnötig - AUTORELEASE hat bereits NULL-Check
if (value) AUTORELEASE(value);
```

#### ✅ Korrekte Verwendung
```c
// Direkt verwenden - Makros handhaben NULL automatisch
RELEASE(obj);
RETAIN(result);
AUTORELEASE(value);
```

#### Code-Cleanup-Beispiele
```c
// Vorher (unnötig):
if (body_result) {
    RELEASE(body_result);
}

// Nachher (sauber):
RELEASE(body_result);
```

#### Vorteile der direkten Verwendung
- **Sauberer Code** - Weniger Boilerplate
- **Konsistenz** - Einheitlicher Stil im gesamten Codebase
- **Weniger Fehlerquellen** - Keine vergessenen NULL-Checks
- **Bessere Lesbarkeit** - Fokus auf die eigentliche Logik

#### Implementierung in memory_hooks.h
```c
#define RELEASE(obj) ({ \
    CljObject* _tmp = (CljObject*)(obj); \
    memory_hook_trigger(MEMORY_HOOK_RELEASE, _tmp, 0); \
    release(_tmp); \
    _tmp; \
})
```

Die `release()` Funktion selbst hat bereits den NULL-Check, daher ist der explizite Check redundant.

#### Regel für zukünftige Entwicklung
**Immer direkt verwenden:**
- `RELEASE(obj)` statt `if (obj) RELEASE(obj)`
- `RETAIN(obj)` statt `if (obj) RETAIN(obj)`  
- `AUTORELEASE(obj)` statt `if (obj) AUTORELEASE(obj)`

Diese Regel macht den Code sauberer und konsistenter.

## 16. Konsequente Verwendung von is_type()

### ✅ Wichtige Regel: Immer is_type() verwenden statt direkte Typvergleiche

Verwende konsequent `is_type(obj, CLJ_TYPE)` statt direkte Typvergleiche wie `obj->type == CLJ_TYPE`. Dies macht den Code konsistenter und robuster.

#### ❌ Direkte Typvergleiche vermeiden
```c
// Unnötig - direkter Typvergleich
if (obj->type == CLJ_NIL) { ... }
if (obj->type == CLJ_INT) { ... }
if (obj->type == CLJ_SYMBOL) { ... }
```

#### ✅ Korrekte Verwendung mit is_type()
```c
// Konsistent - is_type() verwenden
if (is_type(obj, CLJ_NIL)) { ... }
if (is_type(obj, CLJ_INT)) { ... }
if (is_type(obj, CLJ_SYMBOL)) { ... }
```

#### Code-Cleanup-Beispiele
```c
// Vorher (direkt):
if (a && a->type == CLJ_NIL) {
    a = make_int(0);
}

// Nachher (konsistent):
if (a && is_type(a, CLJ_NIL)) {
    a = make_int(0);
}
```

#### Vorteile der is_type() Verwendung
- **Konsistenz** - Einheitlicher Stil im gesamten Codebase
- **Robustheit** - is_type() hat eingebaute NULL-Checks
- **Lesbarkeit** - Klarere Intent beim Typvergleich
- **Wartbarkeit** - Einheitliche API für alle Typvergleiche

#### Regel für zukünftige Entwicklung
**Immer verwenden:**
- `is_type(obj, CLJ_NIL)` statt `obj->type == CLJ_NIL`
- `is_type(obj, CLJ_INT)` statt `obj->type == CLJ_INT`
- `is_type(obj, CLJ_SYMBOL)` statt `obj->type == CLJ_SYMBOL`

Diese Regel macht den Code konsistenter und robuster.

## 17. NULL-Checks vor is_type() sind überflüssig

### ✅ Wichtige Regel: is_type() hat eingebaute NULL-Checks

Die `is_type()` Funktion hat bereits eingebaute NULL-Checks, daher sind explizite `if (obj && is_type(obj, TYPE))` Checks überflüssig.

#### ❌ Überflüssige NULL-Checks
```c
// Unnötig - is_type() hat bereits NULL-Check
if (obj && is_type(obj, CLJ_NIL)) { ... }
if (a && is_type(a, CLJ_INT)) { ... }
```

#### ✅ Korrekte Verwendung
```c
// Direkt verwenden - is_type() handhabt NULL automatisch
if (is_type(obj, CLJ_NIL)) { ... }
if (is_type(a, CLJ_INT)) { ... }
```

#### Code-Cleanup-Beispiele
```c
// Vorher (überflüssig):
if (a && is_type(a, CLJ_NIL)) {
    a = make_int(0);
}

// Nachher (sauber):
if (is_type(a, CLJ_NIL)) {
    a = make_int(0);
}
```

#### Vorteile der direkten Verwendung
- **Sauberer Code** - Weniger Boilerplate
- **Konsistenz** - Einheitlicher Stil im gesamten Codebase
- **Weniger Fehlerquellen** - Keine vergessenen NULL-Checks
- **Bessere Lesbarkeit** - Fokus auf die eigentliche Logik

#### Regel für zukünftige Entwicklung
**Immer direkt verwenden:**
- `is_type(obj, CLJ_NIL)` statt `if (obj && is_type(obj, CLJ_NIL))`
- `is_type(obj, CLJ_INT)` statt `if (obj && is_type(obj, CLJ_INT))`
- `is_type(obj, CLJ_SYMBOL)` statt `if (obj && is_type(obj, CLJ_SYMBOL))`

Diese Regel macht den Code sauberer und konsistenter.
