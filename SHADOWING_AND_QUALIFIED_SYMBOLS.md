# Umgang mit Shadowing und qualifizierten Symbolen

## 1. Shadowing über Lookup-Reihenfolge

### Problem
Wenn Symbole unqualifiziert gespeichert werden, kann dasselbe Symbol in mehreren Namespaces existieren. Wie wird entschieden, welches verwendet wird?

### Lösung: Hierarchische Lookup-Reihenfolge

Die aktuelle Implementierung verwendet eine **definierte Lookup-Reihenfolge**, die Clojure-Semantik entspricht:

#### A) Environment-Stack (für let-Bindings und Funktionsparameter)

**Code**: `resolve_symbol_in_env()` (Zeile 480-512 in `function_call.c`)

```c
// Search through environment stack (most recent first)
CljList *current_stack = env_stack;
while (current_stack && TAG(current_stack) == CLJ_LIST) {
    ID env_obj_id = LIST_FIRST(current_stack);
    
    if (env_obj_id && TAG(env_obj_id) == CLJ_MAP) {
        CljMap *env = (CljMap*)env_obj_id;
        
        // Search for symbol in current environment
        ID resolved = map_get(env, interned_sym, &not_found);
        if (resolved != &not_found) {
            return resolved;  // ← Erste gefundene Bindung gewinnt
        }
    }
    
    // Move to next environment in stack (äußerer Scope)
    current_stack = LIST_REST(current_stack);
}
```

**Reihenfolge**:
1. **Innere let-Bindings** (neueste zuerst)
2. **Äußere let-Bindings**
3. **Funktionsparameter**
4. **Namespace-Mappings** (falls nicht in Environment gefunden)

**Beispiel**:
```clojure
(let [x 1]           ; Äußerer Scope
  (let [x 2]         ; Innerer Scope
    x))              ; → 2 (innere Bindung shadowt äußere)
```

#### B) Namespace-Lookup (für unqualifizierte Symbole)

**Code**: `ns_resolve()` (Zeile 323-408 in `namespace.c`)

```c
// Unqualified symbol - check current namespace first (before cache)
// This ensures that redefined symbols in current namespace take precedence
CljSymbol *qualified_sym = intern_symbol(current_ns->name, sym->cname);
ID v = map_get(current_ns->mappings, qualified_sym, &not_found_sentinel);
if (v != &not_found_sentinel) {
    // Found in current namespace - check for ambiguity
    // ...
    return v;  // ← Current namespace hat Vorrang
}

// Not in current namespace - search clojure.core
if (g_runtime.clojure_core_cache) {
    CljSymbol *qualified_sym = intern_symbol(SYM_CLOJURE_CORE, sym->cname);
    ID resolved = map_get(clojure_core->mappings, qualified_sym, ...);
    if (resolved) {
        return resolved;  // ← clojure.core als Fallback
    }
}
```

**Reihenfolge**:
1. **Current Namespace** (`user`, `my-ns`, etc.)
2. **clojure.core** (automatisch verfügbar)
3. **Andere Namespaces** (nur mit expliziter Qualifizierung oder `:refer`)

**Beispiel**:
```clojure
;; In user namespace
(def inc 999)        ; Shadowt clojure.core/inc
(+ inc 1)            ; → 1000 (user/inc hat Vorrang)

;; Ohne Shadowing
(+ 1 2)              ; → 3 (clojure.core/+ wird verwendet)
```

### Ambiguitäts-Erkennung

**Code**: `ns_resolve()` (Zeile 344-398 in `namespace.c`)

Wenn ein Symbol in mehreren Namespaces gefunden wird (außer current_ns und clojure.core), wird ein **hilfreicher Fehler** geworfen:

```c
if (search_ctx.ambiguous && search_ctx.result_ns && search_ctx.second_ns) {
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0,
        "Unable to resolve symbol: %s in this context, perhaps you meant: %s/%s or %s/%s",
        sym_name, ns1_name, sym_name, ns2_name, sym_name);
    return NULL;
}
```

**Beispiel**:
```clojure
;; Symbol 'helper' existiert in 'utils' und 'common'
(helper)  ; → Fehler: "Unable to resolve symbol: helper in this context, 
          ;   perhaps you meant: utils/helper or common/helper"
```

