# Macro-Expansion Performance-Analyse

## Zusammenfassung

**Wichtig:** Macro-Expansion findet zur **Laufzeit** statt (Tiny-CLJ ist ein Interpreter, kein Compiler), aber **NICHT im Hot-Path**. Expansion erfolgt nur bei tatsächlichen Macro-Aufrufen, nicht bei jeder Liste.

**Performance-Kosten:**
1. **AST-Transformation:** Macro-Expansion transformiert den AST (erstellt neuen, größeren AST)
2. **Größerer expandierter AST:** Mehr AST-Nodes müssen evaluiert werden
3. **Expansion-Overhead:** Macro-Funktion-Aufruf und AST-Building

## Wann findet Macro-Expansion statt?

### Architektur: Macro-Expansion nach Parsing, vor TCO

**Neue Verarbeitungs-Pipeline:**
1. **Parsing** → AST (unverändert, `parse()` oder `parse_from_reader()`)
2. **Macro-Expansion** → expandierter AST (NEU, in `macroexpand()` oder `expand_macros()`)
3. **TCO-Optimierung** → optimierter AST (auf expandiertem AST, in `transform_recursive_tail_calls()`)
4. **Evaluation** → Ergebnis (unverändert, `eval_parsed()` → `eval_list()`)

**Implementierungs-Orte:**
- **Parsing:** `src/parser.c` - `parse()`, `parse_from_reader()`
- **Macro-Expansion:** `src/macro.c` (neu) - `macroexpand()`, `expand_macros()`
- **TCO-Optimierung:** `src/optimize.c` - `transform_recursive_tail_calls()` (bereits vorhanden)
- **Evaluation:** `src/function_call.c` - `eval_parsed()`, `eval_list()` (unverändert)

**Integration in `eval_parsed()`:**
```c
ID eval_parsed(CljObject *parsed_expr, EvalState *eval_state, CljMap *env) {
    // 1. Macro-Expansion (NEU)
    CljObject *expanded = macroexpand(parsed_expr, eval_state);
    
    // 2. TCO-Optimierung (nur bei defn, bereits vorhanden)
    // ... (bleibt unverändert, arbeitet auf expanded)
    
    // 3. Evaluation (unverändert)
    // ... (bleibt unverändert, arbeitet auf expanded)
}
```

**Zeitpunkt der Macro-Expansion:**
- **Nach dem Parsing:** AST ist vollständig geparst
- **Vor der TCO-Optimierung:** Expandierter AST kann dann TCO-optimiert werden
- **Vor der Evaluation:** Expandierter AST wird evaluiert

**Vorteile dieser Architektur:**
- ✅ Macro-Expansion ist **NICHT im Hot-Path** (Evaluation)
- ✅ Expandierter AST kann TCO-optimiert werden
- ✅ Klare Trennung der Phasen (Parsing → Expansion → Optimierung → Evaluation)
- ✅ Expansion erfolgt nur einmal pro Ausdruck (nicht bei jedem Funktionsaufruf)
- ✅ **Hot-Path (`eval_list()`) bleibt komplett unverändert**

### Hot-Path-Analyse

**`eval_list()` wird sehr häufig aufgerufen:**
- Bei jedem Funktionsaufruf
- Bei jedem Special-Form-Aufruf
- Bei jeder verschachtelten Liste
- **Geschätzte Häufigkeit:** 100-1000x pro Sekunde in typischen Programmen

**Aktueller Hot-Path in `eval_list()`:**
1. Symbol-Resolution (~O(n) Array-Map-Lookup - linear search durch gesamtes Array)
2. Special-Form-Checks (Pointer-Vergleiche - sehr schnell)
3. Arithmetic-Dispatch (Pointer-Vergleiche - sehr schnell)
4. Function-Call (Interpreter-Aufruf)

**Hinweis:** `CljMap` ist eine **Array-Map** (keine Hash-Map), daher O(n) Lookup durch gesamtes Array. Hash-Maps (Open Hashing/Linear Probing) wären typischerweise schneller, da sie an einer speziellen Stelle (Hash-Index) starten und dann linear suchen, statt durch das gesamte Array zu iterieren. Der Hashing-Overhead könnte bei sehr kleinen Maps (<4-8 Einträge) dominieren, aber bei typischen Map-Größen (>8 Einträge) wären Hash-Maps schneller.

