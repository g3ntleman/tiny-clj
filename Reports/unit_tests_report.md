# Unit-Tests Report

**Stand:** 2025-02-09  
**Quelle:** `./build/unit-tests --list` bzw. vollständiger Lauf `./build/unit-tests`

## Übersicht

| Metrik | Wert |
|--------|------|
| **Tests gesamt** | 1 303 |
| **Test-Gruppen** | 76 |
| **Bekannte Fehler (letzter Volllauf)** | viele (siehe Liste unten) |

## Behoben (nachhaltig)

- **shared_test_arithmetic/integer_overflow_detection**  
  tearDown ruft `autorelease_pool_free()`; Pool wuchs sonst über Tests (Exceptions ohne Drain). Heap-Limit 200.

- **shared_test_core_functions** (mapcat_expand, keep_filters_nil, …)  
  `test_core_functions.c`: `TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT` 4096 (Lazy-Seq/keep über 2048).

- **shared_test_predicates** (hash_set_dedup_count, conj_set_duplicate_no_growth)  
  `native_count` in `builtins.c`: Fall für `CLJ_HASHSET` ergänzt (`hashset_count`), damit `(count (hash-set 1 1 2))` ⇒ 2.

## Bekannte fehlschlagende Tests (letzter Volllauf)

1. **test_edn_file_all_types**  
   - `edn_file_all_supported_types` (Key-Lookup oder Typ-Assertion :float/:char)

2. **test_go_blocks** (4 Tests)  
   - go_enqueues_and_result_channel_receives_value, go_success_puts_value_high_level, channel_mutation_after_run_next_task, event_loop_run_next_mutates_channel

3. **shared_test_core_functions/frequencies_symbols**  
   - TypeError „Expected number“ (get/inc in frequencies); zusätzlich Heap > 4096.

4. Weitere: test_keyword_evaluation, test_macros (unquote_splice_*), test_qualified_symbol_resolution, test_repl (doc/pst/stacktrace), test_file_io (slurp/spit), test_rrd_scripts, test_atom_watch, test_special_forms (quasiquote), …

## Test-Gruppen (76)

```
shared_test_arithmetic, shared_test_core, shared_test_core_functions, shared_test_datetime,
shared_test_do, shared_test_equal, shared_test_eval_body_vector, shared_test_loops,
shared_test_predicates, shared_test_print, shared_test_regex, shared_test_seq,
shared_test_string, shared_test_time, shared_test_vector, shared_test_yield_sleep,
test_alias_lowlevel, test_atom, test_atom_watch, test_basics, test_builtins_destructure,
test_byte_array, test_byte_array_view, test_call_frame, test_clojure_core_loading,
test_closure_capture_minimal, test_compiled_ast, test_core, test_core_functions,
test_core_initialization, test_cow, test_datetime, test_defn, test_dynamic_binding,
test_edn_file_all_types, test_embedded_sources, test_eval_context, test_exception,
test_file_io, test_fixed_point, test_go_blocks, test_hashmap_highlevel, test_instant_uuid,
test_keyword_evaluation, test_let, test_let_performance, test_line_editor_serial,
test_list, test_list_resolution, test_macro_expander_debug, test_macros, test_map,
test_mdns_bindings, test_mdns_codec, test_mdns_resolver, test_memory,
test_memory_leak_fibonacci, test_memory_macros, test_meta, test_namespace, test_native_lookup,
test_parser, test_platform_mdns, test_platform_net, test_pprint, test_qualified_symbol_resolution,
test_recur, test_recursive_resolution, test_repl, test_require, test_require_perf,
test_rrd_scripts, test_runtime_stats, test_seq, test_sequences, test_slot_ref,
test_special_forms, test_static_keywords, test_string, test_symbol_clojure_compat,
test_threading_macros, test_time, test_timer, test_utf8_emoji, test_values
```

## Hinweise

- **Vollständiger Lauf:** Ein Aufruf von `./build/unit-tests` (alle Tests) endet derzeit mit **Exit-Code 138** (128+Signal), bevor die finale Zeile „X Tests Y Failures Z Ignored“ ausgegeben wird. Die obigen Fehler stammen aus der zuletzt erfassten Ausgabe.
- **Einzelne Gruppen prüfen:**  
  `./build/unit-tests --test "test_embedded_sources*"` → Exit 0 bei Erfolg, Exit 1 bei Fehlern (Test-Runner normalisiert seit Reparatur auf 0/1).
- **Testliste ausgeben:**  
  `./build/unit-tests --list`