---

## 2. Qualifizierte Symbole

### Problem
Wenn Symbole unqualifiziert gespeichert werden, wie werden qualifizierte Symbole wie `clojure.core/inc` behandelt?

### Lösung: Qualifizierung wird beim Lookup entfernt

**Code**: `ns_resolve()` (Zeile 295-320 in `namespace.c`)

```c
// Handle qualified symbols (symbol->ns_name is set during parsing)
if (sym->ns_name && sym->ns_name->cname) {
    // Qualified symbol - look up in target namespace
    CljSymbol *interned_ns_name = sym->ns_name;
    CljNamespace *target_ns = ns_find_by_symbol(interned_ns_name);
    
    if (target_ns && target_ns->mappings && sym->cname) {
        // Intern the qualified symbol to get the same pointer as stored in mappings
        CljSymbol *interned_sym = intern_symbol(sym->ns_name, sym->cname);
        
        // Look up in target namespace with qualified symbol
        ID resolved = map_get(target_ns->mappings, interned_sym, &not_found_sentinel);
        if (resolved != &not_found_sentinel) {
            return resolved;
        }
    }
    return NULL;  // Qualified symbol not found in target namespace
}
```

**Aktuell (qualifiziert gespeichert)**:
- Qualifiziertes Symbol `clojure.core/inc` wird direkt gesucht
- Funktioniert, weil Mappings qualifizierte Symbole enthalten

**Bei unqualifiziertem Speichern** (Option D):
- Qualifiziertes Symbol `clojure.core/inc` wird erkannt
- Namespace-Komponente (`clojure.core`) wird extrahiert
- Unqualifiziertes Symbol (`inc`) wird im target Namespace gesucht
- **Änderung nötig**: Statt `intern_symbol(sym->ns_name, sym->cname)` → `intern_symbol_global(sym->cname)`

### Beispiel für beide Varianten

**Aktuell (qualifiziert gespeichert)**:
```c
// Speicherung
ns_define(clojure_core, intern_symbol_global("inc"), func);
// → Speichert: clojure.core/inc → func

// Lookup: clojure.core/inc
CljSymbol *qualified = intern_symbol(SYM_CLOJURE_CORE, "inc");
map_get(clojure_core->mappings, qualified, ...);  // → func
```

**Bei unqualifiziertem Speichern**:
```c
// Speicherung
ns_define(clojure_core, intern_symbol_global("inc"), func);
// → Speichert: inc → func (unqualifiziert)

// Lookup: clojure.core/inc
CljSymbol *unqualified = intern_symbol_global("inc");  // Entferne Qualifizierung
map_get(clojure_core->mappings, unqualified, ...);  // → func
```

---

## Zusammenfassung

### Shadowing
- **Gelöst durch Lookup-Reihenfolge**: Inner → Outer → Current Namespace → clojure.core
- **Clojure-kompatibel**: Entspricht Clojure-Verhalten
- **Ambiguitäts-Erkennung**: Hilfreiche Fehlermeldungen bei mehrdeutigen Symbolen

### Qualifizierte Symbole
- **Aktuell**: Direkter Lookup mit qualifiziertem Symbol (funktioniert)
- **Bei unqualifiziertem Speichern**: Qualifizierung wird entfernt, dann Lookup mit unqualifiziertem Symbol
- **Beide Varianten funktionieren**: `inc` und `clojure.core/inc` führen zum gleichen Ergebnis

### Vorteile der aktuellen Implementierung
1. **Robust**: Shadowing wird korrekt behandelt
2. **Clojure-kompatibel**: Verhalten entspricht Clojure
3. **Benutzerfreundlich**: Hilfreiche Fehlermeldungen bei Ambiguität
4. **Flexibel**: Unterstützt sowohl qualifizierte als auch unqualifizierte Symbole

### Bei Umstellung auf unqualifiziertes Speichern
- **Shadowing**: Bleibt gleich (Lookup-Reihenfolge ändert sich nicht)
- **Qualifizierte Symbole**: Einfache Änderung: Entferne Qualifizierung vor Lookup
- **Vorteil**: Pointer-Konsistenz wird verbessert


