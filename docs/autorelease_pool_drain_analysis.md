# Analyse: test_memory – Expected 1 Was 2

## Betroffene Tests

1. **test_nested_autorelease_pools** (Zeile 15, 16): `REFERENCE_COUNT(outer)` und `REFERENCE_COUNT(inner)` nach dem inneren `WITH_AUTORELEASE_POOL`-Block.
2. **test_autorelease_pool_drains_objects** (Zeile 52): `REFERENCE_COUNT(s)` nach dem `WITH_AUTORELEASE_POOL`-Block.

In allen Fällen: **erwartet 1, tatsächlich 2**.

---

## Ablauf (erwartet)

1. `make_string("…")` → rc = 1  
2. `RETAIN(…)` → rc = 2  
3. `AUTORELEASE(…)` → Objekt wird in den Pool gelegt (rc unverändert 2)  
4. Ende von `WITH_AUTORELEASE_POOL` → **Drain** sollte jedes Pool-Objekt mit `RELEASE` versehen → rc 2→1  
5. Assert: `REFERENCE_COUNT == 1`

---

## Ursache: `drain_to_depth` ist bei „Tiefe 1“ ein No-Op

### `autorelease_pool_depth()` (memory.c:350)

```c
uint32_t autorelease_pool_depth(void) { return g_pool ? 1u : 0u; }
```

- `0`: Pool existiert noch nicht (`g_pool == NULL`)
- `1`: Pool existiert

Es wird **keine echte Verschachtelung** gezählt, nur „Pool da / nicht da“.

### `autorelease_pool_drain_to_depth` (memory.c:352–362)

```c
void autorelease_pool_drain_to_depth(uint32_t d) {
    if (d == 0 && g_pool) {
        VECTOR_FOR_EACH(g_pool, elem) { RELEASE(elem); }
        vector_clear(g_pool);
    }
}
```

Drain passiert **nur**, wenn `d == 0`. Bei `d == 1` passiert **nichts**.

### `WITH_AUTORELEASE_POOL` (memory.h:311–315)

```c
#define WITH_AUTORELEASE_POOL(code) do { \
    uint32_t pool_restore_depth = autorelease_pool_depth(); \
    if (pool_restore_depth == 0) autorelease_pool_ensure_active(); \
    code; \
    autorelease_pool_drain_to_depth(pool_restore_depth); \
} while(0)
```

- **Eintritt:** `pool_restore_depth = autorelease_pool_depth()`
- **Austritt:** `autorelease_pool_drain_to_depth(pool_restore_depth)`

---

## Wann wird überhaupt gedraint?

| Situation                           | `autorelease_pool_depth()` | `drain_to_depth(?)` | Drain? |
|------------------------------------|----------------------------|----------------------|--------|
| Erstes `WITH_AUTORELEASE_POOL`     | 0 (noch kein Pool)         | `drain_to_depth(0)`  | ja     |
| Jedes weitere `WITH_AUTORELEASE_POOL` | 1 (Pool existiert)      | `drain_to_depth(1)`  | nein   |

Konsequenz: **Nur das allererste `WITH_AUTORELEASE_POOL` im Prozess führt einen Drain aus.** Sobald `g_pool` einmal existiert, liefert `autorelease_pool_depth()` 1, und `drain_to_depth(1)` ist ein No-Op.

---

## Warum die Tests fehlschlagen

- Jeder Test läuft in `SUBJECTIVE_C_TEST_WITH_POOL` → äußeres `WITH_AUTORELEASE_POOL`.
- `test_memory` wird nach z.B. `test_strings` oder `test_vectors` ausgeführt → **der Pool existiert bereits**.
- Beim Eintritt in den Test: `autorelease_pool_depth() == 1` → `pool_restore_depth = 1`.
- Am Ende: `drain_to_depth(1)` → Bedingung `d == 0` ist falsch → **kein Drain**.
- Die per `AUTORELEASE` hinzugefügten Objekte bleiben im Pool, rc bleibt 2 → Assert „Expected 1 Was 2“.

`test_nested_autorelease_pools` hat ein **weiteres** `WITH_AUTORELEASE_POOL` im Testkörper. Auch dieses bekommt `pool_restore_depth == 1` und ruft `drain_to_depth(1)` auf → wieder kein Drain. Die Objekte aus dem inneren Block werden erst gedraint, wenn irgendwann `drain_to_depth(0)` läuft (z.B. beim äußersten Rahmen), also **nach** den Assertions.

---

## Nötige Änderung

Die Pool-Logik muss **verschachtelte** `WITH_AUTORELEASE_POOL`-Rahmen abbilden:

1. **Tiefe / „Marken“:**  
   - `g_pool_depth`: aktuelle Verschachtelungstiefe (0 = kein aktiver Block).  
   - `g_pool_marks[]`: bei Eintritt in einen Block wird die aktuelle `vector_count(g_pool)` als Marke für diese Tiefe gespeichert.

2. **`autorelease_pool_depth()`:**  
   - Soll `g_pool_depth` zurückgeben (bzw. 0, wenn `!g_pool`).

3. **`autorelease_pool_push()` (neu):**  
   - Wenn `g_pool_depth == 0`: `autorelease_pool_ensure_active()`.  
   - `g_pool_marks[g_pool_depth] = vector_count(g_pool)`.  
   - `g_pool_depth++`.

4. **`autorelease_pool_drain_to_depth(restore)`:**  
   - Solange `g_pool_depth > restore`:  
     - `mark = g_pool_marks[g_pool_depth - 1]`  
     - Alle Elemente von `mark` bis `vector_count(g_pool)-1` mit `RELEASE(elem)` versehen.  
     - Pool auf Länge `mark` zurückschneiden (`vector_truncate` o.ä.).  
     - `g_pool_depth--`.

5. **`WITH_AUTORELEASE_POOL`:**  
   - `restore = autorelease_pool_depth()`  
   - `if (restore == 0) autorelease_pool_ensure_active()` (kann ggf. in `push` wandern)  
   - **`autorelease_pool_push()`** (neu)  
   - `code`  
   - `autorelease_pool_drain_to_depth(restore)`

6. **`vector_truncate(v, n)` (oder vergleichbar):**  
   - In `vector.c`: setzt `v->count = n` (mit `n <= v->count`).  
   - Für den WEAK-Pool: `RELEASE` der betroffenen Elemente geschieht im Drain, nicht in `truncate`.

Dann wird bei **jedem** Verlassen eines `WITH_AUTORELEASE_POOL`-Blocks nur der in diesem Block hinzugefügte Teil des Pools gedraint; die Tests „Expected 1 Was 2“ wären damit behoben.
