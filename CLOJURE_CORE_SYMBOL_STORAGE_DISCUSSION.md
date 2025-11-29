# Diskussion: clojure.core Symbole in Mappings speichern

## Aktuelle Implementierung

### Wie es aktuell funktioniert:

1. **Registrierung** (`register_builtin_in_core`):
   ```c
   CljObject *symbol = (CljObject*)intern_symbol_global(symbol_name);  // unqualifiziert
   ns_define(target_ns, symbol, func_obj);
   ```

2. **Speicherung** (`ns_define`):
   ```c
   // Automatische Qualifizierung
   if (sym && !sym->ns_name && ns->name && ns->name->cname) {
       qualified_symbol = intern_symbol(ns->name, sym->cname);  // qualifiziert
   }
   // Speicherung mit qualifiziertem Symbol
   map_assoc(ns->mappings, qualified_symbol, value);
   ```

3. **Lookup** (`ns_resolve`):
   ```c
   // Qualifizierung für Lookup
   CljSymbol *qualified_sym = intern_symbol(SYM_CLOJURE_CORE, sym->cname);
   map_get(g_runtime.clojure_core_cache->mappings, qualified_sym, ...);
   ```

**Ergebnis**: In `clojure.core` Mappings werden qualifizierte Symbole gespeichert (`clojure.core/inc`).

---

## Option A: Qualifiziert speichern (aktuell)

### Vorteile:

1. **Konsistenz**: Alle Namespace-Mappings verwenden qualifizierte Symbole
   - `user/my-var` in `user` Namespace
   - `clojure.core/inc` in `clojure.core` Namespace
   - Einheitliches Verhalten für alle Namespaces

2. **Eindeutigkeit**: Keine Verwechslung zwischen Namespaces möglich
   - `user/inc` vs. `clojure.core/inc` sind klar unterschieden
   - Shadowing ist explizit sichtbar

3. **Clojure-Kompatibilität**: 
   - In Clojure können qualifizierte Symbole verwendet werden: `clojure.core/inc`
   - Metadaten zeigen qualifizierte Symbole: `(meta #'inc)` → `{:ns clojure.core, :name inc}`

4. **Einfacheres Debugging**: 
   - Mappings zeigen klar, welcher Namespace gemeint ist
   - Keine Ambiguität bei der Inspektion

### Nachteile:

1. **Pointer-Inkonsistenzen**: 
   - `intern_symbol_global("inc")` erstellt unqualifiziertes Symbol
   - `ns_define()` qualifiziert es → neues Symbol-Objekt
   - Lookup muss wieder qualifizieren → möglicherweise wieder neues Objekt
   - **Problem**: Verschiedene Pointer für dasselbe logische Symbol

2. **Performance-Overhead**:
   - Zusätzliche `intern_symbol()` Aufrufe bei Speicherung und Lookup
   - Mehr Symbol-Objekte im Speicher (wenn nicht korrekt interniert)

3. **Komplexität**:
   - Zwei Schritte: Unqualifiziert erstellen → Qualifizieren
   - Lookup muss auch qualifizieren

---

## Option B: Unqualifiziert speichern (wie in Clojure)

### Vorteile:

1. **Clojure-Semantik**:
   - In Clojure haben `inc`, `+` etc. `ns_name = nil`
   - Sie sind im `clojure.core` Namespace, aber das Symbol selbst ist unqualifiziert
   - **Konsistent mit Clojure-Verhalten**

2. **Pointer-Konsistenz**:
   - `intern_symbol_global("inc")` erstellt Symbol einmal
   - Wird direkt gespeichert (keine Qualifizierung)
   - Lookup verwendet dasselbe Symbol
   - **Keine Pointer-Inkonsistenzen**

3. **Performance**:
   - Weniger `intern_symbol()` Aufrufe
   - Weniger Symbol-Objekte
   - Schnellerer Lookup (direkter Pointer-Vergleich)

4. **Einfachheit**:
   - Ein Schritt: Unqualifiziert erstellen → Direkt speichern
   - Lookup verwendet dasselbe unqualifizierte Symbol

### Nachteile:

1. **Inkonsistenz mit anderen Namespaces**:
   - `user/my-var` würde als `my-var` gespeichert werden
   - Oder: Nur `clojure.core` speichert unqualifiziert, andere qualifiziert?
   - **Problem**: Unterschiedliches Verhalten je nach Namespace

2. **Ambiguität bei Shadowing**:
   - Wenn `user` Namespace auch `inc` definiert, wie unterscheiden?
   - Muss durch Namespace-Kontext unterschieden werden
   - **Lösung**: Lookup-Reihenfolge (current_ns → clojure.core)

3. **Qualifizierte Symbole**:
   - `clojure.core/inc` würde nicht funktionieren (wenn unqualifiziert gespeichert)
   - **Lösung**: Qualifizierte Symbole müssen anders behandelt werden

---

## Vergleich mit Clojure

### Clojure-Verhalten:

