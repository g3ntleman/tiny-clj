# Implementation Plan: (ns) Function

## Ziel

```clojure
user=> (ns foo.bar)
nil
foo.bar=> (+ 1 2)
3
foo.bar=> (ns user)
nil
user=> *ns*
user
```

---

## Status Quo

### ✅ Bereits Vorhanden

**Namespace-Infrastruktur (in `namespace.c`):**
```c
CljNamespace* ns_get_or_create(const char *name, const char *file)
CljNamespace* ns_find(const char *name)
void evalstate_set_ns(EvalState *st, const char *ns_name)
```

**Funktioniert bereits:**
- ✅ Namespaces erstellen/finden
- ✅ Namespace wechseln (C-Ebene)
- ✅ `*ns*` Variable (zeigt aktuellen Namespace)
- ✅ REPL-Prompt zeigt Namespace (`user=>`)

**Fehlt:**
- ❌ `(ns)` Funktion für Clojure-Code

---

## Implementierungsplan

### Option 1: Native C-Funktion (Empfohlen) ⭐

**Aufwand:** 20-25 Zeilen C-Code  
**Komplexität:** ⭐ Trivial  
**Dateien:** 2 (`function_call.c`, `clj_symbols.c`)

#### Schritt 1: Symbol registrieren (2 Zeilen)

**File:** `src/clj_symbols.c`

```c
// In init_special_symbols():
SYM_NS = intern_symbol_global("ns");  // Neue Zeile

// In clj_symbols.h:
extern CljObject *SYM_NS;  // Neue Zeile
```

#### Schritt 2: eval_ns() Funktion (15 Zeilen)

**File:** `src/function_call.c`

```c
CljObject* eval_ns(CljObject *list, CljObject *env, EvalState *st) {
    if (!list || !st) return clj_nil();
    
    // Get namespace name (first argument)
    CljObject *ns_name_obj = list_nth(list, 1);
    if (!ns_name_obj || ns_name_obj->type != CLJ_SYMBOL) {
        eval_error("ns expects a symbol", st);
        return clj_nil();
    }
    
    CljSymbol *ns_sym = as_symbol(ns_name_obj);
    if (!ns_sym || !ns_sym->name) return clj_nil();
    
    // Switch to namespace (creates if not exists)
    evalstate_set_ns(st, ns_sym->name);
    
    return clj_nil();
}
```

#### Schritt 3: Integration in eval_list (3 Zeilen)

**File:** `src/function_call.c` (in `eval_list()`)

```c
if (sym_is(op, "ns")) {
    return eval_ns(list, env, st);
}
```

#### Schritt 4: Header-Deklaration (1 Zeile)

**File:** `src/function_call.h`

```c
CljObject* eval_ns(CljObject *list, CljObject *env, EvalState *st);
```

---

### Option 2: Interpretierte Clojure-Funktion

**Aufwand:** 5 Zeilen Clojure  
**Komplexität:** ⭐⭐⭐ Schwierig  
**Problem:** `ns` ist Spezialform, kein normaler Function Call

```clojure
; NICHT MÖGLICH - ns ist Spezialform!
(def ns (fn [name] ...))  ; Kann namespace nicht von innen wechseln
```

**Fazit:** Option 2 nicht umsetzbar für `ns`.

---

## Empfehlung: Option 1 (Native)

### Implementation Summary

| Step | File | Lines | Complexity |
|------|------|-------|------------|
| 1. Symbol | clj_symbols.c/h | 2 | ⭐ Trivial |
| 2. eval_ns() | function_call.c | 15 | ⭐ Trivial |
| 3. Integration | function_call.c | 3 | ⭐ Trivial |
| 4. Header | function_call.h | 1 | ⭐ Trivial |
| **TOTAL** | **4 files** | **21 lines** | **⭐ Sehr einfach** |

### Aufwand-Schätzung

- **Implementierung:** 10 Minuten
- **Tests:** 5 Minuten
- **TOTAL:** **~15 Minuten**

### Test-Strategie

```bash
# Manual REPL test
./tiny-clj-repl -e '(ns foo.bar)' -e '*ns*'  # Should print: nil, foo.bar

# MinUnit test (optional)
test_ns_function() {
    assert_type(eval_code("(ns test.ns)"), CLJ_NIL);
    assert_equal(current_namespace(), "test.ns");
}
```

---

## Edge Cases

| Case | Behavior | Implementation |
|------|----------|----------------|
| `(ns)` ohne Argument | Error | Check argc |
| `(ns 123)` non-symbol | Error | Type check |
| `(ns "string")` | Error | Type check |
| `(ns foo.bar.baz)` dots | ✅ Works | Symbol name allows dots |
| Namespace nicht existent | Create | `ns_get_or_create()` |

---

## Alternative: Erweiterte ns-Funktionalität

Für später (nicht jetzt):

```clojure
; Mit :require, :import, etc (wie Java Clojure)
(ns foo.bar
  (:require [clojure.string :as str]))
```

**Aufwand:** ⭐⭐⭐⭐ Komplex (100+ Zeilen)  
**Für später:** Phase 2

---

## Entscheidung

**Empfehlung:** ✅ **Option 1 implementieren**

**Begründung:**
- Super einfach (21 Zeilen)
- Nutzt vorhandene Infrastruktur
- Clojure-kompatibel
- Ergänzt perfekt den neuen Namespace-Prompt

**Nächste Schritte:**
1. Symbol SYM_NS registrieren
2. eval_ns() implementieren  
3. In eval_list() integrieren
4. Testen mit `-e` Flag

**Soll ich das jetzt implementieren?**

