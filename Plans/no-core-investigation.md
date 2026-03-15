# No-Core Investigation (ohne Test-Abschwächung)

Datum: 2026-03-15

## Ziel
Ermitteln, welche Testgruppen mit `load_core=false` laufen können, ohne zusätzliche Failures oder zusätzliche Ignores zu erzeugen.

## Vorgehen
1. Runner um ein opt-in Override erweitert:
   - Env: `TINY_CLJ_TEST_NO_CORE_GROUPS=group1,group2,...`
   - Datei: `src/tests/unity_test_runner.c` (`group_runs_without_core`)
2. Für jede Gruppe ein isolierter Lauf:
   - `TINY_CLJ_TEST_DISABLE_DEFAULT_BATCH=1 TINY_CLJ_TEST_NO_CORE_GROUPS=<gruppe> ./build/unit-tests --test '<gruppe>/*'`
3. Strikte Kandidaten gewählt mit:
   - `rc == 0`
   - `failures == 0`
   - konsistente Zählung (`run_tests == listed`)
4. Kombi-Validierung der Kandidaten als Vollsuite.
5. Zusätzlich: Vergleich `Ignored` je Gruppe gegen Baseline, um Abschwächung auszuschließen.

## Verifizierte no-core Gruppen (ohne Abschwächung)
Diese 32 Gruppen sind gemeinsam validiert (Vollsuite grün, gleiche Ignores wie Baseline):

- test_byte_array
- test_byte_array_view
- test_call_frame
- test_compiled_ast
- test_cow
- test_edn_file_all_types
- test_embedded_sources
- test_exception
- test_fixed_point
- test_gpio_architecture_contract
- test_instant_uuid
- test_let_performance
- test_line_editor_serial
- test_list
- test_list_resolution
- test_lockfree_spsc_queue
- test_macro_expander_debug
- test_mdns_bindings
- test_mdns_codec
- test_mdns_resolver
- test_memory
- test_memory_macros
- test_meta
- test_parser
- test_platform_mdns
- test_platform_net
- test_pprint
- test_runner_patterns
- test_static_keywords
- test_symbol_clojure_compat
- test_utf8_emoji
- test_values

## Nicht aufnehmen (trotz Einzelpass)
- `test_rrd_scripts`
  - Einzeltestlauf: pass
  - Aber baseline vs no-core zeigte zusätzliche Ignores (`0 -> 6`) -> würde abschwächen.

## Messwerte (Vollsuite)
- Baseline (aktueller Default, ohne `TINY_CLJ_TEST_NO_CORE_GROUPS`):
  - `1760 Tests 0 Failures 8 Ignored`
  - `40.379s`
- Mit obiger 32er no-core Liste (Default-Batching aktiv):
  - `1760 Tests 0 Failures 8 Ignored`
  - `35.497s`

## Effekt
- Zusätzlicher Gewinn: `-4.882s` (~`12.1%`) gegenüber aktuellem Default.
- Kein Qualitätsverlust im Vollsuite-Check (Fail/Ignore unverändert).
