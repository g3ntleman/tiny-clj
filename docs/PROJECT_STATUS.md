# Project Status: tiny-clj

**Last Updated:** 2025-01-20  
**Test Status:** 664 Tests, 0 Failures ✅

## Overview

tiny-clj is an embedded-first Clojure interpreter written in pure C99/C11 for microcontrollers (ESP32, ARM Cortex-M) and desktop platforms.

## Implementation Status

### Core Language Features

| Feature | Status | Tests | Notes |
|---------|--------|-------|-------|
| `if`, `when`, `cond` | ✅ | 30+ | Conditional expressions |
| `let`, `let*` | ✅ | 33 | Lexical bindings, closures |
| `do` | ✅ | 10 | Sequential evaluation |
| `fn`, `defn` | ✅ | 25 | Function definition, multi-arity |
| `def` | ✅ | - | Global bindings |
| `loop`, `recur` | ✅ | 28 | Tail-call optimization |
| `while`, `dotimes`, `doseq` | ✅ | 13 | Loop constructs |
| `quote`, `'` | ✅ | - | Quoting |

### Data Types

| Type | Status | Notes |
|------|--------|-------|
| Integers (Fixnum) | ✅ | Tagged immediates, overflow to BigInt not implemented |
| Floats | ✅ | IEEE 754 double precision |
| Booleans | ✅ | `true`, `false` |
| `nil` | ✅ | |
| Characters | ✅ | UTF-8 support |
| Strings | ✅ | Immutable, UTF-8 |
| Symbols | ✅ | Interned, namespace-qualified |
| Keywords | ✅ | `:keyword` syntax |
| Lists | ✅ | Persistent linked lists |
| Vectors | ✅ | Persistent vectors |
| Maps | ✅ | 66 tests, persistent hash maps |
| Sets | ❌ | Not implemented |
| Regex | ❌ | Not implemented |

### Arithmetic & Comparison

| Feature | Status | Tests |
|---------|--------|-------|
| `+`, `-`, `*`, `/` | ✅ | 7 | Variadic, overflow handling |
| `mod`, `quot`, `rem` | ✅ | - |
| `<`, `>`, `<=`, `>=` | ✅ | - |
| `=`, `==`, `not=` | ✅ | 18 |
| `identical?` | ✅ | - |
| `bit-shift-left`, etc. | ✅ | - |

### Collections & Sequences

| Feature | Status | Tests |
|---------|--------|-------|
| `first`, `rest`, `next` | ✅ | 33 |
| `seq`, `cons`, `conj` | ✅ | - |
| `count`, `empty?` | ✅ | - |
| `nth`, `get` | ✅ | - |
| `assoc`, `dissoc` | ✅ | 66 |
| `merge`, `update` | ✅ | - |
| `keys`, `vals` | ✅ | - |
| `contains?`, `find` | ✅ | - |
| `into`, `select-keys` | ✅ | - |
| `vec`, `vector` | ✅ | - |
| `list`, `list?` | ✅ | 6 |
| `map?`, `vector?` | ✅ | - |

### Higher-Order Functions

| Feature | Status | Tests |
|---------|--------|-------|
| `map` | ✅ | 54 |
| `filter` | ✅ | - |
| `reduce` | ✅ | - |
| `apply` | ✅ | - |
| `partial` | ✅ | - |
| `comp` | ✅ | - |
| `take`, `drop` | ✅ | - |
| `concat` | ✅ | - |
| `reverse` | ✅ | - |
| `sort`, `sort-by` | ✅ | - |
| `range`, `repeat` | ✅ | - |

### Namespaces & Require

| Feature | Status | Tests |
|---------|--------|-------|
| `ns` | ✅ | 52 |
| `require` | ✅ | 18 |
| `:as` alias | ✅ | - |
| `:refer` | ✅ | - |
| `in-ns` | ✅ | - |
| `*ns*` | ✅ | - |
| Qualified symbols | ✅ | 21 |

### String Functions (clojure.string)

| Feature | Status | Notes |
|---------|--------|-------|
| `str` | ✅ | Concatenation |
| `subs` | ✅ | Substring |
| `upper-case` | ✅ | |
| `lower-case` | ✅ | |
| `trim` | ✅ | |
| `join` | ✅ | |
| `split` | ⚠️ | Works, but no regex support |
| `replace` | ⚠️ | String only, no regex |
| `reverse` | ✅ | |

