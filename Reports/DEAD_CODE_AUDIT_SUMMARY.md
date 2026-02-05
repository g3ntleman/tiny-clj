# Dead Code Audit: Final Summary

**Generated:** 2026-02-05  
**Scope:** C-Core (`src/*.c`, excluding `src/tests/`, `external/`, `subjective-c/`)  
**Targets analyzed:** `tiny-clj-repl`, `unit-tests`

---

## Executive Summary

The **comprehensive dead-code audit** (linker stripping + coverage + call-site verification) found:

- **0 definitively safe-to-delete functions** in the C-Core for `tiny-clj-repl`.
- All "high-confidence" candidates (linker-stripped AND coverage-never-executed) have active call-sites in the codebase.

### Key Findings

1. **Linker-stripped candidates (Release build, `-dead_strip`):**
   - `tiny-clj-repl`: **191 symbols** eliminated by linker
   - `unit-tests`: **98 symbols** eliminated by linker

2. **Coverage-never-executed (unit-tests subset):**
   - **332 functions** never hit by the executed test patterns

3. **High-confidence intersection (stripped + never-executed):**
   - `tiny-clj-repl`: **83 candidates**
   - `unit-tests`: **47 candidates**

4. **Verification results (call-site checks via `rg`):**
   - `tiny-clj-repl`: **0 HIGH** (safe to delete), **83 FALSE POSITIVE** (all have call-sites)
   - This indicates: either (a) code paths are guarded by `#ifdef` (ESP32, feature flags), (b) error-handling branches not triggered in tests, or (c) incomplete test coverage.

---

## Interpretation

### Why are "stripped" functions still referenced?

Functions can be **defined in object files** but **stripped by the linker** if:
- They are **static** and unreachable within that compilation unit (intra-CU dead code).
- They are **extern** but never referenced by the chosen entry-points (cross-CU dead code).

However, our verification shows that **all** stripped symbols have call-sites in the source code. Possible explanations:
1. **`#ifdef`-guarded code paths** (e.g., `ESP32_BUILD`, `REPL_ENABLED=0`, `DEBUG`, `META_ENABLED`) compile differently in Release vs Debug → Release strips out Debug-only or embedded-only paths.
2. **Inlining/LTO aggressiveness**: Release builds with `-O3` + LTO inline or eliminate intermediate calls, but the *source* still references them.
3. **Test coverage gaps**: Some branches (error paths, rare input patterns) are not hit by the curated test subset.

### Why are "never-executed" functions still necessary?

Functions can be **present in the binary** but **never executed in tests** if:
- They handle **error conditions** not triggered by the test suite (e.g., `throw_index_out_of_bounds`, `parse_error`).
- They are part of **feature sets** not exercised by the subset (e.g., `mdns_*` functions, `fs_*` KV/global-store APIs).
- They are **REPL-only** or **embedded-only** paths (e.g., `clojure_core_set_quiet`, `fs_exists`).

---

## Recommendations

### 1. **No immediate deletions recommended**

All high-confidence candidates have active call-sites → **do not delete**.

### 2. **Consider feature-flag-based dead-code elimination**

If certain features are **never used** in production (e.g., mDNS, KV global-store), consider:
- Adding compile-time flags: `MDNS_ENABLED`, `GLOBAL_STORE_ENABLED`, `COMPILED_AST_ENABLED`.
- Guarding large feature blocks with `#if FEATURE_ENABLED`.
- This reduces binary size for embedded targets without removing code from the repository.

### 3. **Expand test coverage for error paths**

Many candidates are error-handling functions (e.g., `throw_index_out_of_bounds`, `parse_error`, `validate_arity`). Consider:
- Adding negative tests (invalid input, out-of-bounds access, arity mismatches).
- This improves both coverage metrics **and** code quality (catches regressions in error handling).

### 4. **Document feature-specific code paths**

Some candidates are clearly feature-specific (e.g., `mdns_*`, `fs_*`, `eval_compiled_*`). Consider:
- Adding comments: `// ESP32-only`, `// REPL-only`, `// Compiled AST (experimental)`.
- This helps future audits distinguish "intentionally unused in tests" from "genuinely dead".

