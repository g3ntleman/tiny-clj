# Plan: Clojure-Stacktrace im Debug-Mode

> **Constraint: Alles ausschließlich in `#ifdef DEBUG`-Blöcken.**
> Release-Builds bleiben unberührt — kein zusätzlicher Code, keine Felder, keine Laufzeitkosten.

## Status: Infrastruktur zu ~85% implementiert

### Was bereits existiert

| Komponente | Datei | Status |
|---|---|---|
| `CljCallStack` (festes 64-Frame C-Array) | `exception.h:121` | ✅ fertig |
| `g_clj_callstack` (thread-local) | `exception.c:102` | ✅ fertig |
| `clj_callstack_push/pop` Inline-Funktionen | `exception.h:131` | ✅ fertig |
| `clj_stacktrace_build()` Implementierung | `exception.c:108` | ✅ fertig |
| `saved_callstack_depth` in `ExceptionHandler` | `exception.h:98` | ✅ fertig |
| `_TRY_SAVE_CALLSTACK` / `_CATCH_RESTORE_CALLSTACK` | `exception.h:146` | ✅ fertig |
| Push/Pop in `eval_function_call` (natives + Clojure) | `eval.c:344,354,365,557` | ✅ fertig |
| `throw_exception` → `clj_stacktrace_build()` | `exception.c:307` | ✅ fertig |
| `print_exception` gibt Stacktrace aus | `exception.c:552` | ✅ fertig |
| `CLJException.stacktrace` Feld | `exception.h:38` | ✅ fertig |

---

## Frage 1: CljTransientVector vs. festes C-Array?

### Rechercheergebnis: Festes Array ist die richtige Wahl

**CljPersistentVector** (subjective-c/src/vector.c):
- Flaches Array mit flexiblem Member (`ID data[]`), kein Trie
- Wachstum: verdoppelt Capacity (`capacity * 2`, Minimum 4)
- Jedes Element ist ein `ID` (8 Byte Pointer auf Heap-Objekt)
- Allokation: `sizeof(CljPersistentVector) + capacity * 8` Bytes per Reallocation

**CljTransientVector**:
- Wraps ein `CljPersistentVector *backing`
- Mutation ist O(1) wenn RC == 1 und unter Capacity
- Aber: jedes Wachstum = `alloc()` + `memcpy`

**Zur ID-Kompatibilität der Symbole**:
- `CljSymbol` hat `CljObject base` als ersten Member → jeder `CljSymbol*` ist ein gültiges `ID`
- Symbole sind Singletons (`SINGLETON_RC`), d.h. `RETAIN` ist No-op und sie werden nie freigegeben
- Man könnte also `(ID)name_sym` direkt in einem `CljTransientVector` speichern — kein Boxing nötig

**Trotzdem: Festes Array bleibt die richtige Wahl**:
- 0 Heap-Allokationen vs. TV: O(1) solange unter Capacity, dann alloc+memcpy
- Bei `longjmp` reicht ein `depth = saved_depth` zum Restore; TV-Restore wäre aufwändiger
- 64 Frames deckt alle realistischen Fälle ab (darüber wirft tiny-clj ohnehin StackOverflow)
- Bereits korrekt implementiert

---

## Frage 2: Zentrale eval*-Stelle — ist das realistisch?

### eval_function_call (eval.c:333) ist bereits diese Stelle

```
eval_string()
  → parser
  → eval_parsed_value()
    → eval_body() / eval_body_with_params()
      → eval_function_call()  ← zentrale Dispatch-Stelle
          clj_callstack_push()        ← push
          native_func->fn(args, argc)  ← native Funktion
          ODER
          eval_body_with_params(body, ctx)  ← Clojure-Funktion mit TCO
          clj_callstack_pop()         ← pop (nach TCO-Loop)
```

**Special Forms** (`eval_let`, `eval_doseq`, `eval_fn`, etc.) passieren `eval_function_call` nicht — aber das ist korrekt. Ein Stacktrace zeigt Funktion-Frames, keine `let`-Blöcke.