**Mit Macro-Expansion (NICHT im Hot-Path!):**
- **Macro-Expansion erfolgt NACH Parsing, VOR Evaluation**
- **Hot-Path (Evaluation) bleibt komplett unverändert**
- **Expansion-Phase:** 
  - Durchläuft AST rekursiv
  - Prüft jede Liste auf Macro-Aufrufe
  - Expandiert Macros zu größerem AST
- **TCO-Optimierung:** Arbeitet auf expandiertem AST
- **Evaluation:** Arbeitet auf optimiertem, expandiertem AST (mehr Nodes = mehr Evaluation)

## Performance-Vergleich

### Parser-Level Expansion (aktuell für `if-let`)

**Zeitpunkt:** Beim Parsing (einmalig)
**Kosten pro Aufruf:**
- String-Vergleich: ~10-50ns
- AST-Building: ~100-500ns
- **Gesamt:** ~100-550ns pro `if-let`-Aufruf

**Vorteile:**
- ✅ Einmalige Kosten (nur beim Parsing)
- ✅ Sehr schnell (direkt im Parser)
- ✅ Keine Runtime-Overhead

**Nachteile:**
- ❌ Code-Duplikation
- ❌ Nicht erweiterbar
- ❌ Inkonsistent (nur für `if-let`)

### Macro-Level Expansion (geplant)

**Zeitpunkt:** Nach Parsing, vor TCO-Optimierung (einmalig pro Ausdruck)
**Kosten pro Macro-Aufruf:**

**Expansion-Phase (nach Parsing):**
1. **AST-Traversierung:** ~50-200ns (rekursives Durchlaufen des AST)
2. **Macro-Registry-Lookup:** ~20-100ns (Array-Map-Lookup O(n), typischerweise <16 Einträge)
3. **Macro-Funktion-Aufruf:** ~500-2000ns (Interpreter-Aufruf mit un-evaluierten Argumenten)
4. **AST-Transformation:** ~200-800ns (Erstellen neuer AST-Nodes, List/Vector-Building)
5. **Rekursive Expansion:** ~100-500ns pro verschachteltem Macro
6. **Expansion-Gesamt:** ~870-3600ns

**TCO-Optimierung (auf expandiertem AST):**
7. **TCO arbeitet auf expandiertem AST:** Keine zusätzlichen Kosten (TCO würde sowieso laufen)
8. **Vorteil:** Expandierte Macros können TCO-optimiert werden

**Evaluation-Phase (expandierter AST):**
9. **Mehr AST-Nodes:** Expandierter AST hat typischerweise 2-5x mehr Nodes
10. **Mehr Evaluation:** Jeder zusätzliche Node muss evaluiert werden
11. **Beispiel:** `(-> data (map f))` expandiert zu `(map f data)` - ähnlich viele Nodes
12. **Beispiel:** `(if-let [x y] a b)` expandiert zu `(let [x y] (if x a b))` - mehr Nodes!

**Gesamt-Kosten:**
- **Expansion:** ~860-3550ns (einmalig, nach Parsing)
- **Zusätzliche Evaluation:** ~100-1000ns (je nach Größe des expandierten AST)
- **Gesamt:** ~960-4550ns pro Macro-Aufruf

**Vorteile:**
- ✅ Erweiterbar (User können Macros definieren)
- ✅ Konsistent (alle Macros gleich behandelt)
- ✅ Wiederverwendung des Interpreters
- ✅ **NICHT im Hot-Path** - Expansion erfolgt vor Evaluation
- ✅ **Einmalige Expansion** - nicht bei jedem Funktionsaufruf
- ✅ **TCO-kompatibel** - expandierter AST kann TCO-optimiert werden

**Nachteile:**
- ❌ **2-8x langsamer** als Parser-Level-Expansion (inkl. größerer AST)
- ❌ Expansion-Overhead nach Parsing (einmalig pro Ausdruck)
- ❌ Größerer expandierter AST = mehr Evaluation

## Performance-Impact-Schätzung

### Szenario 1: Einfaches Programm ohne Macros

