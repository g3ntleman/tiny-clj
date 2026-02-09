# Plan: clojure_core.c auflösen

**Ziel:** Die Datei `src/clojure_core.c` perspektivisch entfernen und die Logik in generische Module verschieben, sodass clojure.core wie jeder andere Namespace geladen wird.

## Aktueller Inhalt von clojure_core.c

| Bestandteil | Zeilen/Bereich | Funktion |
|-------------|----------------|----------|
| `g_clojure_core_last_form` | 29 | Crash-Diagnostik (volatile) |
| `g_core_quiet` | 33, 544 | Ausgabe unterdrücken |
| `getenv_int` | 34–42 | Hilfsfunktion für Debug-Env |
| `core_mem_*` (DEBUG) | 44–149 | Memory-Profiling beim Core-Load |
| `clojure_core_override` | 160 | Test/Override-Quelltext |
| `read_file_cstr_local` | 169–194 | Datei lesen (für load_clojure_repl) |
| `eval_core_source` | 203–447 | Source parsen + in `st->current_ns` auswerten |
| `load_clojure_core` | 448–541 | Core-Quelle holen, ns setzen, eval_core_source, Math-Alias, inc-Check |
| `clojure_core_set_quiet` | 544–548 | API |
| `clojure_core_set_source` | 550 | API (Override) |
| `load_clojure_repl` | 552–631 | Datei suchen, ns setzen, eval_core_source |

In `tiny_clj.h` deklariert, aber **nicht in clojure_core.c implementiert**:  
`call_clojure_core_function`, `get_clojure_core_namespace`, `cleanup_clojure_core` (vermutlich in builtins.c oder Stubs).

## Auflösungsstrategie

### 1. Generische „Source in Namespace auswerten“

- **`eval_core_source`** umbenennen/verschieben zu einer generischen Funktion, z. B.  
  **`eval_source_into_namespace(const char *src, size_t src_len, const char *source_name, EvalState *st)`**
- **Ort:** z. B. `namespace.c` (zusammen mit ns-Logik) oder neues Modul `ns_loader.c`.
- Voraussetzung: Caller setzt `st->current_ns` auf den Ziel-Namespace; die Funktion wertet nur aus (kein clojure.core-spezifischer Code mehr in dieser Funktion).

### 2. Generisches „Namespace aus Pfad/Resolver laden“

- Eine Funktion **`load_namespace_from_path(EvalState *st, const char *ns_name, const char *resolver_path)`**:
  - `embedded_source_map_init()` einmalig (oder beim ersten Bedarf)
  - `bytes = resolve_path_to_bytes(resolver_path)`
  - bei `bytes`: Source-View bauen, `evalstate_set_ns(st, ns_name)`, `st->current_ns = ns_find(ns_name)` (oder st->current_ns nach set_ns), dann `eval_source_into_namespace(...)`
  - Optional: Idempotenz wie bei Core (z. B. „bereits geladen“, wenn ein Marker-Symbol existiert)
- **`load_clojure_core(st)`** wird zum dünnen Wrapper:
  - `load_namespace_from_path(st, "clojure.core", "/libs/clojure/core.clj")`
  - danach: Math-Alias setzen (einziger Core-spezifischer Nachlauf)
  - optional: Verifikation „inc“ vorhanden (kann später gelockert werden)

### 3. load_clojure_repl vereinheitlichen

- **clojure.repl** ebenfalls über Resolver laden:
  - Entweder `/libs/clojure/repl.clj` in `embedded_sources` aufnehmen (wie core) und `load_namespace_from_path(st, "clojure.repl", "/libs/clojure/repl.clj")` aufrufen.
  - Oder bestehende Datei-Suche (libs/, ../libs/, …) in eine gemeinsame Hilfsfunktion „source bytes für ns_name“ auslagern, die zuerst `resolve_path_to_bytes("/libs/...")` probiert, dann Datei.