### State & Concurrency

| Feature | Status | Tests |
|---------|--------|-------|
| `atom` | ✅ | 22 |
| `deref`, `@` | ✅ | - |
| `reset!` | ✅ | - |
| `swap!` | ✅ | - |
| `go` blocks | ✅ | 11 |
| Channels | ✅ | - |
| `schedule`, `cancel-timer` | ✅ | 13 |

### Exception Handling

| Feature | Status | Tests |
|---------|--------|-------|
| `try` | ✅ | 10 |
| `catch` | ✅ | - |
| `throw` | ✅ | - |
| `finally` | ✅ | - |
| Stack traces | ✅ | - |

### I/O

| Feature | Status | Tests |
|---------|--------|-------|
| `println`, `print` | ✅ | - |
| `pr`, `prn` | ✅ | - |
| `slurp` | ✅ | 18 |
| `spit` | ✅ | - |
| `*in*`, `*out*` | ❌ | Not implemented |

### Metadata

| Feature | Status | Tests |
|---------|--------|-------|
| `meta` | ✅ | 16 |
| `with-meta` | ✅ | - |
| `^` reader macro | ✅ | - |
| `:doc`, `:line` | ✅ | - |

### Miscellaneous

| Feature | Status | Notes |
|---------|--------|-------|
| `type` | ✅ | Returns type keyword |
| `class` | ❌ | No Java interop |
| `symbol`, `keyword` | ✅ | Constructors |
| `name`, `namespace` | ✅ | Symbol/keyword accessors |
| `gensym` | ❌ | Not implemented |
| `macroexpand` | ❌ | No macro system |
| `eval` | ❌ | Not implemented |

## Not Implemented

- **Macros** - No `defmacro`, no macro expansion
- **Regex** - No `#""` literals, no `re-find`, `re-seq`
- **Sets** - No `#{}` literals, no `clojure.set`
- **Multimethods** - No `defmulti`, `defmethod`
- **Protocols** - No `defprotocol`, `extend-type`
- **Records** - No `defrecord`
- **Java Interop** - No `.method`, `new`, `import`
- **Transducers** - Not implemented
- **Spec** - Not implemented

## Performance Benchmark

**fib(20) - 100 iterations** (as of 2025-01-20):

| System | Time | ms/iter | vs tiny-clj |
|--------|------|---------|-------------|
| Clojure/JVM (after warmup) | 7 ms | 0.07 ms | 104x faster |
| ClojureScript/Node.js | 8 ms | 0.08 ms | 88x faster |
| Python 3.14 | 93 ms | 0.93 ms | 8x faster |
| **tiny-clj** | 728 ms | 7.28 ms | baseline |

Note: JVM and V8 benefit from JIT compilation. tiny-clj is a pure interpreter.

## Test Categories

```
66 tests - Maps
54 tests - Sequences  
52 tests - Namespaces
33 tests - Seq operations
33 tests - Let bindings
30 tests - Basic operations
29 tests - Core functions
28 tests - Recur/loop
25 tests - REPL
25 tests - Parser
25 tests - Function definitions
22 tests - Atoms
21 tests - Qualified symbols
18 tests - Require
18 tests - File I/O
18 tests - Equality
16 tests - Metadata
14 tests - Call frames
13 tests - Timers
13 tests - Loops
13 tests - Keywords
11 tests - Go blocks
10 tests - Exceptions
10 tests - Do blocks
 8 tests - Fixed point
 7 tests - Time macro
 7 tests - Arithmetic
 6 tests - Symbols
 6 tests - List resolution
 4 tests - Memory leaks
 3 tests - Hashmap
 1 test  - UTF-8/Emoji
```

## Build Targets

| Target | Size | Platform |
|--------|------|----------|
| `tiny-clj-repl` | ~500 KB | macOS/Linux desktop |
| `tiny-clj-esp32` | ~150 KB | ESP32 microcontroller |
| `unit-tests` | ~1 MB | Test binary |

## Next Steps

1. **Performance** - Optimize evaluator (target: 2-3x speedup)
2. **Regex** - Basic regex support for `split`, `replace`
3. **Sets** - Implement `#{}` and `clojure.set`
4. **Documentation** - Improve inline docs and examples
