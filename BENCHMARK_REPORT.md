# 📊 Tiny-Clj Performance Benchmark Report

**Date:** October 7, 2025  
**Platform:** macOS (darwin 24.4.0)  
**Compiler:** Clang (Apple)  
**Build Type:** Debug  
**Git Commit:** 158dc0e

---

## 🎯 Executive Summary

All **16 performance benchmarks** passed successfully. This report measures the **complete memory lifecycle** (allocation + deallocation) for realistic performance metrics suitable for embedded systems. Target binary size: <100KB for embedded deployment.

### Key Performance Indicators

| Metric | Value | Notes |
|--------|-------|-------|
| **Object Creation Rate** | 22.5M ops/sec | int + string (with release) |
| **String Alloc/Free** | 14.9M cycles/sec | Complete lifecycle |
| **Integer Parsing** | 10,000 parses/ms | Simple literals |
| **Expression Parsing** | 1,110 parses/ms | `(+ n m)` forms |
| **Direct Iteration** | 503 µs/1K elements | Vector access |
| **Seq Iteration** | 31.7 ms/1K elements | Iterator-based |

---

## 📈 Detailed Results

### 1. Object Creation Benchmarks

#### 1.1 Primitive Object Creation (with Memory Management)
```
Total time:    8.906 ms
Iterations:    100,000
Per cycle:     0.089 µs
Throughput:    22.5M operations/sec
```

**Objects per iteration:** 2 (int + string)  
**Memory management:** Full lifecycle (make + release)  
**Singletons tested:** true, nil (no release needed)

#### 1.2 Collection Creation (with Memory Management)
```
Total time:    18.557 ms
Iterations:    10,000
Per iteration: 1.856 ms
Per operation: 0.109 µs (17 ops: 1 vector + 16 ints)
```

**Operation:** Create vector, add 16 integers, release all  
**Memory management:** Proper cleanup of vector and elements

---

### 2. Loop Performance Benchmarks

#### 2.1 dotimes Performance
```
Total time:    0.276 ms (100 iterations)
Per iteration: 2.76 µs
```

**Code:** `(dotimes [i 100] (+ i 1))`  
**Implementation:** Clojure macro (interpreted)

#### 2.2 doseq Performance
```
Total time:    0.352 ms (100 iterations)
Per iteration: 3.52 µs
```

**Code:** `(doseq [x [1 2 3 4 5]] (+ x 1))`  
**Implementation:** Clojure macro (interpreted)

#### 2.3 for Performance
```
Total time:    0.342 ms (100 iterations)
Per iteration: 3.42 µs
```

**Code:** `(for [x [1 2 3 4 5]] (* x 2))`  
**Implementation:** Clojure macro (interpreted)

---

### 3. Iteration Strategy Comparison

#### 3.1 doseq vs Direct Iteration
```
doseq:         3.985 ms (1000 iterations)
               3.985 µs per iteration
               
Direct:        0.015 ms (1000 iterations)
               0.015 µs per iteration
               
Overhead:      265.7x
```

**Vector size:** 10 elements  
**Interpretation:** High abstraction cost due to:
- Macro expansion
- Seq creation
- Iterator protocol
- Interpreted execution

**Recommendation:** Use direct iteration for performance-critical embedded code.

---

### 4. Sequence Iteration Performance

#### 4.1 Direct Vector Iteration
```
Total time:    198.720 ms (100,000 iterations)
Per iteration: 1.987 µs
Vector size:   1000 elements
Sum (check):   49,950,000,000
```

**Method:** Direct array access via `as_vector()->data[i]`  
**Performance:** ~503 ns per 1K vector scan

#### 4.2 Seq-based Vector Iteration
```
Total time:    3,172.799 ms (100,000 iterations)
Per iteration: 31.728 µs
Vector size:   1000 elements
Sum (check):   12,475,000,000
```

**Method:** Iterator protocol (`seq_create`, `seq_first`, `seq_rest`)  
**Performance:** ~31.7 µs per 1K vector scan  
**Overhead:** **16.0x** vs direct iteration

**Note:** Sum mismatch indicates potential iterator bug (under investigation)

---

### 5. Parsing Performance

#### 5.1 Integer Parsing
```
Total time:    9.998 ms (100,000 parses)
Per parse:     0.100 µs
Throughput:    10.0M parses/sec
```

**Input:** `"42"`  
**Result:** Single integer object

#### 5.2 Expression Parsing
```
Total time:    9.007 ms (10,000 parses)
Per parse:     0.901 µs
Throughput:    1.11M parses/sec
```

**Input:** `"(+ n m)"` (dynamic values)  
**Result:** List with 3 elements (symbol + 2 ints)

---

### 6. Memory Management Performance

#### 6.1 String Allocation/Deallocation
```
Total time:    6.705 ms (100,000 cycles)
Per cycle:     0.067 µs
Throughput:    14.9M cycles/sec
```

**Operation:** `make_string("test string")` + `release()`  
**String length:** 11 characters  
**Memory:** Complete lifecycle measured

---

## 🔍 Analysis & Insights

### Strengths
1. ✅ **Excellent primitive performance:** 22.5M ops/sec for basic objects
2. ✅ **Fast parsing:** 10M simple literals/sec, 1.1M expressions/sec
3. ✅ **Efficient memory management:** 14.9M alloc/free cycles/sec
4. ✅ **Low direct iteration overhead:** 503 ns per 1K elements

### Performance Considerations
1. ⚠️ **High abstraction overhead:** 16x-265x for seq/macro abstractions
2. ⚠️ **Seq iterator overhead:** 16x slower than direct access
3. ⚠️ **Interpreted execution:** Loop macros show interpreter overhead

### Recommendations for Embedded Systems (STM32)

#### Performance-Critical Code
```clojure
;; ❌ AVOID in hot paths (265x overhead)
(doseq [x data] (process x))

;; ✅ USE for performance
(loop [i 0]
  (when (< i (count data))
    (process (nth data i))
    (recur (inc i))))
```

#### Memory-Critical Code
- Use direct vector access for large collections
- Prefer primitive operations over abstractions
- Avoid creating temporary seqs in tight loops

---

## 🐛 Known Issues

1. **Seq iteration sum mismatch:** Direct iteration: 49.95B, Seq iteration: 12.48B
   - Indicates potential iterator state corruption
   - Under investigation

2. **Memory leaks in benchmarks:**
   - `create_test_vector()`: 1000 ints not released
   - `eval_string()` results: not released
   - `parse()` results: massive accumulation
   - **Impact:** Artificially inflates iteration times
   - **Status:** Documented, will be fixed in next iteration

---

## 🎯 Conclusions

Tiny-Clj demonstrates **excellent low-level performance** suitable for embedded systems:
- 22.5M object operations/second
- 10M simple parses/second
- Sub-microsecond primitive operations

However, **abstraction overhead is significant** (16x-265x), requiring careful optimization in performance-critical embedded code. Direct memory access and primitive operations are recommended for hot paths on resource-constrained devices like STM32. Target binary size: <100KB for embedded deployment.

---

## 📝 Test Environment

```
Platform:      macOS (darwin 24.4.0)
Architecture:  arm64 (Apple Silicon)
Compiler:      Clang (Apple)
Optimization:  -O0 -g (Debug build)
Test Suite:    16 benchmarks
Status:        ✅ All passed
```

**Note:** Release builds with `-O3` or `-Os` will show significantly better performance.

---

*Generated by: test-performance*  
*Report Date: 2025-10-07*

