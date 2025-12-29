## Plan: `CLJException->object` als `uintptr_t`

Du willst `object` explizit als “Adresse-only” speichern, die nicht sicher dereferenzierbar ist. Das verhindert Ownership-Missverständnisse (und hilft, den Zombie-Fall sauber zu behandeln). Wichtig ist dabei: alle Stellen, die bisher `object` als `CljObject*` nutzen oder dereferenzieren, müssen auf “nur ausgeben/vergleichbar machen” umgestellt werden.

### Steps 5
1. Ändere das Feld `object` in `CLJException` von `CljObject*` auf `uintptr_t` in [subjective-c/exception.h](subjective-c/exception.h) und dokumentiere “address-only, do not dereference, not retained”.
2. Ersetze alle `NULL`-Initialisierungen durch `0` (`exc->object = 0;`, `.object = 0`) in [subjective-c/exception.c](subjective-c/exception.c) und [src/oom.c](src/oom.c).
3. Entferne Ownership im Zombie-Pfad: statt `ex->object = RETAIN(v)` nur die Adresse speichern (z.B. `(uintptr_t)v`) in [subjective-c/memory.c](subjective-c/memory.c#L195-L207).
4. Aktualisiere Exception-Printing: in [subjective-c/exception.c](subjective-c/exception.c) keine `CljObject*`-Deref/`clj_to_string(ex->object)` mehr; nur noch Adresse ausgeben (entweder `%p` mit Cast `(void*)(uintptr_t)ex->object` oder `<inttypes.h>` + `PRIxPTR`).
5. Passe Tests an: Null-Checks werden “== 0”, Pointer-Gleichheit wird “Adress-Gleichheit” (cast auf `uintptr_t`) in [src/tests/test_zombie.c](src/tests/test_zombie.c) und [subjective-c/tests/test_exceptions.c](subjective-c/tests/test_exceptions.c).

### Further Considerations 3
1. Unity-Assertions: vermeide Makros, die auf 32-bit `int` truncaten; nutze pointer-/integer-breite Vergleiche.
2. Druckformat: `%p` verlangt `void*`; ohne Cast ist das UB/Warnung. `PRIxPTR` ist oft sauberer.
3. Semantik klären: `uintptr_t` sollte wirklich “raw heap address” sein (keine tagged `ID`-Werte speichern).
