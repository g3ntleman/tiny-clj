# TCO-Vereinfachungsvorschläge

## Aktuelle Situation
- **535 Zeilen Code** in `optimize.c`
- Komplexe AST-Transformation für alle Special Forms (`if`, `let`, `cond`, `when`, `do`)
- Vollständige Tail-Position-Erkennung mit rekursiven Checks
- Automatische Transformation von rekursiven Tail-Calls zu `recur`

## Vorschlag 1: Nur explizite `recur` (Minimal)
**Code-Reduktion: ~80% (ca. 100 Zeilen)**

### Strategie
- Keine automatische Transformation
- Nur Validierung, dass `recur` in Tail-Position ist
- Benutzer muss `recur` explizit schreiben

### Vorteile
- Sehr wenig Code
- Keine AST-Transformation nötig
- Klar und explizit (wie Clojure)

### Nachteile
- Benutzer muss `recur` manuell schreiben
- Keine automatische Optimierung

### Implementierung
```c
// Nur Validierung - ca. 50 Zeilen
bool is_tail_position_simple(CljObject *expr, CljObject *body) {
    if (!is_type(body, CLJ_LIST)) return false;
    CljList *list = as_list((ID)body);
    // Finde letztes Element
    while (list && list->rest) {
        list = as_list((ID)list->rest);
    }
    return list && list->first == expr;
}

void validate_recur_positions(CljObject *body, CljObject *parent_body) {
    // Rekursiv durch AST gehen, nur `recur`-Calls prüfen
    // ~50 Zeilen
}
```

---

## Vorschlag 2: Einfache Tail-Position (Mittel)
**Code-Reduktion: ~60% (ca. 200 Zeilen)**

### Strategie
- Nur letztes Element in einer Liste ist Tail-Position
- Keine Transformation in `if`, `let`, `cond` etc.
- Automatische Transformation nur für einfache Tail-Calls

### Vorteile
- Deutlich weniger Code
- Funktioniert für die meisten Fälle
- Einfach zu verstehen

### Nachteile
- Funktioniert nicht in `if`-Branches, `let`-Bodies etc.
- Benutzer muss für komplexe Fälle `recur` explizit verwenden

### Implementierung
```c
// Vereinfachte Tail-Position-Erkennung - ca. 30 Zeilen
bool is_tail_position(CljObject *expr, CljObject *body) {
    if (!is_type(body, CLJ_LIST)) return false;
    CljList *list = as_list((ID)body);
    // Nur letztes Element in Liste
    while (list && list->rest) {
        list = as_list((ID)list->rest);
    }
    return list && list->first == expr;
}

// Einfache Transformation - ca. 100 Zeilen
CljObject* transform_recursive_tail_calls(CljObject *body, CljObject *func_name) {
    if (!is_type(body, CLJ_LIST)) return RETAIN(body), body;
    
    CljList *list = as_list((ID)body);
    if (is_recursive_call(body, func_name) && is_tail_position(body, parent_body)) {
        return transform_to_recur(list);
    }
    
    // Rekursiv durch rest gehen, aber keine Special-Form-Transformation
    // ~70 Zeilen
}
```

---

## Vorschlag 3: Lazy Transformation (Empfohlen)
**Code-Reduktion: ~40% (ca. 300 Zeilen)**

### Strategie
- Transformation nur bei `defn`, nicht bei `fn`
- Nur `if` und einfache Listen transformieren
- `let`, `cond` etc. ignorieren (Benutzer verwendet `recur`)

### Vorteile
- Guter Kompromiss zwischen Funktionalität und Code-Größe
- Deckt die häufigsten Fälle ab
- Einfacher zu warten

### Nachteile
- Nicht vollständig für alle Special Forms

### Implementierung
```c
// Vereinfachte Tail-Position - nur `if` und Listen
bool is_tail_position(CljObject *expr, CljObject *body) {
    if (!is_type(body, CLJ_LIST)) return false;
    CljList *list = as_list((ID)body);
    
    // Letztes Element in Liste
    CljList *last = list;
    while (last && last->rest) {
        last = as_list((ID)last->rest);
    }
    if (last && last->first == expr) return true;
    
    // Nur `if`-Branches behandeln
    if (list->first == SYM_IF) {
        // then und else sind Tail-Position
        // ~20 Zeilen
    }
    
    return false;
}

// Transformation nur für einfache Fälle
CljObject* transform_recursive_tail_calls(...) {
    // Nur `if` transformieren, `let`/`cond` ignorieren
    // ~200 Zeilen
}
```

---

## Vorschlag 4: Compile-Time Flag (Flexibel)
**Code-Reduktion: Konfigurierbar**

### Strategie
- Compile-Time Flag `ENABLE_FULL_TCO` vs `ENABLE_SIMPLE_TCO`
- Bei `SIMPLE_TCO`: Nur Vorschlag 2
- Bei `FULL_TCO`: Aktuelle Implementierung

### Vorteile
- Flexibel je nach Anforderungen
- Kann für Embedded-Systeme reduziert werden

---

## Empfehlung

**Vorschlag 2 (Einfache Tail-Position)** ist der beste Kompromiss:
- Reduziert Code um ~60%
- Funktioniert für die meisten praktischen Fälle
- Einfach zu verstehen und zu warten
- Benutzer kann für komplexe Fälle `recur` explizit verwenden

**Alternative: Vorschlag 1** wenn Code-Größe kritisch ist (Embedded).