- Dann **`load_clojure_repl`** = `load_namespace_from_path(st, "clojure.repl", "/libs/clojure/repl.clj")` (+ ggf. Fallback Datei), ohne eigenes `eval_core_source`-Duplikat.

### 4. Verbleibende Teile von clojure_core.c verteilen

- **Crash-Diagnostik** (`g_clojure_core_last_form`): in `runtime.c` oder kleines `diagnostics.c`; nur bei Core-Load setzen (im generischen Loader bei `ns_name == "clojure.core"` oder über Callback).
- **g_core_quiet / clojure_core_set_quiet**: globales Flag z. B. in `runtime.c`; API in `tiny_clj.h` bleibt, Implementierung wandert.
- **clojure_core_set_source**: nur für Tests/Override; kann in `namespace.c` oder `ns_loader.c` als optionaler Override für einen Pfad (z. B. "/libs/clojure/core.clj") bleiben.
- **Debug/Profiling** (getenv_int, core_mem_print_*): in vorhandenes Memory-Profiling (z. B. `memory_profiler.c`) oder unter `#ifdef DEBUG` in den generischen Loader.
- **read_file_cstr_local**: bereits in anderen Modulen ähnlich vorhanden (z. B. builtins load-file); eine gemeinsame Hilfsfunktion in `file_utils.c` o. ä. nutzen oder dort hin verschieben.

### 5. Zielbild

- **Neue/erweiterte Dateien:**  
  - `namespace.c` (oder `ns_loader.c`): `eval_source_into_namespace`, `load_namespace_from_path`, optional `clojure_core_set_source`/Override-Logik.
- **clojure_core.c:** wird obsolet und kann entfernt werden.
- **tiny_clj.h:**  
  - `load_clojure_core` bleibt als dünner Wrapper (ruft `load_namespace_from_path(..., "clojure.core", "/libs/clojure/core.clj")` + Math-Alias).  
  - `load_clojure_repl` analog.  
  - `clojure_core_set_quiet` bleibt API, Implementierung in runtime/ns_loader.

## Reihenfolge (empfohlen)

1. `eval_core_source` nach `namespace.c` (oder `ns_loader.c`) verschieben, in `eval_source_into_namespace` umbenennen, alle Aufrufer anpassen.
2. `load_namespace_from_path` implementieren; `load_clojure_core` darauf umstellen (inkl. Math-Alias und optional inc-Check).
3. `load_clojure_repl` auf Resolver + `eval_source_into_namespace` umstellen (oder auf `load_namespace_from_path` mit Fallback Datei).
4. Crash-/Quiet-/Override-/Debug-Logik in andere Module verschieben.
5. `clojure_core.c` entfernen, Build/CMake anpassen.

**Fazit:** Ja, `clojure_core.c` kann aufgelöst werden, indem man eine generische „Namespace aus Source/Resolver laden“-Schicht einführt und Core/Repl nur noch als Aufrufer dieser Schicht nutzt.

## Umgesetzt (Variante B: gemeinsamer Loader)

- **load_namespace_from_bytes(st, ns_name, bytes, source_path)** in `builtins.c`/`builtins.h`: gemeinsame API für „Bytes + Pfad → Ziel-Ns setzen → eval_source_in_current_state → loaded = true → current_ns wiederherstellen“. Wird von Require und von load_clojure_core genutzt.
- **process_require_spec** baut weiterhin Pfad und ruft `resolve_path_to_bytes`; den Eval-Block ersetzt ein Aufruf **load_namespace_from_bytes(st, ns_name, bytes, source_path)**. Alias/Refer-Logik bleibt in process_require_spec.
- **load_clojure_core(st)**: `resolve_path_to_bytes("/libs/clojure/core.clj")`; bei Bytes → **load_namespace_from_bytes(st, "clojure.core", bytes, "/libs/clojure/core.clj")**. Bei Fehlschlag Fallback auf **clojure_core_override** (eval_core_source). Danach: Math-Alias, inc-Check.
- **require_namespace_by_name(st, ns_name)** bleibt als Convenience-API (ruft process_require_spec, der den Loader nutzt).
