# Analyse: Warum bei qualifizierten Symbolen bleiben?

## Aktuelle Situation

**Speicherung** (funktioniert):
- `register_builtin_in_core("inc", ...)` → `intern_symbol_global("inc")` (unqualifiziert)
- `ns_define(clojure_core, unqualified, func)` → qualifiziert zu `clojure.core/inc`
- **Ergebnis**: `clojure.core/inc` wird in Mappings gespeichert ✅

**Lookup** (funktioniert):
- `ns_resolve()` qualifiziert Symbol: `intern_symbol(SYM_CLOJURE_CORE, "inc")` → `clojure.core/inc`
- `map_get(clojure_core->mappings, qualified_sym, ...)` → findet es ✅

## Das Problem

**Test verwendet unqualifiziertes Symbol direkt**:
```c
// Test: test_inc_symbol_interning_during_load
CljSymbol *inc_sym_after = intern_symbol_global("inc");  // unqualifiziert
CljObject *inc_value = map_get(clojure_core->mappings, inc_sym_after, NULL);
// ❌ Schlägt fehl, weil in Mappings ist clojure.core/inc gespeichert
```

**Lösung**: Test sollte qualifiziertes Symbol verwenden:
```c
CljSymbol *inc_sym_qualified = intern_symbol(SYM_CLOJURE_CORE, "inc");  // qualifiziert
CljObject *inc_value = map_get(clojure_core->mappings, inc_sym_qualified, NULL);
// ✅ Funktioniert
```

## Warum bei qualifizierten Symbolen bleiben?

### 1. **Funktioniert bereits**
- `ns_resolve()` verwendet bereits qualifizierte Symbole
- Lookup funktioniert korrekt
- Nur Tests verwenden falsche Symbole

### 2. **Konsistenz**
- Alle Namespaces verwenden qualifizierte Symbole
- Einheitliches Verhalten
- Keine Sonderbehandlung nötig

### 3. **Eindeutigkeit**
- `user/inc` vs. `clojure.core/inc` sind klar unterschieden
- Keine Ambiguität
- Shadowing ist explizit sichtbar

### 4. **Clojure-kompatibel**
- Qualifizierte Symbole funktionieren: `clojure.core/inc`
- Metadaten zeigen qualifizierte Symbole
- Entspricht Clojure-Verhalten

## Das eigentliche Problem

**Nicht die Qualifizierung, sondern fehlendes Interning bei let-Bindings**:

1. **let-Bindings**: Verwenden unqualifizierte Symbole (lokal)
2. **Problem**: Symbole werden mit verschiedenen Pointern gespeichert/gesucht
3. **Lösung**: Interning in `eval_let()` und `resolve_symbol_in_env()` ✅ (bereits implementiert)

**Namespace-Mappings**: Verwenden qualifizierte Symbole
- **Funktioniert bereits**: `ns_define()` und `ns_resolve()` verwenden `intern_symbol()`
- **Konsistent**: Derselbe qualifizierte Symbol-Pointer wird verwendet

## Fazit

**Bei qualifizierten Symbolen bleiben** ✅

**Gründe**:
1. Funktioniert bereits korrekt
2. Konsistent für alle Namespaces
3. Clojure-kompatibel
4. Eindeutig und klar

**Was geändert werden muss**:
- ✅ **let-Bindings**: Interning hinzugefügt (bereits gemacht)
- ✅ **resolve_symbol_in_env**: Interning hinzugefügt (bereits gemacht)
- ❌ **Tests**: Sollten qualifizierte Symbole verwenden (Test-Problem, nicht Implementierung)

**Keine Änderung an Namespace-Mappings nötig** - sie funktionieren bereits korrekt mit qualifizierten Symbolen.



