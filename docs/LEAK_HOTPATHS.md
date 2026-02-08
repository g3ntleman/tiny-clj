# Leak Hot Paths – Where to Look

When `scripts/leak_test.clj` shows linear growth (delta per eval), check these areas.

## 1. Resolve cache (`g_runtime.resolve_cache`)

- **Where:** `src/eval.c` – `resolve_cache_lookup_value`, `resolve_cache_store_value`
- **What:** Map `(ns_key -> (op -> resolved))`. Cached symbol resolution per namespace.
- **Growth:** Grows with each **new** `(namespace, symbol)` pair we resolve. Never shrinks; only invalidated on redefinition (`ns_invalidate_resolve_cache()` in `builtins.c`).
- **Leak risk:** Low if we only resolve the same symbols (e.g. `reduce`, `+`, `range`). Risk: unbounded growth if we keep resolving new symbols (e.g. gensyms, or many namespaces) and never invalidate.
- **Check:** After N evals of the same form, `resolve_cache` should have a bounded number of entries (one per (ns, symbol) ever resolved).

## 2. Callsite cache (per AST node)

- **Where:** `src/ast.c` – `make_callsite_cache`, `ast_node_set_callsite_cache`; `src/eval.c` – `ast_node_update_callsite_cache`, `ast_node_get_cached_resolution`
- **What:** Each **call-site** AST node can get a `CljCallsiteCache` (symbol → resolved) to avoid repeated resolution.
- **Lifecycle:** Cache is owned by the AST node. When the node is released (`release_object_deep` in `memory.c`), `node->callsite_cache` is released. So CallsiteCache only leaks if **AST nodes** are not released.
- **Leak risk:** If AST from parsed forms is never released (e.g. pool not draining, or something retaining the AST), then CallsiteCache and List/ASTNode counts grow with each eval.
- **Check:** Ensure each eval runs inside `WITH_AUTORELEASE_POOL` and that nothing (e.g. global, `EvalState`, result binding) retains the parsed/canonical AST after the form.

## 3. AST / List (parse + canonicalize)

- **Where:** `src/parser.c` – `parse_expr`, `parse_list`, `canonicalize_ast`; `src/eval.c` – `eval_parsed`, `eval_list`
- **What:** Every form creates new List/ASTNode trees (parse → canonicalize → eval). They should be autoreleased and released when the pool drains.
- **Leak risk:** High if the autorelease pool is not drained per form, or if any code path retains the AST (e.g. stores in a global, or in `EvalState`). Stats showing `List` / `ASTNode` with **dealloc-count 0** suggest these types are never released.
- **Check:** `eval_multiform_string` (repl.c) uses `WITH_AUTORELEASE_POOL({ ... })` per form; on block exit `autorelease_pool_drain_to_depth(mark)` runs. Verify no other path keeps a reference to the parsed/canon AST.

## 4. Lazy seq (`range`, `reduce`)

- **Where:** `src/builtins.c` – `native_range`, `native_reduce`; `src/seq.c` – seq iteration
- **What:** `(range 500)` creates a lazy seq (thunk + state). `(reduce + (range 500))` consumes it via `SeqIterator`; the seq and its cells are not retained by `native_reduce` beyond the call.
- **Leak risk:** Low if the lazy seq is only referenced from the caller’s expression (and thus in the same autorelease scope). Risk: if the seq or a thunk is stored somewhere (e.g. global, cache) or if a closure captures it.
- **Check:** Ensure `range` returns an autoreleased value and that `reduce` does not RETAIN the collection or any intermediate seq.

## 5. `(tiny-clj.runtime/stats)`

- **Where:** `src/builtins.c` – `native_tiny_clj_runtime_stats`
- **What:** Builds several maps and strings (`:memory-stats`, `:bytes-by-type`, per-type rows). Return value is autoreleased.
- **Leak risk:** If the caller or any intermediate code RETAINs the returned map (e.g. stores in a var that outlives the form), those maps/strings leak. Normal use `(get (get (stats) :memory-stats) :bytes-current)` does not retain the whole map if the result is not stored.
- **Check:** Call sites of `tiny-clj.runtime/stats` – ensure the result is not stored in a long-lived binding unless intentional.

## 6. Reduce accumulator

- **Where:** `src/builtins.c` – `native_reduce`
- **What:** `acc` is updated in the loop; when `acc_owned` we `RELEASE(acc)` before assigning `new_acc`. Final `acc` is returned (caller owns it).
- **Leak risk:** Low; ownership is consistent. Only risk would be missing `RELEASE(acc)` on an early return path.
- **Check:** All early returns and the loop body – every owned `acc` is either released or returned exactly once.

## Quick checks

- Run `scripts/leak_test.clj` and compare **delta reduce** and **delta stats** before/after changes.
- In stats, check **`:bytes-by-type`** for **Map**, **List**, **ASTNode**, **CallsiteCache**: if **alloc-count** grows with evals but **dealloc-count** stays 0 or much smaller, those types are likely leaking.
- Confirm REPL/file eval uses one `WITH_AUTORELEASE_POOL` per form and that drain runs on block exit (see `memory.h` – `WITH_AUTORELEASE_POOL` → `autorelease_pool_drain_to_depth(_restore)`).
