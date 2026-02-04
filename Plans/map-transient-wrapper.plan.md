 # Plan: Transient Map Wrapper Over Persistent Backing

 ## Goal
Make transient maps thin wrappers around a persistent backing map, mirroring the vector pattern:
- Read-only access goes through `CljPersistentMap` (backing).
- Mutations are only valid via `CljTransientMap`.
- `CljPersistentMap` uses COW optimizations.

## Scope
- `subjective-c/src/subjective-c/map.h`
- `subjective-c/src/map.c`
- Tiny-clj call sites using map transient/persistent types
- Tests for map semantics and transient behavior

## Plan (Updated)
1. **Audit current map types and API** ✅
   - Identified shared struct usage for persistent/transient and copy-based transient/persistent conversions.
   - Cataloged read/write APIs and COW behavior.

2. **Define new map structures** ✅
   - Added `CljTransientMap` wrapper with persistent `backing`.
   - Updated type helpers (`is_map`, `as_map`, `map_backing`) to route through backing.

3. **Implement/verify COW in persistent map** ✅
   - Retained existing COW semantics in `map_assoc`/`map_remove`.

4. **Refactor transient map API** ✅
   - `map_transient` returns wrapper; `map_persistent` returns backing.
   - `map_conj` now `void`; `map_dissoc` added for transient removal.

5. **Update dependent code** ✅
   - subjective-c: callbacks/hash/seq/to_string/memory updated to use `map_backing`.
   - tiny-clj: namespace/channel/event_loop/builtins/tests updated for wrapper usage.

6. **Adjust memory management** ✅
   - `CLJ_MAP_TRANSIENT` release path releases backing.

7. **Update tests** ✅
   - Updated map and meta tests for new tags/behavior.

8. **Run Unity tests** ✅
   - `./build/unit-tests`: 1291 tests, 0 failures, 4 ignored.
