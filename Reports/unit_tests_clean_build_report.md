# Unit-Tests Report (Clean Build)

**Datum:** 2025-02-09  
**Build:** Clean (Build-Verzeichnis gelöscht, CMake neu konfiguriert, `cmake --build build --target unit-tests`)

---

## Clean Build

- **Konfiguration:** `cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug`
- **Ergebnis:** Erfolgreich, keine Compiler-Warnungen
- **Target:** `unit-tests` (ca. 12 s Build-Zeit)
- **Subjective-C:** Debug, ZOMBIE_ENABLED

---

## Unit-Tests Gesamtlauf

- **Befehl:** `./build/unit-tests` (alle Tests, quiet)
- **Exit-Code:** **137** (SIGKILL – Prozess wurde von außen beendet, z. B. Timeout oder OOM)
- **Anzahl gelisteter Tests:** ~1300 (aus `./build/unit-tests --list`)
- **Beobachtete Fehler im Lauf:** **59** FAIL-Zeilen, danach Abbruch (keine Unity-Zusammenfassung „X Tests Y Failures“, da Prozess beendet)

### Fehlgeschlagene Test-Gruppen (aus Log)

| Gruppe | Typische Ursache |
|--------|-------------------|
| `test_closure_capture_minimal` | „Unable to resolve symbol: n in this context“ (Closure/Loop-Scope) |
| `test_edn_file_all_types` | Datei/EDN oder Umgebung |
| `shared_test_eval_body_vector` | Eval/Env-Stack |
| `test_go_blocks` | Go-Blocks / Channels / Event-Loop |
| `shared_test_arithmetic` | Arithmetik (Overflow, Division, quot, sqrt, …) |
| `test_keyword_evaluation` | „Unable to resolve symbol: get“, Parser/Keyword-Alias |
| `shared_test_core` | Core-Funktionen (count, first, rest, conj, get, map, …) – **get** und andere Symbole nicht aufgelöst |
| `test_macros` | Unquote-Splice-Makros |

### Häufige Fehlermeldung

- **„Unable to resolve symbol: X in this context“** (z. B. `n`, `get`) → deutet darauf hin, dass **clojure.core** bzw. die Test-Umgebung für viele Gruppen nicht geladen bzw. nicht im Scope ist (setUp/Namespace/load_clojure_core).

---

## Kurzfassung

| Item | Status |
|------|--------|
| Clean Build | OK, keine Warnungen |
| Unit-Tests vollständiger Lauf | Abbruch mit Exit 137; 59 sichtbare Fehler in 8 Gruppen |
| Wahrscheinliche Hauptursache der Failures | Fehlende oder unvollständige Core-/Namespace-Umgebung („Unable to resolve symbol“) |
| Exit 137 | Prozess von außen beendet (SIGKILL), kein normaler Test-Abschluss |

---

## Empfehlungen

1. **Exit 137:** Prüfen, ob ein Timeout oder Ressourcenlimit (Speicher) den Prozess beendet; ggf. Timeout erhöhen oder ohne Limit laufen lassen.
2. **„Unable to resolve symbol“:** Erstes Failure in `test_closure_capture_minimal/closure_capture_loop_value` (Symbol `n`). Weitere in `test_keyword_evaluation` und `shared_test_core` (z. B. `get`). Setup/load_clojure_core und Reihenfolge der Tests prüfen.
3. **Einzelne Gruppen prüfen:** z. B. `./build/unit-tests -test "shared_test_seq/*"` (laut vorherigem Stand: 34 Tests, 0 Failures) oder andere Gruppen mit `-test "Gruppenname/*"`.