### 5. **Re-run audit with broader test coverage**

The current audit used a **curated test subset** (to avoid timeouts). Consider:
- Running the full test suite with a longer timeout.
- Adding integration tests (REPL scripts, benchmark workloads).
- This will reduce false positives in the "never-executed" list.

---

## Generated Reports

All reports are in `Reports/`:

### Inventory
- `dead_code_inventory.md` / `.json`: CMake targets and source lists

### Linker Analysis
- `dead_code_linker_audit.md` / `.json`: Symbols stripped by Release `-dead_strip` linker

### Static Analysis
- `dead_code_warnings.md` / `.json`: All compiler warnings from `-Wpedantic -Wunused-*`
- `dead_code_warnings_unused.md` / `.json`: Filtered to unused-specific warnings only (found 0)

### Coverage
- `dead_code_coverage.md` / `.lcov`: Never-executed functions from `unit-tests` run
- `dead_code_coverage_candidates.json`: Structured list of 332 never-called core functions

### Combined
- `dead_code_candidates.md` / `.json`: Intersection (linker-stripped ∩ coverage-never-executed)

### Verification
- `dead_code_verified_tiny-clj-repl.md` / `.json`: Call-site verification of high-confidence candidates

---

## Next Steps (if pursuing size optimization)

1. **Measure impact of feature flags**: Compile with `MDNS_ENABLED=0`, `GLOBAL_STORE_ENABLED=0`, etc. → measure binary size delta.
2. **Profile production workloads**: Run typical use-cases (benchmarks, ESP32 scripts) under coverage → identify truly unused branches.
3. **Audit non-core areas**: Repeat for `subjective-c/`, `external/tiny-db/` if needed.
4. **Consider aggressive inlining**: For small, hot-path functions → reduce call overhead and binary size.

---

## Conclusion

The C-Core is **lean and actively used**. No definitively dead functions were found via automated heuristics. Further optimization requires:
- **Feature-based conditionals** (compile-time flags for optional features).
- **Expanded test coverage** (to distinguish "error path" from "dead code").
- **Manual review** of specific feature sets (mDNS, KV, compiled AST) to determine if they should be `#ifdef`-guarded.

---

## Appendix: Methodology

### Audit Pipeline

```
[CMakeLists.txt]
      ↓ (parse)
[Inventory: SOURCES + targets]
      ↓
[Release build: -dead_strip, -ffunction-sections]
      ↓ (nm diff: objects vs binary)
[Linker-stripped candidates]
      ↓
[Coverage build: -fprofile-instr-generate]
      ↓ (run unit-tests subset)
[LCOV export: never-executed functions]
      ↓ (intersection)
[High-confidence candidates]
      ↓ (rg call-site checks)
[Verified: HIGH / MEDIUM / FALSE_POSITIVE]
```

### Tools

- **CMake**: Build system introspection
- **nm**: Symbol table extraction
- **llvm-profdata / llvm-cov**: Coverage instrumentation
- **rg (ripgrep)**: Fast call-site searching
- **Python**: Orchestration and report generation

### Assumptions

- **Entry-points**: `tiny-clj-repl` and `unit-tests` are representative of "alive code" in development/production.
- **Test subset**: Due to timeouts, a curated subset was used (basic, parser, core, eval, symbols, etc.). Full suite may yield different coverage results.
- **Platform**: macOS (`-Wl,-dead_strip`). Linux (`--gc-sections`) behavior is similar but not tested here.

---

## Reproducibility

To re-run the audit:

```bash
# 1. Inventory
python3 scripts/dead_code_audit.py inventory

# 2. Linker analysis (Release build)
python3 scripts/dead_code_audit.py linker

# 3. Static analysis (warnings sweep)
python3 scripts/dead_code_audit.py warnings --filter unused

# 4. Coverage (instrumented build + unit-tests run)
python3 scripts/dead_code_audit.py coverage --test-timeout-seconds 180

# 5. Combine (intersection)
python3 scripts/dead_code_audit.py candidates

# 6. Verify (call-site checks)
python3 scripts/dead_code_audit.py verify --target tiny-clj-repl
```

All reports are written to `Reports/`.
