# defn-Problem in tiny-clj - Zusammenfassung

## Problem-Beschreibung

Das `defn`-Problem in tiny-clj verhindert die Definition von Funktionen, was die Durchführung von Benchmarks mit rekursiven Algorithmen (wie Fibonacci) unmöglich macht.

## Symptome

### 1. Symbol-Auflösung Problem
```clojure
(defn test-fn [] 42)
;; EXCEPTION: RuntimeException: Unable to resolve symbol: defn in this context
```

### 2. Namespace-Problem
```clojure
(clojure.core/defn test-fn [] 42)
;; Funktioniert, aber Funktion ist nicht verfügbar
```

### 3. Funktionsaufruf Problem
```clojure
(clojure.core/defn test-fn [] 42) (test-fn)
;; Keine Ausgabe, Funktion nicht aufrufbar
```

## Technische Analyse

### Implementierung
`defn` ist in tiny-clj auf zwei Arten implementiert:

1. **Als Builtin** (`native_defn` in `src/builtins.c:1335-1439`)
2. **Als Special Form** (`eval_defn` in `src/function_call.c:1990-2155`)

### Registrierung
```c
// In src/builtins.c:1564
register_builtin_in_namespace("defn", native_defn);
```

### Probleme

#### 1. Namespace-Auflösung
- `defn` ist als Builtin registriert, aber nicht im aktuellen Namespace verfügbar
- Expliziter Namespace (`clojure.core/defn`) funktioniert teilweise
- Funktion wird nicht korrekt im Namespace gespeichert

#### 2. Funktionserstellung
```c
// In native_defn (Zeile 1420)
CljObject *fn_obj = make_function(params, param_count, body, NULL, NULL);
```

#### 3. Namespace-Bindung
```c
// In native_defn (Zeile 1435)
ns_define(st->current_ns, name_sym, fn_obj);
```

### Root Cause
Das Problem liegt wahrscheinlich in der Namespace-Verwaltung:
- `defn` wird nicht im aktuellen Namespace (`user`) registriert
- Funktionen werden nicht korrekt im Namespace gespeichert
- Symbol-Auflösung funktioniert nicht für benutzerdefinierte Funktionen

## Auswirkungen auf Benchmarks

### 1. Fibonacci-Benchmark
```clojure
(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
;; Funktioniert nicht - kann keine rekursiven Funktionen definieren
```

### 2. Performance-Tests
- Keine benutzerdefinierten Funktionen möglich
- Nur eingebaute Funktionen testbar
- Rekursive Algorithmen nicht implementierbar

### 3. Vergleich mit Clojure
- Clojure: `defn` funktioniert perfekt
- tiny-clj: `defn` funktioniert nicht
- Ungleiche Testbedingungen

## Lösungsansätze

### 1. Namespace-Fix
- Korrekte Registrierung von `defn` im `user` Namespace
- Fix der Symbol-Auflösung für benutzerdefinierte Funktionen

### 2. Funktionserstellung
- Verbesserung der `make_function` Implementierung
- Korrekte Closure-Umgebung für rekursive Funktionen

### 3. Namespace-Bindung
- Fix der `ns_define` Funktion
- Korrekte Speicherung von Funktionen im Namespace

## Workarounds

### 1. Eingebaute Funktionen verwenden
```clojure
;; Statt: (defn fib [n] ...)
;; Verwende: Eingebaute Funktionen für einfache Tests
```

### 2. System-Zeit für Benchmarks
```bash
# Statt: (time (fib 20))
# Verwende: time bash -c 'echo "..." | ./tiny-clj-repl'
```

### 3. Einfache Ausdrücke testen
```clojure
;; Statt: Rekursive Funktionen
;; Teste: Einfache Arithmetik und eingebaute Funktionen
```

## Priorität

**Hoch** - Das `defn`-Problem verhindert:
- Rekursive Algorithmen (Fibonacci, etc.)
- Komplexe Benchmarks
- Faire Vergleiche mit Clojure
- Vollständige Funktionalitätstests

## Nächste Schritte

1. **Namespace-Debugging**: Warum wird `defn` nicht im `user` Namespace gefunden?
2. **Funktionserstellung**: Warum werden Funktionen nicht korrekt erstellt?
3. **Symbol-Auflösung**: Warum funktioniert die Auflösung nicht?
4. **Tests**: Unit-Tests für `defn` implementieren

---

*Analyse durchgeführt am: $(date)*
*Betroffene Dateien: src/builtins.c, src/function_call.c*
*Commit: $(git rev-parse --short HEAD)*

