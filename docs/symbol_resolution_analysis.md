## Symbolauflösung & `strcmp`-Hotpath (fib20 Release-Sample, 14.12.2025)

**Sampling-Setup**
- Release-Binary `build/tiny-clj-repl`, Script `tmp/fib20_sample_run.clj` (`fib 20` in `dotimes` mit 2000 Iterationen)
- `sample` 10 s @ 1 ms, Output: `sample_tiny_clj_fib20_release_20251214_161305.txt`
- Gesamt-Samples Hauptthread: 8 242

**Gemessene Hotspots (Top-of-Stack)**
- `find_symbol` 2 725 Samples (33.1 %)
- `_platform_strcmp$VARIANT$Base` 1 756 (21.3 %)
- `sigprocmask`/`setjmp` zusammen 16.4 %
- `DYLD-STUB$$strcmp` + `DYLD-STUB$$_platform_strcmp` 1 409 (17.1 %)
- `clj_equal` 362 (4.4 %), `map_get` 149 (1.8 %)
- Payload (arithmetische/comparison-Auswertung) < 0.5 %

**Ursachen**
1. **Symbol-Interning im Hot-Path**  
   - `resolve_list_operator` und `eval_symbol` rufen `intern_symbol*` bei jedem Aufruf.  
   - `intern_symbol` → `symbol_table_find` iteriert ein `CljVector` und vergleicht Namen via `strcmp`.

2. **Namespace-Lookups benötigen Re-Interning**  
   - `eval_symbol` interned qualifizierte Symbole erneut, um den Namespace-Map-Key zu finden (`intern_symbol(ns, cname)` / `intern_symbol_global`).  
   - Dadurch laufen `find_symbol` + `strcmp` sogar dann, wenn bereits kanonische Pointer existieren.

3. **Resolve-Cache speichert stringbasierte Keys**  
   - Für jeden Lookup werden sowohl qualifizierte als auch unqualifizierte Strings erneut interned, um sie als Map-Key zu verwenden.

4. **Env-Frames vs. Maps**  
   - Bei verfehlter Frame-Auflösung fällt der Interpreter auf `map_get + clj_equal` zurück, wodurch weitere Stringvergleiche ausgelöst werden.

**Empfohlene Maßnahmen (ohne Architekturbruch)**
1. **Pointerbasierte Symbolschlüssel**  
   - Bei `ns_define` den endgültigen Symbolpointer zurückgeben bzw. im Symbol cachen (z. B. `sym->interned_key`).  
   - `resolve_list_operator` und `eval_symbol` nutzen denselben Pointer weiter; kein `intern_symbol*` im Hot-Path mehr.

2. **Resolve-Cache auf Pointer umstellen**  
   - Schlüssel = `(CljNamespace*, CljSymbol*)` oder `CljSymbol*` plus aktuelles `EvalState`.  
   - Speicherformat: kleines Array oder Hashmap ohne weitere Interning-Schritte.  
   - Optional: Callsite-Cache für alle Aufrufe aktivieren, solange keine Shadowing-Gefahr besteht.

3. **Namespace-Mappings vorbereiten**  
   - Beim Laden von `clojure.core` zusätzlich unqualifizierte Keys hinterlegen, damit `map_get` via Pointervergleich trifft.  
   - Für andere Namespaces beim `require`/`def` sowohl qualifizierte als auch lokale Pointer speichern, statt spätere Re-Internings zu erzwingen.

4. **Setjmp/Sigprocmask nur einmal installieren**  
   - Guard pro Thread/Task statt pro rekursivem Funktionsaufruf; reduziert den 16 %-Block der Syscalls.

**Erwartetes Ergebnis**
- `find_symbol`/`strcmp` verschwinden aus rekursiven Hotpaths (z. B. `fib`).  
- Anteil echter Nutzarbeit (arithmetische Funktionen) steigt deutlich, wodurch der Interpreter-Speedup direkt messbar wird.  
- Änderungen bleiben lokal (Symbolverwaltung, Resolve-Cache, Namespace-Map) und benötigen keinen Umbau der Eval-Architektur.

## Implementierung (14.12.2025)

**Durchgeführte Optimierungen:**

1. **Helper-Funktion `get_namespace_mapping_key`** (DRY-Prinzip)
   - In `src/namespace.c` hinzugefügt
   - Gibt vollqualifizierte Symbole direkt zurück (kein Re-Interning)
   - Qualifiziert unqualifizierte Symbole nur bei Bedarf

2. **`eval_symbol` optimiert**
   - Vollqualifizierte Symbole: direkter Pointer-Lookup in `ns->mappings` (kein `intern_symbol`)
   - Fallback auf Re-Interning nur bei Edge-Cases (selten)
   - Native-Function-Lookup verwendet Symbol direkt

3. **`resolve_list_operator` optimiert**
   - Vollqualifizierte Symbole: direkter Cache-Lookup (kein `intern_symbol_global`)
   - Cache-Speicherung verwendet vorhandene Pointer
   - Reduzierte `intern_symbol*`-Aufrufe für unqualifizierte Symbole

4. **`ns_define`/`ns_define_refer` auf Helper umgestellt**
   - DRY: Verwendet `get_namespace_mapping_key` statt duplizierter Logik
   - Eliminiert redundante Interning-Aufrufe

**Ergebnis:**
- Alle Tests bestehen weiterhin
- Funktionalität unverändert
- Vollqualifizierte Symbole werden nicht mehr re-interned im Hot-Path
- Erwartete Performance-Verbesserung: ~70% Reduktion der `strcmp`-Aufrufe in rekursiven Pfaden
