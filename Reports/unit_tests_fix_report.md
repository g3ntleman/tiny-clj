# Unit-Tests Fix-Report („fix next easy test“ bis Stand)

**Datum:** 2025-02-09

---

## Behobene Ursachen (Root Cause)

### 1. **shared_test_eval_body_vector** (Heap-Limit)
- **Ursache:** `TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT` war 300 Bytes, die Tests allokieren ~197 KB.
- **Fix:** In `src/tests/test_eval_body_vector.c` Limit auf **250000** gesetzt.
- **Ergebnis:** 3 Tests grün.

### 2. **test_keyword_evaluation** – „Unable to resolve symbol: get“
- **Ursache:** Nach `evalstate_reset(..., true)` hatte der **user**-Namespace eine leere Map; in Clojure ist clojure.core im user-Namespace sichtbar (implizites Refer).
- **Fix:** In `src/namespace.c` in `evalstate_reset()` nach dem Zurücksetzen von user die Mappings von **clojure.core in user** übernommen (`MAP_FOR_EACH` + `map_assoc_inplace`), wenn `load_core` true ist.
- **Zusatz:** `test_keyword_evaluation` aus `group_runs_without_core()` entfernt (in `unity_test_runner.c`), damit die Gruppe überhaupt Core lädt.
- **Ergebnis:** `colon_key_vs_double_colon_key_map_lookup` und andere Keyword-Tests, die `(get ...)` nutzen, sind grün. **Ausnahme:** `parser_resolves_keyword_alias` (erwartet Alias-Auflösung str → clojure.string im Parser – kein einfacher Fix).

### 3. **shared_test_string** (Heap-Limit + Runner)
- **Ursache 1:** Kein eigenes Heap-Limit für die Datei; Default für shared-Tests war 2048, String-Tests brauchen ~80 KB.
- **Fix 1:** In `src/tests/test_string.c` vor `#include "tests_common.h"` gesetzt: `#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 90000`.
- **Ursache 2:** Beim Lauf mit **Pattern** (z. B. `-test "shared_test_string/*"`) wurde `setUp()` einmal aufgerufen, während `g_current_test_entry` noch **NULL** war → Heap-Limit und load_core kamen nicht vom ersten Treffer.
- **Fix 2:** In `run_specific_test_impl()` (Pattern-Pfad) vor `setUp()` die **erste** zum Pattern passende Test-Entry setzen: `g_current_test_entry = &all_tests[i]`.
- **Ergebnis:** 7 Tests in shared_test_string grün.

---

## Noch fehlgeschlagene Gruppen (ohne einfachen Root-Cause-Fix)

| Gruppe | Beispiele / typische Meldung |
|--------|------------------------------|
| **shared_test_predicates** | hash_set_dedup_count, conj_set_duplicate_no_growth – „Expected TRUE Was FALSE“ (HashSet-Verhalten) |
| **test_atom_watch** | notify_watchers_calls_watcher – „Cannot call catch as a function“ (try/catch-Special-Form) |
| **test_edn_file_all_types** | edn_file_all_supported_types – Assertion (z. B. Zeile 218 oder Parser-Typen) |
| **test_embedded_sources** | embedded_sources_fallback |
| **test_file_io** | tiny_clj_fs_and_kv_bindings_smoke |
| **test_go_blocks** | 4 Tests (Channels/Event-Loop) |
| **test_keyword_evaluation** | parser_resolves_keyword_alias – „Expected 'clojure.string' Was 'str'“ (Alias-Auflösung im Parser) |
| **test_macros** | Mehrere unquote_splice_*-Tests |

Diese benötigen tieferen Code- oder Spezifikations-Check (Parser, Special Forms, HashSet, I/O, Go-Blocks), keine „one-liner“-Limits oder Refer-Fixes.

---

## Kurzfassung

- **Behoben (Ursache):** eval_body_vector (Heap-Limit), keyword/get (Refer clojure.core → user), string (Heap-Limit + g_current_test_entry vor setUp bei Pattern).
- **Grüne Gruppen:** 86 Gruppen mit „✓ Group“; viele weitere Einzeltests grün.
- **Noch rot:** u. a. predicates, atom_watch, edn, embedded_sources, file_io, go_blocks, parser_resolves_keyword_alias, macros – **nicht** alle mit einem einfachen Fix lösbar; weitere Analyse nötig.
