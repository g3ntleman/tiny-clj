# Heap-Limit-Anhebungen – TODOs Leak-Suche

Wenn ein Test-Heap-Limit angehoben wird, hier eintragen: **Datei**, **alter Wert (Ziel)**, **neuer Wert**, **TODO**.

| Datei | Alter Wert (Ziel) | Neuer Wert | TODO |
|-------|-------------------|------------|------|
| `src/tests/test_arithmetic.c` | 200 | 200 | Erledigt: tearDown ruft `autorelease_pool_free()`; Limit 200 |
| `src/tests/test_core_functions.c` | 2048 | 4096 | Lazy-Seq/keep/mapcat über 2048; Limit 4096 für Gruppe |