```clojure
;; Symbole selbst sind unqualifiziert
user=> (type 'inc)
clojure.lang.Symbol
user=> (:ns (meta #'inc))
clojure.core
user=> (name 'inc)
"inc"
user=> (namespace 'inc)
nil  ; ← Symbol selbst ist unqualifiziert!

;; Aber qualifizierte Symbole funktionieren
user=> (resolve 'clojure.core/inc)
#'clojure.core/inc
```

**Wichtig**: 
- Das Symbol `inc` hat `namespace = nil`
- Es ist aber im `clojure.core` Namespace verfügbar
- Qualifizierte Symbole `clojure.core/inc` funktionieren auch

---

## Empfehlung: Hybrid-Ansatz

### Vorschlag: Unqualifiziert speichern, aber qualifiziert unterstützen

1. **Speicherung**: Unqualifizierte Symbole in Mappings
   ```c
   // In ns_define für clojure.core:
   if (ns->name == SYM_CLOJURE_CORE) {
       // clojure.core: Speichere unqualifiziert (wie Clojure)
       qualified_symbol = sym;  // Keine Qualifizierung
   } else {
       // Andere Namespaces: Qualifiziere
       qualified_symbol = intern_symbol(ns->name, sym->cname);
   }
   ```

2. **Lookup**: Beide Varianten unterstützen
   ```c
   // Unqualifiziertes Symbol: Suche in clojure.core
   if (!sym->ns_name) {
       // Suche in clojure.core mit unqualifiziertem Symbol
       map_get(clojure_core->mappings, sym, ...);
   }
   
   // Qualifiziertes Symbol: Suche in target Namespace
   if (sym->ns_name == SYM_CLOJURE_CORE) {
       // Entferne Qualifizierung für Lookup
       CljSymbol *unqualified = intern_symbol_global(sym->cname);
       map_get(clojure_core->mappings, unqualified, ...);
   }
   ```

### Vorteile des Hybrid-Ansatzes:

1. **Clojure-kompatibel**: `inc` hat `ns_name = nil` (wie in Clojure)
2. **Pointer-konsistent**: Ein Symbol-Objekt für `inc`
3. **Beide Varianten**: `inc` und `clojure.core/inc` funktionieren
4. **Performance**: Weniger Qualifizierungen für clojure.core

### Nachteile:

1. **Komplexität**: Unterschiedliche Behandlung für clojure.core
2. **Inkonsistenz**: Andere Namespaces verhalten sich anders

---

## Alternative: Konsistent unqualifiziert für alle Namespaces

### Vorschlag: Alle Namespaces speichern unqualifiziert

**Prinzip**: Der Namespace-Kontext kommt vom Namespace-Objekt, nicht vom Symbol.

1. **Speicherung**: Immer unqualifiziert
   ```c
   // In ns_define: Keine Qualifizierung
   qualified_symbol = sym;  // Immer unqualifiziert
   ```

2. **Lookup**: Namespace-Kontext bestimmt, wo gesucht wird
   ```c
   // Unqualifiziertes Symbol: Suche in current_ns, dann clojure.core
   if (!sym->ns_name) {
       // Suche in current_ns->mappings mit unqualifiziertem Symbol
       map_get(current_ns->mappings, sym, ...);
       // Falls nicht gefunden: Suche in clojure.core
       map_get(clojure_core->mappings, sym, ...);
   }
   
   // Qualifiziertes Symbol: Suche in target Namespace
   if (sym->ns_name) {
       target_ns = ns_find_by_symbol(sym->ns_name);
       map_get(target_ns->mappings, unqualified_sym, ...);
   }
   ```

### Vorteile:

1. **Maximale Konsistenz**: Alle Namespaces gleich behandelt
2. **Clojure-kompatibel**: Symbole sind unqualifiziert
3. **Pointer-konsistent**: Ein Symbol-Objekt pro Name
4. **Einfachheit**: Keine Sonderbehandlung für clojure.core

### Nachteile:

1. **Shadowing**: Muss durch Lookup-Reihenfolge gelöst werden
2. **Qualifizierte Symbole**: Müssen Namespace-Kontext entfernen

---

## Empfehlung

**Option: Konsistent unqualifiziert für alle Namespaces**

**Begründung**:
1. **Clojure-Kompatibilität**: Symbole haben `ns_name = nil` (wie in Clojure)
2. **Pointer-Konsistenz**: Löst das Hauptproblem der Test-Fehler
3. **Einfachheit**: Keine Sonderbehandlung nötig
4. **Performance**: Weniger Symbol-Objekte, schnellere Lookups

**Implementierung**:
- `ns_define()`: Keine automatische Qualifizierung mehr
- `ns_resolve()`: Lookup mit unqualifizierten Symbolen
- Qualifizierte Symbole: Entferne Qualifizierung für Lookup

**Migration**:
- Bestehende qualifizierte Symbole in Mappings müssen migriert werden
- Oder: Lookup unterstützt beide Varianten (qualifiziert und unqualifiziert)

