# Performance-Analyse: Symbol-Interning

## Problem

`intern_symbol_global()` hat **O(n) Zeitkomplexität**:

1. **Symbol-Tabelle ist ein Vector** (lineare Suche)
2. **`vector_index_of()`** durchsucht alle Symbole (O(n))
3. **Wird im Hot-Path aufgerufen**: Bei jedem let-Binding und Symbol-Lookup

### Code-Analyse

```c
// symbol.c:368-373
static CljSymbol* symbol_table_find(CljSymbol *ns_name, const char *name) {
    if (name && g_runtime.symbol_table) {
        return find_symbol(g_runtime.symbol_table, ns_name, name);  // O(n)
    }
    return NULL;
}

// symbol.c:347-365
static CljSymbol* find_symbol(CljVector *vec, CljSymbol *ns_name, const char *name) {
    // ...
    int index = vector_index_of(vec, (ID)&temp_sym);  // O(n) lineare Suche
    // ...
}

// vector.c:73-83
int vector_index_of(CljVector *vec, ID value) {
    VECTOR_FOR_EACH(vec, elem) {  // Iteriert durch alle Elemente
        if (clj_equal(elem, value)) {
            return _i;
        }
    }
    return INDEX_NOT_FOUND;
}
```

### Aufruf-Häufigkeit

**Im Hot-Path** (bei jedem Symbol-Lookup):
- `resolve_symbol_in_env()` → `intern_symbol_global()` → O(n)
- `eval_let()` → `intern_symbol_global()` → O(n) pro Binding
- Bei 100 Symbolen in der Tabelle = 100 Vergleiche pro Aufruf

---

## Lösungsansätze

### Option 1: Optimierung: Nur internieren wenn nötig

**Idee**: Prüfe, ob Symbol bereits interniert ist, bevor Suche.

**Problem**: Wie prüfen? Pointer-Vergleich funktioniert nicht, weil Parser neue Symbole erstellt.

**Lösung**: Symbol-Interning ist idempotent - wenn Symbol bereits existiert, wird existierender Pointer zurückgegeben. Das Problem ist nur die Suche.

### Option 2: Hash-Map für Symbol-Tabelle

**Idee**: Symbol-Tabelle als Hash-Map statt Vector.

**Vorteile**:
- O(1) Lookup (durchschnittlich)
- O(1) Insert (durchschnittlich)
- Massive Performance-Verbesserung

**Nachteile**:
- Größere Code-Änderung
- Mehr Speicher-Overhead
- Hash-Funktion muss implementiert werden

**Implementierung**:
```c
// Statt Vector:
CljVector *symbol_table;  // O(n) Suche

// Hash-Map:
CljMap *symbol_table;  // O(1) Lookup
```

### Option 3: Cache für häufig verwendete Symbole

**Idee**: Kleiner Cache für die häufigsten Symbole (z.B. `inc`, `+`, `-`, etc.).

**Vorteile**:
- Schneller für häufige Symbole
- Minimale Code-Änderung
- Kann schrittweise eingeführt werden

**Nachteile**:
- Hilft nur bei häufigen Symbolen
- Andere Symbole bleiben O(n)

**Implementierung**:
```c
// Cache für häufig verwendete Symbole
static CljSymbol *common_symbols_cache[32];
static const char *common_symbol_names[] = {
    "inc", "dec", "+", "-", "*", "/", "=", "<", ">", 
    "first", "rest", "conj", "count", "nil", "true", "false",
    // ...
};

// Fast path für häufige Symbole
CljSymbol* intern_symbol_global_fast(const char *name) {
    // Prüfe Cache zuerst (O(1))
    for (int i = 0; i < 32; i++) {
        if (common_symbol_names[i] && strcmp(common_symbol_names[i], name) == 0) {
            if (common_symbols_cache[i]) {
                return common_symbols_cache[i];
            }
        }
    }
    // Fallback zu normalem Interning
    return intern_symbol_global(name);
}
```

### Option 4: Lazy Interning (nur wenn wirklich nötig)

**Idee**: Interne nur, wenn Symbol wirklich in Map gespeichert/gesucht wird.

**Problem**: Wir müssen trotzdem internieren, um konsistente Pointer zu haben.

### Option 5: Symbol-Pointer-Check vor Interning

**Idee**: Wenn Symbol bereits einen Pointer hat, der in der Symbol-Tabelle ist, verwende diesen.

**Problem**: Wie prüfen, ob Pointer in Tabelle ist? Wieder O(n).

---

## Empfehlung: Hybrid-Ansatz

### Kurzfristig: Cache für häufige Symbole (Option 3)

**Vorteile**:
- Schnelle Implementierung
- Löst 80% der Fälle (häufige Symbole)
- Minimale Code-Änderung

**Implementierung**:
- Cache für ~20-30 häufigste Symbole
- Fast-Path in `intern_symbol_global()`
- Fallback zu normalem Interning

### Langfristig: Hash-Map für Symbol-Tabelle (Option 2)

**Vorteile**:
- O(1) für alle Symbole
- Skaliert besser
- Professionelle Lösung

**Implementierung**:
- Symbol-Tabelle als `CljMap` statt `CljVector`
- Hash-Funktion für Symbol-Namen
- Migration der bestehenden Symbole

---

## Performance-Impact

### Aktuell (O(n))
- 100 Symbole in Tabelle → 100 Vergleiche pro Lookup
- Bei 1000 Symbol-Lookups → 100.000 Vergleiche

### Mit Cache (Option 3)
- Häufige Symbole (80%) → 1 Vergleich (Cache)
- Seltene Symbole (20%) → 100 Vergleiche
- Bei 1000 Lookups → ~20.000 Vergleiche (80% Reduktion)

### Mit Hash-Map (Option 2)
- Alle Symbole → ~1 Vergleich (Hash-Lookup)
- Bei 1000 Lookups → ~1.000 Vergleiche (99% Reduktion)

---

## Pragmatische Lösung für jetzt

**Für die aktuelle Implementierung**: 

Die O(n) Komplexität ist akzeptabel, weil:
1. **Symbol-Tabelle wächst langsam**: Meist < 200 Symbole
2. **Interning ist idempotent**: Nach erstem Aufruf wird Pointer gecacht
3. **String-Vergleich ist schnell**: `clj_equal()` ist optimiert

**Aber**: Bei vielen Symbolen (> 500) wird es langsam.

**Empfehlung**: 
- **Jetzt**: Implementierung wie geplant (Interning ist notwendig für Pointer-Konsistenz)
- **Später**: Hash-Map für Symbol-Tabelle (wenn Performance-Problem auftritt)

---

## Alternative: Interning nur einmal pro Symbol

**Optimierung**: Wenn Symbol bereits interniert ist (Pointer in Tabelle), verwende diesen direkt.

**Problem**: Wie erkennen, ob Symbol bereits interniert ist? Pointer-Vergleich funktioniert nicht.

**Lösung**: `intern_symbol_global()` ist bereits optimiert - wenn Symbol existiert, wird existierender Pointer zurückgegeben. Das Problem ist nur die Suche beim ersten Aufruf.

**Fazit**: Die O(n) Suche ist notwendig, aber nur beim ersten Aufruf pro Symbol. Danach ist der Pointer bekannt und wird direkt zurückgegeben.