**Für native Builtins** (CljCFunc): `name_sym` ist bei den meisten Builtins gesetzt. Einige anonyme native Funktionen erscheinen als `<anonymous>`.

---

## Identifizierte Lücken

### 1. `clj_stacktrace_build()` Buffer-Overflow-Risiko

```c
// exception.c:110
char buf[512];
```

Bei 64 Frames à ~20 Zeichen Funktionsname = bis zu 1.280 Zeichen.
Der Buffer kann truncated sein. Besser: dynamische Allokation oder Größe an `CLJ_CALLSTACK_MAX` anpassen.

```c
// Empfohlene Größe: CLJ_CALLSTACK_MAX * 40 Bytes (Frame-Nummer + Name + Newline)
char buf[CLJ_CALLSTACK_MAX * 40];  // = 2560 Bytes — immer noch stack-lokal, kein Heap
```

### 2. Anonyme Lambda-Funktionen in Stacktrace

Für `(fn [x] ...)` ohne Name wird `<anonymous>` angezeigt. Das ist akzeptabel, aber
ggf. könnte eine `eval_fn`-Heuristik einen synthetischen Namen wie `<lambda:file:line>` vergeben.

### 3. Tail-Call-Optimierung (TCO) und Stacktrace

Im TCO-Loop (`recur`) wird kein neuer Frame gepusht — korrekt. Ein einziger Frame bleibt
auf dem Callstack für die gesamte rekursive Ausführung.

### 4. Multi-Arity-Dispatch (CljMultiFn?)

Falls Multi-Arity-Dispatch existiert, prüfen ob der Dispatch-Wrapper auch `clj_callstack_push` aufruft.

---

## Implementierungsschritte (verbleibend)

### Schritt 1: Buffer-Größe in `clj_stacktrace_build()` korrigieren

**Datei**: `subjective-c/src/exception.c:110`
Bereits in `#ifdef DEBUG` — nur die Puffergröße anpassen:

```c
#ifdef DEBUG
struct CljString *clj_stacktrace_build(void) {
    if (g_clj_callstack.depth == 0) return NULL;
    // Vorher: char buf[512];
    char buf[CLJ_CALLSTACK_MAX * 40];  // 2560 Bytes, sicher für alle Frames
    ...
}
#endif
```

### Schritt 2: Anonyme Clojure-Funktionen mit Kontext-Namen versehen (optional)

In `eval_fn` (eval.c) kann nach dem Erzeugen der `CljFunction` ein synthetischer Name
vergeben werden, z.B. `<fn:namespace:line>`, falls `name_sym == NULL`.
Auch das nur in `#ifdef DEBUG` — `name_sym` bleibt in Release-Builds unverändert `NULL`.

Präzedenz: `fn` in `defn` bekommt bereits den Namen via `name_sym`. Nur echte anonyme `fn`-Formen sind betroffen.

### Schritt 3: Test

```c
// test_stacktrace.c (neu)
TRY {
    eval_string("(defn a [] (throw (Exception. \"boom\")))"
                "(defn b [] (a))"
                "(defn c [] (b))"
                "(c)", st);
} CATCH(ex) {
    assert(ex->stacktrace != NULL);
    assert(strstr(string_data(ex->stacktrace), "a") != NULL);
    assert(strstr(string_data(ex->stacktrace), "b") != NULL);
    assert(strstr(string_data(ex->stacktrace), "c") != NULL);
} END_TRY
```

---

## Nicht-Ziele

- **CljTransientVector als Callstack**: Unnötige Komplexität. Festes Array bleibt.
- **Expression-Level-Tracking** (jede `if`/`let`-Form): Zu granular für einen Stacktrace,
  verursacht Rauschen und Performance-Overhead.
- **Release-Build-Stacktraces**: Bewusst wegkompiliert (`#ifdef DEBUG`), bleibt so.

---

## Zusammenfassung

Die Feature-Implementierung ist praktisch abgeschlossen. Der einzige echte Bug ist der
512-Byte-Buffer in `clj_stacktrace_build()` der bei tiefen Callstacks truncated. Alle anderen
Punkte (TransientVector, zentrale Stelle) sind bereits korrekt gelöst.