**Overhead:** **Minimal, nur in Expansion-Phase!**
- Expansion-Phase durchläuft AST rekursiv
- Prüft jede Liste auf Macro-Aufrufe (Symbol-Pointer-Vergleich)
- Bei Nicht-Macro-Listen: Keine Expansion, AST bleibt unverändert
- **Expansion-Kosten:** ~50-200ns pro Ausdruck (AST-Traversierung)
- **Hot-Path (Evaluation):** **KEIN Overhead!**
- **Impact:** <0.1% Performance-Verlust (vernachlässigbar)

### Szenario 2: Programm mit vielen Macro-Aufrufen

**Beispiel:** `(-> data (map f) (filter p) (reduce +))`
- 3 Macro-Aufrufe (`->`) im AST
- **Expansion-Phase (nach Parsing):**
  - AST-Traversierung: ~150-600ns
  - 3 Macro-Expansionen: ~2580-10650ns
  - **Expansion-Gesamt:** ~2730-11250ns
- **TCO-Optimierung:** Arbeitet auf expandiertem AST (keine zusätzlichen Kosten)
- **Evaluation-Phase:**
  - Expandierter AST: `(reduce + (filter p (map f data)))`
  - Ähnlich viele Nodes wie Original
  - **Zusätzliche Evaluation:** ~100-500ns (minimal, da ähnlich viele Nodes)
- **Gesamt:** ~2830-11750ns zusätzlich (einmalig, nach Parsing)

**Impact:** 5-25% Performance-Verlust bei Macro-heavy Code (nur in Expansion-Phase, nicht im Hot-Path!)

**Wichtig:** Expansion erfolgt **einmalig nach Parsing**, nicht bei jedem Funktionsaufruf!

### Szenario 3: Verschachtelte Macros

**Beispiel:** `(cond-> data test1 (->> (map f)) test2 (-> (filter p)))`
- Mehrfache Expansion (cond-> expandiert zu if-Ketten)
- Rekursive Expansion (->> und -> innerhalb von cond->)
- **Expansion-Phase (nach Parsing):**
  - AST-Traversierung: ~200-800ns
  - Mehrfache Expansionen: ~5000-20000ns
  - **Expansion-Gesamt:** ~5200-20800ns
- **TCO-Optimierung:** Arbeitet auf expandiertem AST (kann komplexer sein)
- **Evaluation-Phase:**
  - Deutlich größerer expandierter AST (if-Ketten mit verschachtelten Threading-Macros)
  - **Zusätzliche Evaluation:** ~500-5000ns (mehr Nodes = mehr Evaluation)
- **Gesamt:** ~5700-25800ns zusätzlich (einmalig, nach Parsing)

**Impact:** 10-35% Performance-Verlust (nur in Expansion-Phase, nicht im Hot-Path!)

**Wichtig:** Verschachtelte Macros erzeugen deutlich größere expandierte ASTs, was mehr Evaluation erfordert. Aber Expansion erfolgt **einmalig nach Parsing**.

## Optimierungsmöglichkeiten

### 1. Expansion-Caching (KRITISCH - empfohlen)

**Konzept:** Expandierte Macros cachen (AST → expandierter AST)

**Implementierung:**
```c
// Cache-Struktur
typedef struct {
    CljMap *expansion_cache;  // AST → expandierter AST
} MacroExpansionCache;

// Bei Macro-Aufruf:
1. Prüfe Cache (AST-Hash als Key)
2. Wenn gefunden: Verwende gecachte Expansion
3. Wenn nicht: Expandiere, speichere im Cache
```

**Performance-Gewinn:**
- **Erster Ausdruck:** ~860-3550ns Expansion (wie vorher)
- **Weitere Ausdrücke (mit Cache):** ~10-50ns (Cache-Lookup) + AST-Traversierung
- **Geschätzte Verbesserung:** 20-100x schneller bei wiederholten Ausdrücken (nur Expansion)
- **Wichtig:** 
  - Expansion erfolgt **einmalig nach Parsing** (nicht bei jedem Funktionsaufruf)
  - Cache spart Expansion-Kosten bei wiederholten Parsing desselben Codes
  - Evaluation des expandierten AST bleibt gleich (kann nicht gecacht werden)

**Nachteile:**
- Mehr Speicher-Verbrauch (~100-500 Bytes pro gecachte Expansion)
- Cache-Management nötig (LRU, Größen-Limit)

**Empfehlung:** **Implementieren!** Kritisch für Performance.

### 2. Macro-Registry-Optimierung

