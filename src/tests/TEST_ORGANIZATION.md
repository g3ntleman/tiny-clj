# Test Organization

## Core Function Tests (`test_core.c`)

Consolidated tests for clojure.core functions:

### Collection Functions
- `test_core_count` - count function for collections
- `test_core_first` - first element access
- `test_core_rest` - rest of collection
- `test_core_conj` - conjoin element to collection
- `test_core_cons` - construct new list
- `test_core_nth` - nth element access

### Arithmetic Functions
- `test_core_inc` - increment by 1
- `test_core_dec` - decrement by 1
- `test_core_plus` - addition
- `test_core_minus` - subtraction
- `test_core_multiply` - multiplication

### Comparison Functions
- `test_core_equals` - equality check
- `test_core_not_equals` - inequality check
- `test_core_less_than` - less than comparison
- `test_core_greater_than` - greater than comparison

### Predicate Functions
- `test_core_nil_predicate` - nil? check
- `test_core_empty_predicate` - empty? check
- `test_core_vector_predicate` - vector? check
- `test_core_map_predicate` - map? check

### Map Functions
- `test_core_get` - get value from map
- `test_core_assoc` - associate key-value
- `test_core_dissoc` - dissociate key
- `test_core_keys` - get all keys
- `test_core_vals` - get all values

### Boolean Functions
- `test_core_not` - logical not

### Sequence Functions
- `test_core_map` - map function over collection
- `test_core_filter` - filter collection
- `test_core_reduce` - reduce collection
- `test_core_range` - generate range

## Removed Duplicates

### From `test_basics.c`
- `test_first_function` → moved to `test_core.c::test_core_first`
- `test_rest_function` → moved to `test_core.c::test_core_rest`
- `test_cons_function` → moved to `test_core.c::test_core_cons`
- `test_count_vector` → moved to `test_core.c::test_core_count`
- `test_count_list` → moved to `test_core.c::test_core_count`
- `test_count_string` → moved to `test_core.c::test_core_count`

### From `test_arithmetic.c`
- `test_simple_arithmetic` → moved to `test_core.c::test_core_plus`

## Test Organization Strategy

- **`test_core.c`**: Consolidated functional tests for clojure.core functions
- **`test_basics.c`**: Integration tests, system tests, special forms
- **`test_arithmetic.c`**: Edge cases for arithmetic (overflow, division by zero)
- **`test_map.c`**: Map implementation details and edge cases
- **`test_vector.c`**: Vector implementation details and edge cases
- **`test_string.c`**: clojure.string namespace functions
- **Other test files**: Specialized tests for specific subsystems

## Statistics

- Total tests after consolidation: 655 tests
- Tests removed as duplicates: 7 tests
- New consolidated tests in test_core.c: 33 tests




