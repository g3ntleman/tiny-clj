<!-- 77c51648-26f6-487a-8a3a-22eaac6948fc 1a98d476-035e-4ea9-bf38-b01240c3ae3b -->
# Refactor CLJ_UNKNOWN Usage + Empty Sequence Singleton Pattern

## Goal

1. Separate `CLJ_UNKNOWN` usage: keep it only for `nil`, replace other uses with specific types.
2. Replace Empty-SEQ-Marker (`CLJ_UNKNOWN`) with singleton pattern: use `is_singleton(container)` to detect empty sequences.
3. Make `empty_vector_singleton` externally available (like `empty_string_singleton`).

## Current Usage Analysis

1. **For `nil` (NULL)**: `if (!obj) return CLJ_UNKNOWN;` in `object.h:82`
2. **For empty sequences**: `iter->seq_type = CLJ_UNKNOWN;` in `seq.c` (multiple places)

- Should be replaced by checking if `container` is an empty collection singleton

3. **For TYPE_OF macros** (internal structures without real CljType):

- `TYPE_OF_SymbolEntry CLJ_UNKNOWN` - **NOT USED** (no `ALLOC(SymbolEntry, ...)` calls)
- `TYPE_OF_CljNamespace CLJ_UNKNOWN` - **NOT USED** (no `ALLOC(CljNamespace, ...)` calls)
- `TYPE_OF_EvalState CLJ_UNKNOWN` - **NOT USED** (no `ALLOC(EvalState, ...)` calls)

4. **For memory allocation**: `alloc(..., CLJ_UNKNOWN)` in `function.c:27` (for raw ID array)
5. **In `clj_type_name`**: `case CLJ_UNKNOWN: return "Unknown";`

## Implementation Plan

### Step 1: Make `empty_vector_singleton` externally available

- Add `extern CljPersistentVector* empty_vector_singleton;` to `vector.h`
- Export `clj_empty_vector_singleton` as `empty_vector_singleton` in `vector.c`
- Similar to `empty_string_singleton` pattern

### Step 2: Add new CljType values to `types.h`

- Add `CLJ_NIL = 0` (rename from `CLJ_UNKNOWN`)
- Add `CLJ_RAW_MEMORY = 32` (for raw memory allocations like ID arrays)
- **Note**: No `CLJ_EMPTY_SEQ` needed - we use singleton pattern instead

### Step 3: Update `types.c`

- Change `CLJ_UNKNOWN` case to `CLJ_NIL` returning "Nil"
- Add case for `CLJ_RAW_MEMORY` (return "RawMemory")

### Step 4: Update `object.h`

- Rename `CLJ_UNKNOWN` to `CLJ_NIL` in `TAG()` function
- Remove unused TYPE_OF macros (not used in ALLOC calls):
- `TYPE_OF_SymbolEntry` (can be removed or kept as CLJ_UNKNOWN for compatibility)
- `TYPE_OF_CljNamespace` (can be removed or kept as CLJ_UNKNOWN for compatibility)
- `TYPE_OF_EvalState` (can be removed or kept as CLJ_UNKNOWN for compatibility)

### Step 5: Update `seq.c` - Replace Empty-SEQ-Marker with Singleton Pattern

- Remove `iter->seq_type = CLJ_UNKNOWN;` assignments
- Update `seq_iter_init()`:
- For empty vectors: check `vec == empty_vector_singleton` or `vec->count == 0 && is_singleton((CljObject*)vec)`
- For empty lists: check `list == empty_list_singleton` or use existing `LIST_FIRST` check
- For empty strings: check `str == empty_string_singleton` (already done)
- Don't set `seq_type` for empty sequences (leave it as 0 or set to a valid type)
- Update `seq_iter_empty()`:
- Replace `seq_type == CLJ_UNKNOWN` checks with:
- `!iter->container` (nil)
- `is_singleton(iter->container)` AND empty check (count == 0 for vectors, etc.)
- Update `make_seq()`:
- Replace `heap_seq->iter.seq_type == CLJ_UNKNOWN` check with `seq_iter_empty(&heap_seq->iter)`

### Step 6: Update `builtins.c`

- Replace `if (iter.seq_type == CLJ_UNKNOWN)` with `seq_iter_empty(&iter)`

### Step 7: Update `function.c`

- Replace `alloc(..., CLJ_UNKNOWN)` with `alloc(..., CLJ_RAW_MEMORY)`

### Step 8: Update tests

- Replace `CLJ_UNKNOWN` with `CLJ_NIL` in tests that check for nil
- Update tests that check for empty sequences to use `seq_iter_empty()` or singleton checks

### Step 9: Code cleanup

- Remove unused TYPE_OF macros if they're not needed
- Verify all changes compile without warnings
- Run all tests to ensure everything works
- Clean up any temporary code or comments

## Files to Modify

1. `src/vector.h` - Export `empty_vector_singleton`
2. `src/vector.c` - Export `clj_empty_vector_singleton` as `empty_vector_singleton`
3. `src/types.h` - Add new enum values
4. `src/types.c` - Update clj_type_name() function
5. `src/object.h` - Update TAG() and optionally remove unused TYPE_OF macros
6. `src/seq.c` - Replace CLJ_UNKNOWN marker with singleton pattern
7. `src/builtins.c` - Replace CLJ_UNKNOWN checks with seq_iter_empty()
8. `src/function.c` - Replace CLJ_UNKNOWN with CLJ_RAW_MEMORY
9. Test files - Update test assertions

## Notes

- **JVM-Compatible**: `make_seq()` continues to return `NULL` (nil) for empty sequences, matching Clojure/JVM behavior
- **Singleton Pattern**: Empty sequences are detected by checking if `container`