**Aktuell:** Map-Lookup mit Symbol-Name (String-Vergleich)
**Optimiert:** Symbol-Pointer-Vergleich

**Implementierung:**
```c
// Statt: map_get(registry, symbol_name_string, ...)
// Verwende: map_get(registry, symbol_pointer, ...)
```

**Performance-Gewinn:**
- **Lookup:** ~20-100ns → ~10-50ns (2-5x schneller, durch Symbol-Pointer-Vergleich statt Array-Iteration)
- **Gesamt:** ~10-50ns Ersparnis pro Macro-Aufruf

**Empfehlung:** **Implementieren!** Einfach, großer Gewinn.

### 3. Fast-Path für häufigste Macros

**Konzept:** Häufigste Macros (`->`, `->>`, `when-let`) als Parser-Level behalten

**Implementierung:**
- Parser-Level-Expansion für Top-5 Macros
- Macro-Level-Expansion für alle anderen

**Performance-Gewinn:**
- **Häufigste Macros:** ~100-550ns (wie Parser-Level)
- **Andere Macros:** ~750-3200ns (wie Macro-Level)
- **Gesamt:** ~50-70% der Macro-Aufrufe sind schnell

**Nachteile:**
- Inkonsistenz (zwei Implementierungswege)
- Wartungsaufwand

**Empfehlung:** **Optional** - Nur wenn Performance kritisch ist.

### 4. Compile-Zeit-Expansion (optional)

**Konzept:** Bei `defn`/`def` bereits expandieren

**Implementierung:**
- Bei Funktions-Definition: Alle Macros im Body expandieren
- Expandierte Version speichern
- Bei Funktions-Aufruf: Verwende expandierte Version

**Performance-Gewinn:**
- **Keine Runtime-Kosten** für Macros in Funktions-Bodies
- **Nur einmalige Expansion** bei Definition

**Nachteile:**
- Komplex (erfordert AST-Transformation)
- Funktioniert nicht für dynamische Macros

**Empfehlung:** **Optional** - Nur wenn Performance absolut kritisch ist.

### 5. Macro-Registry-Lookup-Optimierung

**Konzept:** Lookup nur bei Symbolen, nicht bei jeder Liste

**Aktuell:** Lookup bei jeder Liste (auch wenn kein Macro)
**Optimiert:** Lookup nur wenn Operator ein Symbol ist

**Implementierung:**
```c
// In eval_list(), nach Symbol-Resolution:
if (op && TAG(op) == CLJ_SYMBOL) {
    CljFunction *macro_fn = macro_registry_get(st->current_ns, op_sym);
    if (macro_fn) {
        // Macro-Expansion
    }
}
```

**Performance-Gewinn:**
- **Kein Lookup** bei Funktions-Aufrufen, Special-Forms, Maps, etc.
- **Nur Lookup** bei Symbolen (die potentiell Macros sein könnten)
- **Geschätzte Ersparnis:** ~20-100ns pro Nicht-Macro-Liste (Array-Map-Lookup vermieden)
- **Wichtig:** Hält Macro-Expansion aus dem Hot-Path heraus!

**Empfehlung:** **Implementieren!** Einfach, reduziert Overhead, hält Hot-Path sauber.

## Performance-Ziele

### Mit Optimierungen

**Ziel:** <10% Performance-Overhead gegenüber Parser-Level-Expansion

**Erreichbar mit:**
1. Expansion-Caching ✅
2. Macro-Registry-Optimierung ✅
3. Lookup-Optimierung ✅

**Geschätzte Performance:**
- **Erster Ausdruck (mit Macros):** ~870-3600ns Expansion (nach Parsing) + Evaluation
- **Weitere Ausdrücke (mit Cache):** ~10-50ns (Cache-Lookup) + AST-Traversierung + Evaluation
- **Ausdrücke ohne Macros:** ~50-200ns (AST-Traversierung, keine Expansion)
- **Hot-Path (Evaluation):** **Komplett unverändert!** Expansion erfolgt vor Evaluation

**Hinweis:** Array-Map-Lookup ist O(n) durch gesamtes Array. Hash-Maps (Open Hashing/Linear Probing) wären typischerweise schneller, da sie an Hash-Index starten und dann linear suchen. Bei sehr kleinen Maps (<4-8 Einträge) könnte Hashing-Overhead dominieren, aber bei typischen Größen (>8 Einträge) wären Hash-Maps schneller. Der Hashing-Overhead könnte bei sehr kleinen Maps sogar weggelassen werden.

