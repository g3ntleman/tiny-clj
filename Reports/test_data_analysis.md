# For-Loop Test Data Analysis

## Overview
Analysis of the data and iteration counts used in the 5 For-Loop tests to understand why memory usage differs.

## Test Data Comparison

### 1. dotimes Basic Functionality
```
Data: Single integer (3)
Iterations: 3 (dotimes [i 3])
Memory: 16 bytes, 1 allocation
Structure: (dotimes [i 3] (println i))
```

### 2. doseq Basic Functionality  
```
Data: Vector with 3 integers [1, 2, 3]
Iterations: 3 (over vector elements)
Memory: 64 bytes, 4 allocations
Structure: (doseq [x [1 2 3]] (println x))
```

### 3. for Basic Functionality
```
Data: Vector with 3 integers [1, 2, 3] 
Iterations: 3 (over vector elements)
Memory: 64 bytes, 4 allocations
Structure: (for [x [1 2 3]] (println x))
```

### 4. dotimes with Variable Binding
```
Data: Single integer (5)
Iterations: 5 (dotimes [i 5])
Memory: 16 bytes, 1 allocation  
Structure: (dotimes [i 5] (println i))
```

### 5. for with Simple Expression
```
Data: Vector with 2 integers [1, 2]
Iterations: 2 (over vector elements)
Memory: 48 bytes, 3 allocations
Structure: (for [x [1 2]] (println x))
```

## Detailed Breakdown

| Test | Data Type | Data Size | Iterations | Peak Memory | Allocations | Memory per Iteration |
|------|-----------|-----------|------------|-------------|-------------|---------------------|
| dotimes Basic | int | 1 element | 3 | 16 bytes | 1 | 5.3 bytes |
| doseq Basic | vector | 3 elements | 3 | 64 bytes | 4 | 21.3 bytes |
| for Basic | vector | 3 elements | 3 | 64 bytes | 4 | 21.3 bytes |
| dotimes Variable | int | 1 element | 5 | 16 bytes | 1 | 3.2 bytes |
| for Simple | vector | 2 elements | 2 | 48 bytes | 3 | 24 bytes |

## Key Insights

### 1. **dotimes Tests (16 bytes)**
- **Simplest data structure**: Single integer
- **Minimal memory footprint**: 16 bytes regardless of iteration count
- **Consistent allocation pattern**: 1 allocation per test
- **Memory efficiency**: 3.2-5.3 bytes per iteration

### 2. **Vector-based Tests (48-64 bytes)**
- **Complex data structure**: Vector with multiple elements
- **Higher memory footprint**: 48-64 bytes
- **More allocations**: 3-4 allocations per test
- **Memory efficiency**: 21.3-24 bytes per iteration

### 3. **Memory Usage Patterns**

#### **dotimes (Counter-based):**
- **Fixed overhead**: ~16 bytes regardless of count
- **Single allocation**: Counter object
- **Linear scaling**: Memory per iteration decreases with more iterations

#### **doseq/for (Collection-based):**
- **Variable overhead**: 48-64 bytes depending on vector size
- **Multiple allocations**: Vector + binding + body + call objects
- **Fixed overhead**: Memory doesn't scale linearly with iterations

## Memory Efficiency Analysis

### **Per-Iteration Memory Cost:**
1. **dotimes Variable Binding**: 3.2 bytes/iteration (best)
2. **dotimes Basic**: 5.3 bytes/iteration  
3. **doseq Basic**: 21.3 bytes/iteration
4. **for Basic**: 21.3 bytes/iteration
5. **for Simple**: 24 bytes/iteration (worst)

### **Fixed Overhead Analysis:**
- **dotimes**: ~16 bytes fixed overhead
- **Vector-based**: ~48-64 bytes fixed overhead
- **Vector creation**: Additional memory for data storage

## Why Memory Usage Differs

### 1. **Data Structure Complexity**
```
dotimes: [i 3]           → Simple integer binding
doseq:   [x [1 2 3]]     → Vector with 3 elements  
for:     [x [1 2 3]]     → Vector with 3 elements
```

### 2. **Object Creation Overhead**
```
dotimes: 1 allocation  → Counter object only
doseq:   4 allocations → Vector + binding + body + call
for:     3-4 allocations → Vector + binding + body + call
```

### 3. **Vector Storage Requirements**
```
Vector [1,2,3]: 3 integers × 16 bytes = 48 bytes + overhead
Vector [1,2]:   2 integers × 16 bytes = 32 bytes + overhead
Integer 3:      16 bytes + overhead
```

## Recommendations

### **For Memory-Critical Applications:**
1. **Use dotimes** for simple counting (16 bytes fixed)
2. **Minimize vector sizes** in doseq/for loops
3. **Consider pre-allocated vectors** for repeated use

### **For Performance Optimization:**
1. **dotimes** scales better with more iterations
2. **Vector-based loops** have fixed overhead regardless of iterations
3. **Small vectors** (2-3 elements) are reasonable for most use cases

## Conclusion

The memory usage differences are **not due to the same data**, but rather:

1. **Different data structures**: Integers vs Vectors
2. **Different complexity levels**: Simple counters vs Collection iteration  
3. **Different object creation patterns**: 1 vs 3-4 allocations
4. **Different iteration counts**: 2-5 iterations

**dotimes** is most memory-efficient for simple counting, while **doseq/for** provide more functionality at a higher memory cost. The 16-64 byte range is reasonable for most applications, with **zero memory leaks** across all test types.
