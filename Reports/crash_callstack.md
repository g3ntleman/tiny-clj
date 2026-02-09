# Crash Call-Stack

## 1) SIGABRT in autorelease (unter lldb)

```
Object has rc=0 but already 0 times in pool. Missing RETAIN before AUTORELEASE!
ASSERTION FAILED at memory.c:424
  1  autorelease
  2  call_function_with_args_and_context_vec + 844  <- AUTORELEASE(result)
  ...
```

**Ursache:** Doppel-AUTORELEASE: `native_next` gibt korrekt `AUTORELEASE(it)` zurück (MEMORY_POLICY: Caller bekommt pool-sichere Referenz). Im Eval-Pfad ruft aber der **direkte Builtin-Zweig** in `eval_function_call_from_vector` (Symbol → native_func, Zeile ~1302) erneut `AUTORELEASE(result)` auf → rc <= pool_count.

**Fix (erledigt):** In `eval_function_call_from_vector`: Im Builtin-Fast-Path `result` unverändert zurückgeben, kein zweites AUTORELEASE. Builtins liefern bereits pool-sichere Referenzen.

**Fix 2 (erledigt):** In `eval_function_call` (eval.c): Vor der Arg-Cleanup-Schleife `RETAIN(result)`, danach `RELEASE(result)`. So bleibt `result` am Leben, wenn es struktureller Tail eines Args ist (z. B. `(rest list)` → result = list->rest; Release von list würde sonst rest freigeben → rc=0).

---

## 2) SIGSEGV in native_next (noch offen)

## Letzter Lauf

```
0   unit-tests                          signal_handler + 96
1   libsystem_platform.dylib            _sigtramp + 56
2   unit-tests                          native_next + 564          <-- CRASH
3   unit-tests                          native_rest + 204
4   unit-tests                          eval_function_call + 428
5   unit-tests                          call_function_with_args_and_context_vec + 520
6   unit-tests                          eval_function_call_from_vector + 144
7   unit-tests                          eval_ast_call + 1948
8   unit-tests                          eval_body_with_params + 2672
9   unit-tests                          eval_body + 56
10  unit-tests                          eval_let + 1444
11  unit-tests                          eval_special_let + 48
12  unit-tests                          eval_ast_call + 784
...
24  unit-tests                          eval_core_source + 1588
25  unit-tests                          load_clojure_core + 648
26  unit-tests                          evalstate_reset + 100
27  unit-tests                          setUp + 532
```

**Kontext:** Crash tritt beim Laden von clojure.core (setUp) auf, während `canonicalize_expr_with_scope` / `eval_parsed` einen Ausdruck auswertet, der `rest`/`next` aufruft.

## Kritische Stelle

- **Symbol:** `native_next` (builtins.c, ab Zeile 781)
- **Aufrufer:** `native_rest` (builtins.c, Zeile 839)

Mögliche Ursachen in `native_next`:
1. `(CljObject *)collection` – ungültiger oder freigegebener `collection` (z. B. während Canonicalize)
2. `make_seq(collection)` – gibt Seq zurück, der auf `collection` zeigt; wenn `collection` danach invalide wird, crasht später `seq_next_inplace`/`seq_next`
3. `seq_next_inplace(&it)` → `as_seq(*seq_slot)` oder `seq_next()` – Dereferenzierung eines ungültigen Seq/Containers (z. B. `seq->iter.state.list.current` oder `seq->iter.container`)

## Nächste Schritte (genaue Zeile)

```bash
lldb ./build/unit-tests
(lldb) run
# Nach dem Crash:
(lldb) bt
(lldb) frame select 2
(lldb) list
```

Oder Adresse in Zeile umrechnen (Debug-Build, keine Stripping-Infos nötig):

```bash
cd /Users/theisen/Projects/Work/tiny-clj-feature
lldb -o "run" -o "bt" -o "frame info" -o "quit" -- ./build/unit-tests 2>&1 | head -80
```