### Ohne Optimierungen

**Realität:** 5-30% Performance-Overhead bei Macro-heavy Code

**Nicht akzeptabel für Embedded-Systeme!**

## Empfehlungen

### Für Embedded-Systeme (ESP32)

1. **Expansion-Caching implementieren** - **KRITISCH!**
2. **Macro-Registry-Optimierung** - Symbol-Pointer-Vergleich
3. **Lookup-Optimierung** - Nur bei Symbolen
4. **Benchmarks durchführen** - Messen, nicht raten
5. **Optional:** Fast-Path für häufigste Macros

### Performance-Monitoring

**Wichtig:** Performance-Impact messen, nicht schätzen!

**Benchmarks:**
- Macro-Expansion ohne Cache
- Macro-Expansion mit Cache
- Vergleich mit Parser-Level-Expansion
- Real-World-Szenarien (Macro-heavy Code)

## AST-Transformation und größerer expandierter AST

### AST-Transformation-Kosten

**Macro-Expansion transformiert den AST:**
- Erstellt neue AST-Nodes (Listen, Vektoren, etc.)
- Kopiert/transformiert bestehende Nodes
- **Kosten:** ~200-800ns pro Macro-Aufruf (abhängig von Komplexität)

### Größerer expandierter AST

**Expandierte Macros erzeugen typischerweise größere ASTs:**

**Beispiel 1:** `(if-let [x y] a b)`
- **Original:** 4 Nodes (if-let, [x y], a, b)
- **Expandiert:** `(let [x y] (if x a b))` - 6 Nodes (let, [x y], if, x, a, b)
- **Mehr Nodes:** +50% Nodes

**Beispiel 2:** `(-> data (map f) (filter p))`
- **Original:** 3 Nodes (->, data, (map f), (filter p))
- **Expandiert:** `(filter p (map f data))` - 4 Nodes (filter, p, map, f, data)
- **Ähnlich viele Nodes:** ~0% Unterschied

**Beispiel 3:** `(cond-> data test1 (->> (map f)) test2 (-> (filter p)))`
- **Original:** 6 Nodes
- **Expandiert:** Komplexe if-Kette mit verschachtelten Threading-Macros
- **Mehr Nodes:** +200-500% Nodes (deutlich größer!)

**Performance-Impact:**
- **Mehr Nodes = mehr Evaluation**
- Jeder zusätzliche Node muss evaluiert werden
- **Kosten:** ~50-200ns pro zusätzlichem Node
- **Typischer Overhead:** 5-20% zusätzliche Evaluation-Kosten

### Zusammenfassung AST-Kosten

1. **AST-Transformation:** ~200-800ns (einmalig pro Macro-Aufruf)
2. **Größerer expandierter AST:** 0-500% mehr Nodes (abhängig vom Macro)
3. **Zusätzliche Evaluation:** ~50-200ns pro zusätzlichem Node
4. **Gesamt AST-Overhead:** ~100-2000ns zusätzlich (je nach Macro-Komplexität)

## Fazit

**Macro-Expansion kostet Performance**, aber:
- ✅ **NICHT im Hot-Path (Evaluation)** - Expansion erfolgt nach Parsing, vor Evaluation
- ✅ **Einmalige Expansion** - nicht bei jedem Funktionsaufruf
- ✅ **TCO-kompatibel** - expandierter AST kann TCO-optimiert werden
- ✅ Mit Optimierungen (Expansion-Caching) ist der Overhead akzeptabel (<10%)
- ⚠️ **Größerer expandierter AST** erfordert mehr Evaluation (kann nicht gecacht werden)

**Ohne Optimierungen:** 5-35% Performance-Verlust (nur in Expansion-Phase) - **akzeptabel, da nicht im Hot-Path**

**Mit Optimierungen:** <5% Performance-Overhead - **sehr gut für Embedded-Systeme**

**Wichtig:** 
- Der größere expandierte AST ist ein inhärenter Teil der Macro-Expansion
- Expansion erfolgt **einmalig nach Parsing**, nicht bei jedem Funktionsaufruf
- **Hot-Path (Evaluation) bleibt komplett unverändert**

