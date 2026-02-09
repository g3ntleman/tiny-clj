# Heap-Limit-Anhebungen – TODOs Leak-Suche

Wenn ein Test-Heap-Limit angehoben wird, hier eintragen: **Datei**, **alter Wert (Ziel)**, **neuer Wert**, **TODO**.

| Datei | Alter Wert (Ziel) | Neuer Wert | TODO |
|-------|-------------------|------------|------|
| `src/tests/test_arithmetic.c` | 200 | 200 | Erledigt: tearDown ruft `autorelease_pool_free()`; Limit 200 |
